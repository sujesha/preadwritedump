/*
 * This is just to siphon trace data from input file in debugfs into
 * user-specified directory.
 * Base name of trace file is mandatory input -- eg. pscanevents, pioevents, etc.
 *
 *
 * Author: Sujesha Sudevalayam
 *
 * Code heavily borrowed from blktrace.c
 * Credit: Jens Axboe <axboe@suse.de>
 *
 */
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/statfs.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <ctype.h>
#include <getopt.h>
#include <errno.h>


static char psiphon_version[] = "0.1.0";

#define BUF_SIZE    (512 * 1024)
#define BUF_NR      (4)
#define MAX_BUF_NR      (16)
#define OFILE_BUF   (128 * 1024)
#define DEBUGFS_TYPE    0x64626720
#define HOSTNAME_LEN	(25)

#define min(a, b)   ((a) < (b) ? (a) : (b))
#define max(a, b)   ((a) > (b) ? (a) : (b))

#define S_OPTS  "i:r:kw:vb:n:D:c"
static struct option l_opts[] = {
    {
        .name = "base-name",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'i'
    },
    {
        .name = "relay",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'r'
    },
    {
        .name = "kill",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'k'
    },
    {
        .name = "create-new-output-files",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'c'
    },
    {
        .name = "stopwatch",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'w'
    },
    {
        .name = "version",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'v'
    },
    {
        .name = "buffer-size",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'b'
    },
    {
        .name = "num-sub-buffers",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'n'
    },
    {
        .name = "output-dir",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'D'
    },
};

/*
 * Per input file information --- borrowed from btrecord.c & tweaked
 *
 * @base_name:  file type, eg. pscanevents, pioevents
 * @file_name:  Fully qualified name for this input file
 * @cpu:    CPU that this file was collected on
 * @ifd:    Input file descriptor (when opened)
 */
struct infile_info {
    char *base_name;
    int cpu, ifd;
    char fn[MAXPATHLEN + 64];	/* The file's name */
};


struct thread_information {
    int cpu;					/* The cpu for this thread */
    pthread_t thread;			/* This thread */
	struct infile_info ii;		/* Input file this is associated with */
    void *fd_buf;

    FILE *ofile;				/* Output file stream */
    char *ofile_buffer;
    off_t ofile_offset;
    int ofile_mmap;

    unsigned long long data_read;

    int exited;

    /*
     * mmap controlled output files
     */
    unsigned long long fs_size;
    unsigned long long fs_max_size;
    unsigned long fs_off;
    void *fs_buf;
    unsigned long fs_buf_len;
};

struct base_information 
{
	char *path;
	struct thread_information *threads;
};

static int ncpus;
static struct thread_information *thread_information;
static int nbases;
static struct base_information *base_information;
static char *host_name;   					/* Host name for prefixing */

/* command line option globals */
static char *debugfs_path;
static char *output_dir;
static int kill_running_siphon;
static unsigned long buf_size = BUF_SIZE;
static unsigned long buf_nr = BUF_NR;
static unsigned int page_size;
static unsigned int recreate_output = 0;

#define is_done()   (*(volatile int *)(&done))
static volatile int done;

#define is_siphon_stopped()  (*(volatile int *)(&siphon_stopped))
static volatile int siphon_stopped;

#define __for_each_bip(__b, __bi, __e, __i) \
    for (__i = 0, __b = __bi; __i < __e; __i++, __b++)
#define for_each_bip(__b, __i)      \
    __for_each_bip(__b, base_information, nbases, __i)
#define __for_each_tip(__b, __t, __ncpus, __j)  \
    for (__j = 0, __t = (__b)->threads; __j < __ncpus; __j++, __t++)
#define for_each_tip(__b, __t, __j) \
    __for_each_tip(__b, __t, ncpus, __j)

//TODO: remove this when sure
#if 0
#define __for_each_tip(__t, __ti, __e, __i) \
    for (__i = 0, __t = __ti; __i < __e; __i++, __t++)
#define for_each_tip(__t, __i)      \
    __for_each_tip(__t, thread_information, ncpus, __i)
#endif

/* handling Ctrl^C - this should be sent by install.sh or uninstall.sh
 *		whenever they do rmmod on kernel module 
 * Or should this program be sending the STOP signal to the kernel module? Maybe
 * 		do a fork() and execute rmmod in the child process?
 */
static void handle_sigint(__attribute__((__unused__)) int sig)
{
    /*
     * stop trace so we can reap currently produced data
     */
    done = 1;
}

static void wait_for_data(struct thread_information *tip, int timeout)
{
    /* This is for the poll() ahead, checking to see if file has data to read */
    struct pollfd pfd = { .fd = tip->ii.ifd, .events = POLLIN };

    /* check "done" flag to see if tracing is running or finito */
    while (!is_done()) {
        /* while tracing is running, poll the file and wait till timeout*/
        if (poll(&pfd, 1, timeout) < 0) {
            perror("poll");
            break;
        }
        /* If file has data to read, break from while loop & return */
        if (pfd.revents & POLLIN)
            break;
    }
}


/* formerly read_data_file() in blktrace.c */
static int read_data(struct thread_information *tip, void *buf, unsigned len)
{
    int ret = 0;

    do {
        /* poll tip->fd for 100 units time to see for data to read 
		 * Repeated polling till there is data
		 */
        wait_for_data(tip, 100);

        /* read the data available */
        ret = read(tip->ii.ifd, buf, len);
        /* if nothing was read though it was avail, continue reading */
        if (!ret)
            continue;
        else if (ret > 0)
            return ret;
        else {
            if (errno != EAGAIN) {
                perror(tip->ii.fn);
                fprintf(stderr,"Thread %d failed read of %s\n",
                    tip->cpu, tip->ii.fn);
                break;
            }
            continue;
        }
    } while (!is_done());
    /* keep doing above until tracing is not finito */

    return ret;

}

/*
 * For file output, truncate and mmap the file appropriately --- mmap_subbuf()
 */
static int get_subbuf(struct thread_information *tip, unsigned int maxlen)
{
    /* Get fd associated to the open file */
    int ofd = fileno(tip->ofile);
    int ret;
    unsigned long nr;

    /*
     * extend file, if we have to. use chunks of 16 subbuffers.
     */
    if (tip->fs_off + maxlen > tip->fs_buf_len) {
        if (tip->fs_buf) {
            munlock(tip->fs_buf, tip->fs_buf_len);	/* unlock to allow swap */
            munmap(tip->fs_buf, tip->fs_buf_len);	/* releases memory mapping of fs_buf */
            tip->fs_buf = NULL;
        }

        /* compute max size that file needs to be */
        tip->fs_off = tip->fs_size & (page_size - 1);
        nr = max(MAX_BUF_NR, buf_nr);
        tip->fs_buf_len = (nr * buf_size) - tip->fs_off;
        tip->fs_max_size += tip->fs_buf_len;

        /* ftruncate() for truncating file that is open for writing,
		 * else could have used truncate() for a closed writable file */
        if (ftruncate(ofd, tip->fs_max_size) < 0) {
            perror("ftruncate");
            return -1;
        }

        /* map such that data can be written, write changes are shared */
        tip->fs_buf = mmap(NULL, tip->fs_buf_len, PROT_WRITE,
                   MAP_SHARED, ofd, tip->fs_size - tip->fs_off);
        if (tip->fs_buf == MAP_FAILED) {
            perror("mmap");
            return -1;
        }
        /* lock the mmap'ed area into virtual address space of process 
		 * so that it can not be swapped out by kernel */
        mlock(tip->fs_buf, tip->fs_buf_len);
    }

    /* Depending on the net mode, read data accordingly from file or net.
	 * Read data maxlen bytes from tip->fd into specified buf  */
    ret = read_data(tip, tip->fs_buf + tip->fs_off, maxlen);
    /* Increment byte counters accordingly */
    if (ret >= 0) {
        tip->data_read += ret;
        tip->fs_size += ret;
        tip->fs_off += ret;
        return 0;
    }

    return -1;
}


static void close_thread(struct thread_information *tip)
{
    if (tip->ii.ifd != -1)
        close(tip->ii.ifd);		/* close file descriptor of this thread */
    if (tip->ofile)
        fclose(tip->ofile);		/* close file pointer of output file */
    if (tip->ofile_buffer)
        free(tip->ofile_buffer);/* free output buffer */
    if (tip->fd_buf)
        free(tip->fd_buf);		/* free file buffer */

    tip->ii.ifd = -1;
    tip->ofile = NULL;
    tip->ofile_buffer = NULL;
    tip->fd_buf = NULL;
}

static void tip_ftrunc_final(struct thread_information *tip)
{
    /*
     * truncate to right size and cleanup mmap
     */
    if (tip->ofile_mmap && tip->ofile) {
        int ofd = fileno(tip->ofile);	/* Get fd for output file stream */

        if (tip->fs_buf)
            munmap(tip->fs_buf, tip->fs_buf_len);	 /* unmap the buf */

        /* ftruncate the output file according to its size */
        if (ftruncate(ofd, tip->fs_size) < 0)
		{
            perror("ftruncate");
        }
    }
}

static void stop_threads(struct base_information *bip)
{
    struct thread_information *tip;
    unsigned long ret;
    int i;

    /* iterating over all threads */
    for_each_tip(bip, tip, i) {
        /* waiting for all threads for this base name to join */
    	/* tip->thread is starting addr of all thread str of this basename */
        (void) pthread_join(tip->thread, (void *) &ret);
        close_thread(tip);
    }
    if (bip->path)
		free(bip->path);
}

static void stop_all_threads(void)
{
	struct base_information *bip;
    int i;

	for_each_bip(bip, i)
        stop_threads(bip);
}

#if 0
static void exit_siphon()
{
    if (!is_siphon_stopped()) {
        siphon_stopped = 1;
        stop_all_threads();
		if (base_information)
			free(base_information);
    }
}
#endif

/* same as exit_siphon, except that it forwards status value */
static void stop_siphon(int status)
{
    if (!is_siphon_stopped()) {
        siphon_stopped = 1;
        stop_all_threads();
		if (base_information)
    		free(base_information);
    }

    exit(status);
}

/* This is started as a thread from start_thread() */
static void *thread_main(void *arg)
{
    struct thread_information *tip = arg;
    pid_t pid = getpid();		/* get thread's own pid */
    cpu_set_t cpu_mask;

    CPU_ZERO(&cpu_mask);
    CPU_SET((tip->cpu), &cpu_mask);

    /* setting cpu affinity for thread, j loop in start_threads() */
    if (sched_setaffinity(pid, sizeof(cpu_mask), &cpu_mask) == -1) {
        perror("sched_setaffinity");
        stop_siphon(1);
    }

    /* construct input filename since we have to read debugfs */
    snprintf(tip->ii.fn, sizeof(tip->ii.fn), "%s/%s%d",
            debugfs_path, tip->ii.base_name, tip->cpu);
    tip->ii.ifd = open(tip->ii.fn, O_RDONLY);	/* open file read-only */
    if (tip->ii.ifd < 0) {
        perror(tip->ii.fn);
        fprintf(stderr,"Thread %d failed open of %s\n", tip->cpu,
            tip->ii.fn);
        stop_siphon(1);
    }

    /* continuously get data while trace not finito */
    while (!is_done()) {
        if (get_subbuf(tip, buf_size) < 0)
            break;
    }

#if 0
    /*
     * trace is stopped, pull data until we get a short read
     */
    while (get_subbuf(tip, buf_size) > 0)
        ;
#endif

    /* ftruncate tip->ofile according to size, for the last time? */
    tip_ftrunc_final(tip);
    tip->exited = 1;
    return NULL;
}

#if 0
static int write_data(struct thread_information *tip, void *buf, 
					unsigned int buf_len)
{
    int ret;

    if (!buf_len)
        return 0;

    /* write databuf to output file stream - first arg is src & last is dest */
    ret = fwrite(buf, buf_len, 1, tip->ofile);
    if (ferror(tip->ofile) || ret != 1) {
        perror("fwrite");
        clearerr(tip->ofile);
        return 1;
    }

    return 0;
}

/* Formerly flush_subbuf_file() in blktrace.c */
//TODO: maybe we dont need this? it is only used for net & pipe?
static int flush_subbuf(struct thread_information *tip,
                 struct tip_subbuf *ts)
{
    unsigned int offset = 0;
    int events = 0;

    /*
     * surplus from last run
     */
    if (tip->leftover_ts) {
        struct tip_subbuf *prev_ts = tip->leftover_ts;

        if (prev_ts->len + ts->len > prev_ts->max_len) {
            prev_ts->max_len += ts->len;
            prev_ts->buf = realloc(prev_ts->buf, prev_ts->max_len);
        }

        memcpy(prev_ts->buf + prev_ts->len, ts->buf, ts->len);
        prev_ts->len += ts->len;

        free(ts->buf);
        free(ts);

        ts = prev_ts;
        tip->leftover_ts = NULL;
    }

    while (offset + sizeof(*t) <= ts->len) {
        t = ts->buf + offset;

        if (verify_trace(t)) {
            write_data(tip, ts->buf, offset);
            return -1;
        }

        pdu_len = t->pdu_len;

        if (offset + sizeof(*t) + pdu_len > ts->len)
            break;

        offset += sizeof(*t) + pdu_len;
        tip->events_processed++;
        tip->data_read += sizeof(*t) + pdu_len;
        events++;
    }

    if (write_data(tip, ts->buf, offset))
        return -1;

    /*
     * leftover bytes, save them for next time
     */
    if (offset != ts->len) {
        tip->leftover_ts = ts;
        ts->len -= offset;
        memmove(ts->buf, ts->buf + offset, ts->len);
    } else {
        free(ts->buf);
        free(ts);
    }

    return events;
}
#endif

static void wait_for_threads(void)
{
    /*
     * we just wait around for siphon threads to exit
     */
        struct thread_information *tip;
        struct base_information *bip;
        int i, j, tips_running;

        do {
            tips_running = 0;
            usleep(100000);

            for_each_bip(bip, i)
				for_each_tip(bip, tip, j)
                    tips_running += !tip->exited;
        } while (tips_running);
}

static int fill_ofname(struct thread_information *tip, char *dst,
               char *host_name)
{
    struct stat sb;
    int len = 0;

    if (output_dir)
        len = sprintf(dst, "%s/", output_dir);
    else
        len = sprintf(dst, "./");

    /* check whether dst exists */
    if (stat(dst, &sb) < 0) {
        if (errno != ENOENT) {
            perror("stat");
            return 1;
        }
        /* try to make the dir if it doesnt exist */
        if (mkdir(dst, 0755) < 0) {
            perror(dst);
            fprintf(stderr, "Can't make output dir\n");
            return 1;
        }
    }

    /* form output filename */
    sprintf(dst + len, "%s.%s%d", host_name, tip->ii.base_name, tip->ii.cpu);

    return 0;
}

static int tip_open_output(struct thread_information *tip)
{
    int mode, vbuf_size;
    char op[128];
    struct stat st;

	/* Get the host name */
	host_name = malloc(HOSTNAME_LEN);
	if (gethostname(host_name, HOSTNAME_LEN))
	{
		perror("gethostnamme");
		return 1;
	}	

	if (fill_ofname(tip, op, host_name))/* fill output file name into op */
    	return 1;
	free(host_name); 
	if (recreate_output)
	    tip->ofile = fopen(op, "w+");		/* open output file in write mode*/
    else
	{
		tip->ofile = fopen(op, "a+");		/* open output file in append mode*/
    	if (stat(op, &st) < 0) 
		{
			tip->fs_size = st.st_size;
		}
        tip->fs_off = tip->fs_size & (page_size - 1);
	}
    tip->ofile_mmap = 1;			/* output file needs to be mmap'ed */
    mode = _IOFBF;
    vbuf_size = OFILE_BUF;		/* 128KB*/

    /* error if output file could not be opened for writing */
    if (tip->ofile == NULL) {
        perror(op);
        return 1;
    }

    /* Malloc buffer of 128KB for output file */
    tip->ofile_buffer = malloc(vbuf_size);
    if (setvbuf(tip->ofile, tip->ofile_buffer, mode, vbuf_size)) {
        perror("setvbuf");
        close_thread(tip);
        return 1;
    }

    return 0;
}

/* start all threads */
static int start_threads(struct base_information *bip)
{
    struct thread_information *tip;
    int j;

    /* iterating over tips */
    for_each_tip(bip, tip, j) {
        /* there is one thread per cpu */
        tip->ii.cpu = j;
        tip->ii.ifd = -1;
		tip->ii.base_name = strdup(bip->path);

        /* open output file */
        if (tip_open_output(tip))
            return 1;

        /* for every cpu thread, creating execution thread */
    	/* thread_information is starting addr of all thread str */
        if (pthread_create(&tip->thread, NULL, thread_main, tip)) {
            perror("pthread_create");
            close_thread(tip);
            return 1;
        }
    }

    /* exit after creating thread per cpu */
    return 0;
}

static int start_siphon(void)
{
    struct base_information *bip;
    int i, j, size;

    /* every base file has ncpus # of threads */
    size = ncpus * sizeof(struct thread_information);
    thread_information = malloc(size * nbases);
    if (!thread_information) {
        fprintf(stderr, "Out of memory, threads (%d)\n", size * nbases);
        return 1;
    }
    /* initializing all to 0 */
    memset(thread_information, 0, size * nbases);

    for_each_bip(bip, i) 
	{
		bip->threads = thread_information + (i * ncpus);
		if (start_threads(bip)) 
		{
			fprintf(stderr, "Failed to start worker threads\n");
			break;
		}
	}
    /* if atleast 1 thread failed, stop all threads & return error */
    if (i != nbases) 
	{
        __for_each_bip(bip, base_information, i, j)
            stop_threads(bip);
        return 1;
    }
	return 0;
}

static int resize_bases(char *path)
{
    int size = (nbases + 1) * sizeof(struct base_information);

    /* retains existing info but adjusts size */
    base_information = realloc(base_information, size);
    if (!base_information) {
        fprintf(stderr, "Out of memory, for basefile %s (%d)\n", path, size);
        return 1;
    }
    /* noting new input base path */
    base_information[nbases].path = strdup(path);
    nbases++;
    return 0;
}

static char usage_str[] = \
    "-i <basefile> [ -r debugfs path ] [ -w time ] [ -D output_dir ] [ -v ] [ -k ]\n\n" \
    "\t-i Base file to read from\n" \
    "\t-r Path to mounted debugfs, defaults to /sys/kernel/debug\n" \
    "\t-D Directory to prepend to output file names\n" \
    "\t-k Kill a running trace\n" \
    "\t-w Stop after defined time, in seconds\n" \
    "\t-b Sub buffer size in KiB\n" \
    "\t-n Number of sub buffers\n" \
    "\t-v Print program version info\n\n";

static void show_usage(char *program)
{
    fprintf(stderr, "Usage: %s %s",program, usage_str);
}

int main(int argc, char *argv[])
{
    static char default_debugfs_path[] = "/sys/kernel/debug";
    struct statfs st;
    int c;
    int stop_watch = 0;

    while ((c = getopt_long(argc, argv, S_OPTS, l_opts, NULL)) >= 0) {
        switch (c) {
		case 'c':
			recreate_output = 1;
			break;
        case 'r':
            debugfs_path = optarg;
            break;
        case 'i':
            /* base name specified */
            if (resize_bases(optarg) != 0)
                return 1;
            break;
        case 'k':
            /* used to kill the running siphon */
            kill_running_siphon = 1;
            break;
        case 'w':
            stop_watch = atoi(optarg);
            if (stop_watch <= 0) {
                fprintf(stderr,
                    "Invalid stopwatch value (%d secs)\n",
                    stop_watch);
                return 1;
            }
            break;
        case 'v':
            printf("%s version %s\n", argv[0], psiphon_version);
            return 0;
        case 'b':
            buf_size = strtoul(optarg, NULL, 10);
            if (buf_size <= 0 || buf_size > 16*1024) {
                fprintf(stderr,
                    "Invalid buffer size (%lu)\n",buf_size);
                return 1;
            }
            buf_size <<= 10;
            break;
        case 'n':
            buf_nr = strtoul(optarg, NULL, 10);
            if (buf_nr <= 0) {
                fprintf(stderr,
                    "Invalid buffer nr (%lu)\n", buf_nr);
                return 1;
            }
            break;
        case 'D':
            output_dir = optarg;
            break;
        default:
            show_usage(argv[0]);
            return 1;
        }
    }

    page_size = getpagesize();

    while (optind < argc) {
        if (resize_bases(argv[optind++]) != 0)
            return 1;
    }

    if (nbases == 0) {
        show_usage(argv[0]);
        return 1;
    }

    if (!debugfs_path)
        debugfs_path = default_debugfs_path;

    if (statfs(debugfs_path, &st) < 0) {
        perror("statfs");
        fprintf(stderr,"%s does not appear to be a valid path\n",
            debugfs_path);
        return 1;
    } else if (st.f_type != (long) DEBUGFS_TYPE) {
        fprintf(stderr,"%s does not appear to be a debug filesystem\n",
            debugfs_path);
        return 1;
    }

    /* if -k option specified, then stop siphoning */
    if (kill_running_siphon) {
        stop_siphon(0);
    }

    ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpus < 0) {
        fprintf(stderr, "sysconf(_SC_NPROCESSORS_ONLN) failed\n");
        return 1;
    }
    signal(SIGINT, handle_sigint);
    signal(SIGHUP, handle_sigint);
    signal(SIGTERM, handle_sigint);
    signal(SIGALRM, handle_sigint);
    signal(SIGPIPE, SIG_IGN);

    /* start all siphoning threads */
    if (start_siphon() != 0)
        return 1;

    /* registering to be called at normal exit */
//    atexit(exit_siphon);		no need because we only do 1 job here, not 2 jobs

    if (stop_watch)
        alarm(stop_watch);

    wait_for_threads();
	stop_siphon(0);

	/* Will not reach this due to exit() in above stop_siphon() */
    return 0;
}


