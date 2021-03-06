/*
 * This is just to siphon trace data from input file in debugfs into
 * user-specified directory.
 * Base name of trace file is mandatory input -- eg. pscanevents,pioevents,etc.
 *
 *
 * Author: Sujesha Sudevalayam
 *
 * Contains some code borrowed from blktrace.c
 * Credit: Jens Axboe <axboe@suse.de>
 *
 */
#include <pthread.h>
#include <sys/types.h>
#include <asm/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/statfs.h>
#include <sys/poll.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <ctype.h>
#include <getopt.h>
#include <errno.h>
#include <assert.h>

#include "barrier.h"
#include "subbuf_conf.h"
#include "trace_struct.h"
#include "endianness.h"

#include <linux/kdev_t.h>

static char psiphon_version[] = "0.1.0";
extern int configfd;
extern volatile int producerdone;
extern char *pidfile;
char *progname = "pdatadump";	/* hardcoded */

int write_0_and_exit(void);
int write_pid_to_debugfs(void);

#define TRACE(fmt, msg...) {                                               \
       fprintf(stderr, "[%s] " fmt, __FUNCTION__, ##msg);                  \
       }

#ifndef WHERE
#define WHERE TRACE("In file %s, line %d, func %s\n",  __FILE__, __LINE__, __FUNCTION__)
#endif

#if 0
#define MINORBITS   20
#define MINORMASK   ((1U << MINORBITS) - 1)

#define MAJOR(dev)  ((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)  ((unsigned int) ((dev) & MINORMASK))
#define MKDEV(ma,mi)    (((ma) << MINORBITS) | (mi))
#endif

#define STDOUT_BUF	(512 * 8 + 256)

/* Commented because these values should not be redefined here. Use
 * the same values as those used in the kernel module --- ie. while 
 * doing relay_open(), CURR_SUBBUF_SIZE & CURR_N_SUBBUFS
#define BUF_SIZE    (512 * 1024)
#define BUF_NR      (4)
 */

#define H_BUF_NR      (16)
#define LINE_BUFSIZE	(512 * 8 + 256)
#define OFILE_BUF   (128 * 1024)
//#define OFILE_BUF   (2 * 1024 * 1024)
#define DEBUGFS_TYPE    0x64626720			/* DEBUGFS_MAGIC of include/linux/magic.h */
#define HOSTNAME_LEN	(PATH_MAX)

#define min(a, b)   ((a) < (b) ? (a) : (b))
#define max(a, b)   ((a) > (b) ? (a) : (b))

#define S_OPTS  "i:r:kw:vb:n:D:co:t:"
static struct option l_opts[] = {
    {
        .name = "base-name",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'i'
    },
    {
        .name = "relayfs",
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
    {
        .name = "trace-dev",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 't'
    },
    {
        .name = "output",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'o'
    },
};

/*
 * Per input file information --- borrowed from btrecord.c & tweaked
 *
 * @base_name:  file type, eg. pscanevents, pioevents
 * @file_name:  Fully qualified name for this input file
 * @ifd:    Input file descriptor (when opened)
 */
struct infile_info {
    char *base_name;
    int ifd;
    char fn[MAXPATHLEN + 64];	/* The file's name */
	char localname[64];			/* The file's localname */
};

/* Borrowed from blktrace */
struct tip_subbuf {
    void *buf;
    unsigned int len;
    unsigned int max_len;
};

#define FIFO_SIZE   (1024)  /* should be plenty big! */
#define CL_SIZE     (128)   /* cache line, any bigger? */

/* Borrowed from blktrace */
struct tip_subbuf_fifo {
    int tail __attribute__((aligned(CL_SIZE)));
    int head __attribute__((aligned(CL_SIZE)));
    struct tip_subbuf *q[FIFO_SIZE];
};


struct thread_information {
    int cpu;					/* The cpu for this thread */
    pthread_t thread;			/* This thread */
	struct infile_info ii;		/* Input file this is associated with */
    void *fd_buf;

    FILE *ofile;				/* Output file stream */
    char *ofile_buffer;
	int ofile_stdout;
    off_t ofile_offset;
    int ofile_mmap;

	int (*get_subbuf)(struct thread_information *, unsigned int);

	unsigned long events_processed;
    unsigned long long data_read;

    int exited;

    /*
     * piped fifo buffers
     */
    struct tip_subbuf_fifo fifo;
    struct tip_subbuf *leftover_ts;

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
	unsigned long drop_count;
	struct thread_information *threads;
};

static int ncpus;
static struct thread_information *thread_information;
static int nbases = 0;
struct base_information *base_information;
static char *host_name;   					/* Host name for prefixing */

/* command line option globals */
char *debugfs_path;
static char *output_name = "f";
static char *output_dir = NULL;
static char *trace_dev = NULL;
static int kill_running_siphon;
static unsigned int page_size;
static unsigned int recreate_output = 0;
/* Using the parameters of I/O tracing channel for all channels 
 * that might get created in psiphon. Else, need to specify each
 * channel's size separately on psiphon command line itself. 
 * Right now, making psiphon generic is not our true priority, so
 * follow this hack for now.
 */
static unsigned long buf_size = CURR_SUBBUF_SIZE_IO;
static unsigned long buf_nr = CURR_N_SUBBUFS_IO;

#define is_producerdone()   (*(volatile int *)(&producerdone))

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

/* handling Ctrl^C - this should be sent by install.sh or uninstall.sh
 *		whenever they do rmmod on kernel module 
 */
void handle_sigint(__attribute__((__unused__)) int sig)
{
#ifdef DEBUG_SS
        WHERE;
#endif

#if 0
    if ( (pid = vfork()) < 0)
    {
        fprintf(stderr, "vfork failed pid = %d\n", pid);
    }
    else if (pid == 0)
    {

	rc = execl("/sbin/rmmod", "rmmod", "pdatadump", (char *)0);

	/* We will come here only if execl() fails */
    if (ret < 0)
#endif
	system("sudo /sbin/rmmod pdatadump &");
}


/* Returns 0 to indicate timeout + is_done()
 * Returns >0 to indicate successful read
 */
int wait_for_data(struct thread_information *tip, int timeout)
{
	int ret = 0;
#ifdef DEBUG_SS
        WHERE;
#endif
    /* This is for the poll() ahead, checking to see if file has data to read */
    struct pollfd pfd = { .fd = tip->ii.ifd, .events = POLLIN };

#if 0
    /* check "done" flag to see if Ctrl^C received */
    while (!is_done()) {
        /* while tracing is running, poll the file and wait till timeout*/
        if (poll(&pfd, 1, timeout) < 0) {
            perror("poll timeout");
            break;
        }
#endif
	/* producerdone == 1 can happen due to ./uninstall.sh. At this time, 
	 * need to read up all remaining data from the files before
	 * acknowledging to pdatadump that we are exiting.
	 * So, keep polling as long as possible, exit peacefully
	 * if timeout occurs when producerdone == 1.
	 */
	while ((ret = poll(&pfd, 1, timeout)) >= 0)
	{
        /* If file has data to read, break from while loop & return */
        if (pfd.revents & POLLIN) {
            break;
        }

		if (!ret && is_producerdone())	/* timeout causes ret == 0 */
		{
			/* Timeout => no more data. This is okay, if producerdone == 1
			 * due to signaling from kernel
			 */
			break;
		}

		/* Check if an overflow error has occurred, causing POLLERR */
		if (pfd.revents & POLLERR) 
		{
			fprintf(stderr, "Got error in polling, could be"
					 " due to overflow of buffers?\n");
			break;
		}

		/* If no data but output is to stdout, then break & return */
		if (tip->ofile_stdout)
			break;	

		/* If timeout, but done != 1, then continue in this while loop */
    }

	/* poll.h says that poll() returns 0 if timeout, -1 if error and
	 * +ve number for successful fds found
	 */
	if (ret == 0)
	{
		fprintf(stdout, "poll timeout, as expected upon ./uninstall.sh\n");
		return ret;
    }
	
	if (pfd.revents & POLLERR)
		return -1;
	else 
		return ret;
}


/* formerly read_data_file() in blktrace.c */
/* Used for both STDOUT display and for file output also */
static int read_data(struct thread_information *tip, void *buf, unsigned len)
{
#ifdef DEBUG_SS
        WHERE;
#endif
    int ret = 0;

    do {
        /* poll tip->fd for 100 units time to see for data to read 
		 * Repeated polling till there is data
		 */
        ret = wait_for_data(tip, 100);
		if (ret == -1)
		{
			fprintf(stderr, "Thread %d got error in polling, could be"
					 " due to overflow of buffers?\n", tip->cpu);
			break;
		}

		/* If timeout but done != 1, then continue reading */
		if (!ret && !is_producerdone())
		{
			continue;
		}

		/* This is when no data is found because done == 1, not before... */
		if (!ret && is_producerdone())
			return ret;
		
		assert(ret > 0);	
        ret = read(tip->ii.ifd, buf, len);

        /* if nothing was read though it was avail, continue reading */
        //if (!ret && !is_done())
        if (!ret)
            continue;
        else if (ret > 0)
            return ret;
        else {
            if (errno != EAGAIN && !is_producerdone()) {
                perror(tip->ii.fn);
                fprintf(stderr,"Thread %d failed read of %s\n",
                    tip->cpu, tip->ii.fn);
                break;
            }
            continue;
        }
    } while (1);
//    } while (!is_done());

    return ret;

}

static inline struct tip_subbuf *
subbuf_fifo_dequeue(struct thread_information *tip)
{
    const int head = tip->fifo.head;
    /* circular fifo */
    const int next = (head + 1) & (FIFO_SIZE - 1);

    /* if there is atleast one element in fifo, get that into ts
	 * Move fifo head one place ahead
 	 * Return ts (the first element of fifo)
 	 */
    if (head != tip->fifo.tail) {
        struct tip_subbuf *ts = tip->fifo.q[head];

        store_barrier();
        tip->fifo.head = next;
        return ts;
    }

    /* fifo is empty, so return NULL */
    return NULL;
}

static inline int subbuf_fifo_queue(struct thread_information *tip,
                    struct tip_subbuf *ts)
{
    const int tail = tip->fifo.tail;
    const int next = (tail + 1) & (FIFO_SIZE - 1);

    /* if there is at least 1 vacancy in fifo, add ts at tail position &
	 * move tail one step ahead
	 */
    if (next != tip->fifo.head) {
        tip->fifo.q[tail] = ts;
        store_barrier();
        tip->fifo.tail = next;
        return 0;
    }

    /* no vacancy in fifo */
    fprintf(stderr, "fifo too small!\n");
    return 1;
}

/*
 * Use the copy approach for pipes - output to stdout
 */
int get_subbuf(struct thread_information *tip, unsigned int maxlen)
{
    struct tip_subbuf *ts = malloc(sizeof(*ts));
    int ret;
#ifdef DEBUG_SS
        WHERE;
#endif

	if (is_producerdone())
	{
        free(ts);
		return -1;
	}

    /* allocate buffer */
    ts->buf = malloc(buf_size);
    ts->max_len = maxlen;

    /* read data from tip->fd into newly allocated buf of maxlen */
    ret = read_data(tip, ts->buf, ts->max_len);
    if (ret > 0) {
        ts->len = ret;
        tip->data_read += ret;
        /* add ts to tip->fifo queue tail */
        if (subbuf_fifo_queue(tip, ts))
            ret = -1;
    }
#ifdef DEBUG_SS
        WHERE;
#endif

    if (ret <= 0) {
        free(ts->buf);
        free(ts);
    }

    return ret;
}


/*
 * For file output, truncate and mmap the file appropriately 
 */
int mmap_subbuf(struct thread_information *tip, unsigned int maxlen)
{
    /* Get fd associated to the open file */
    int ofd = fileno(tip->ofile);
    int ret;
    unsigned long nr;
#ifdef DEBUG_SS
        WHERE;
#endif

    /*
     * extend file, if we have to. use chunks of 16 subbuffers.
     */
    if (tip->fs_off + maxlen > tip->fs_buf_len) 
	{
#ifdef DEBUG_SS
        TRACE("tip->fs_off + maxlen > tip->fs_buf_len\n");
#endif
        if (tip->fs_buf) 
		{
        TRACE("tip->buf not NULL => munlock/munmap start\n");
            munlock(tip->fs_buf, tip->fs_buf_len);	/* unlock to allow swap */
            munmap(tip->fs_buf, tip->fs_buf_len);	/* releases memory mapping of fs_buf */
        TRACE("tip->buf not NULL => munlock/munmap done\n");
            tip->fs_buf = NULL;
        }

/*
		if (is_done())
			return -1;
*/

        tip->fs_off = tip->fs_size & (page_size - 1);
        nr = max(H_BUF_NR, buf_nr);
        tip->fs_buf_len = (nr * buf_size) - tip->fs_off;
        tip->fs_max_size += tip->fs_buf_len;
#ifdef DEBUG_SS
        TRACE("%u, tip->fs_size = %llu\n", __LINE__, tip->fs_size);
        TRACE("tip->fs_off = %lu\n", tip->fs_off);
        TRACE("tip->fs_buf_len = %lu\n", tip->fs_buf_len);
        TRACE("tip->fs_max_size = %llu\n", tip->fs_max_size);
#endif

tryagain:
        /* ftruncate() for truncating file that is open for writing,
		 * else could have used truncate() for a closed writable file */
        TRACE("ftruncate start\n");
        if (ftruncate(ofd, tip->fs_max_size) < 0) {
            perror("ftruncate");
			switch(errno)
			{
				case EFBIG: 
						fprintf(stderr, "length is greater than"
								" offset maximum\n");
						break;
				case EINVAL: fprintf(stderr, "not open for writing\n");
						break;
				case EINTR: fprintf(stderr, "signal caught during execution\n");
						goto tryagain;
				default: fprintf(stderr, "some error during ftruncate\n");
			}
            return -1;
        }
        TRACE("ftruncate end\n");
#ifdef DEBUG_SS
        WHERE;
#endif

        TRACE("mmap start\n");
        /* map such that data can be written, write changes are shared */
        tip->fs_buf = mmap(NULL, tip->fs_buf_len, PROT_WRITE,
                   MAP_SHARED, ofd, tip->fs_size - tip->fs_off);
        if (tip->fs_buf == MAP_FAILED) {
            perror("mmap");
            return -1;
        }
        TRACE("mmap end\n");
        TRACE("mlock start\n");
        /* lock the mmap'ed area into virtual address space of process 
		 * so that it can not be swapped out by kernel */
        mlock(tip->fs_buf, tip->fs_buf_len);
        TRACE("mlock end\n");
    }

    TRACE("read_data start\n");
	/* Read data maxlen bytes from tip->fd into specified buf  */
    ret = read_data(tip, tip->fs_buf + tip->fs_off, maxlen);
    /* Increment byte counters accordingly */
    if (ret >= 0) {
        tip->data_read += ret;
        tip->fs_size += ret;
        tip->fs_off += ret;
    	TRACE("read_data end\n");
        return ret;
    }
#ifdef DEBUG_SS
        WHERE;
#endif

   	TRACE("read_data failed\n");
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
#ifdef DEBUG_SS
        WHERE;
#endif
}

static void tip_ftrunc_final(struct thread_information *tip)
{
    /*
     * truncate to right size and cleanup mmap - when writing to files
     */
    if (tip->ofile_mmap && tip->ofile) {
        int ofd = fileno(tip->ofile);	/* Get fd for output file stream */

        if (tip->fs_buf)
            munmap(tip->fs_buf, tip->fs_buf_len);	 /* unmap the buf */

        /* ftruncate the output file according to its size */
        if (ftruncate(ofd, tip->fs_size) < 0)
		{
            perror("ftruncate in tip_ftrunc_final");
        }
    }
}

static int get_dropped_count(struct base_information *bip)
{
    int fd;
    char tmp[MAXPATHLEN + 64];

    snprintf(tmp, sizeof(tmp), "%s/pdatadumpdir/%s/dropped",
            debugfs_path, bip->path);
    fd = open(tmp, O_RDONLY);
    if (fd < 0) {
        /*
         * this may be ok, if the kernel doesn't support dropped counts
         */
        if (errno == ENOENT)
            return 0;

        fprintf(stderr, "Couldn't open dropped file %s\n", tmp);
        return -1;
    }

    /* SSS: file exists, read from file into tmp buffer */
    if (read(fd, tmp, sizeof(tmp)) < 0) {
        perror(tmp);
        close(fd);
        return -1;
    }

    close(fd);

    /* SSS: convert from alpha to integer and return */
    return atoi(tmp);
}

void get_drop_counts(void)
{
	struct base_information *bip;
    int i;

	for_each_bip(bip, i)
	{
		bip->drop_count = get_dropped_count(bip);
    	if ((int)bip->drop_count < 0)
			fprintf(stderr, "Error in accessing drop counts for %ith bip\n", i);
		else if (bip->drop_count)
	        fprintf(stderr, "You have dropped events, consider using a larger buffer size (-b) for %ith bip\n", i);
		else
			fprintf(stdout, "No drops in %ith bip, hurrah!\n", i);
	}
}

static void stop_threads(struct base_information *bip)
{
    struct thread_information *tip;
    unsigned long ret;
    int i;
#ifdef DEBUG_SS
        WHERE;
#endif

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
#ifdef DEBUG_SS
        WHERE;
#endif

	for_each_bip(bip, i)
	{
        stop_threads(bip);
	}
}

static void exit_siphon()
{
#ifdef DEBUG_SS
        WHERE;
#endif
    if (!is_siphon_stopped()) {
        siphon_stopped = 1;
        stop_all_threads();
		if (base_information)
			free(base_information);
    }
	
	write_0_and_exit();
}

/* same as exit_siphon, except that it forwards status value */
static void stop_siphon(int status)
{
#ifdef DEBUG_SS
        WHERE;
#endif
    if (!is_siphon_stopped()) {
        siphon_stopped = 1;
        stop_all_threads();
		if (base_information)
    		free(base_information);
    }

	write_0_and_exit();
    exit(status);
}

#if 0
/* Return 0 for success */
static int tip_open_input(struct thread_information *tip)
{
    int inotfd = inotify_init();
	char localname[25];
#ifdef DEBUG_SS
        WHERE;
#endif
    /* construct input filename since we have to read debugfs */
    sprintf(tip->ii.fn, "%s/%s%d",
            debugfs_path, tip->ii.base_name, tip->cpu);
	sprintf(localname, "%s%d", tip->ii.base_name, tip->cpu);
#ifdef DEBUG_SS
    fprintf(stdout, "filename = %s\n", tip->ii.fn);
#endif
    fprintf(stdout, "psiphon waiting on filename = %s\n", tip->ii.fn);

    int watch_desc = inotify_add_watch(inotfd, debugfs_path, 
						IN_CREATE);
	if (watch_desc == -1)
	{
		fprintf(stderr, "watch_desc == -1\n");
		return -1;
	}
	size_t bufsiz = sizeof(struct inotify_event) + PATH_MAX + 1;
	struct inotify_event* event = malloc(bufsiz);
#if 0
    struct pollfd pifd = { .fd = inotfd, .events = POLLIN };
    if (poll(&pifd, 1, -1) < 0)
    {
        perror("poll timeout/error");
        return -1;
    }

    if (!(pifd.revents & POLLIN))
    {
        fprintf(stderr, "POLLIN event not found in inotify on %s\n",
                    tip->ii.fn);
        return -1;
    }
#endif 

repeatinotify:
	/* wait for an event to occur */
	read(inotfd, event, bufsiz);
	if (strcmp(event->name, localname))
		goto repeatinotify;

	inotify_rm_watch(inotfd, watch_desc);
    fprintf(stdout, "psiphon opening filename = %s\n", tip->ii.fn);
    //inotify_rm_watch(watch_desc);
    tip->ii.ifd = open(tip->ii.fn, O_RDONLY);   /* open file read-only */
    if (tip->ii.ifd < 0) {
        perror(tip->ii.fn);
        fprintf(stderr,"Thread %d failed open of %s\n", tip->cpu,
            tip->ii.fn);
        return 1;
    }
    return 0;
}
#endif

/* This is started as a thread from start_threads() */
static void *thread_main(void *arg)
{
	int ret = 0;
    struct thread_information *tip = arg;
    //pid_t pid = getpid();		/* get thread's own pid */
	pthread_t thread;
    thread = pthread_self();
    cpu_set_t cpu_mask;

    CPU_ZERO(&cpu_mask);
    CPU_SET((tip->cpu), &cpu_mask);

#ifdef DEBUG_SS
	fprintf(stdout, "Setting thread=%lu to cpu=%d\n", thread, tip->cpu);
#endif
	if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpu_mask))
	{
		perror("pthread_setaffinity_np");
		stop_siphon(1);
	}

    tip->ii.ifd = open(tip->ii.fn, O_RDONLY);   /* open file read-only */
    if (tip->ii.ifd < 0) {
        perror(tip->ii.fn);
        fprintf(stderr,"Thread %d failed open of %s\n", tip->cpu,
            tip->ii.fn);
        return NULL;
    }
#if 0
#ifdef DEBUG_SS
        WHERE;
	fprintf(stdout, "Setting pid=%u to cpu=%d\n", pid, tip->cpu);
#endif

    /* setting cpu affinity for thread, j loop in start_threads() */
    if (sched_setaffinity(pid, sizeof(cpu_mask), &cpu_mask) == -1) {
        perror("sched_setaffinity");
        stop_siphon(1);
    }

    /* open input file */
    if (tip_open_input(tip))
	    return NULL;
#endif

#ifdef DEBUG_SS
        WHERE;
#endif

    /* continuously get data while trace not finito */
	while ((ret = tip->get_subbuf(tip, buf_size)) >= 0)
	{
		if (!ret && is_producerdone())
			break;
	}
#ifdef DEBUG_SS
        WHERE;
#endif

#if 0
    while (!is_done()) {
        if (tip->get_subbuf(tip, buf_size) < 0)
            break;
    }
#endif

/* In blktrace, the tracing stops and then the event collection stops.
 * So, it is guaranteed there that the event streaming below will stop
 * at some point, hence it makes sense to wait till events = 0. Similarly,
 * in our case, kernel module sends a signal (44) to indicate that it
 * has stopped writing to relay channels and is ready to exit, here
 * we set producerdone = 1. We need to pull data until exhausted.
 */
    /*
     * trace is stopped, pull data until we get a short read
     */
	/* Need to go back and free any malloc'ed buffers based on "done" flag */
    while (tip->get_subbuf(tip, buf_size) > 0)
	{
		fprintf(stderr, "pull data until we get a short read\n");
    }

    /* ftruncate tip->ofile according to size, when writing to files */
    tip_ftrunc_final(tip);
#ifdef DEBUG_SS
        WHERE;
#endif
    tip->exited = 1;
    return NULL;
}

static int write_data(struct thread_information *tip, void *buf, 
					unsigned int buf_len)
{
    int ret;
#ifdef DEBUG_SS
        WHERE;
#endif

    if (!buf_len)
        return 0;
#ifdef DEBUG_SS
        WHERE;
#endif

    /* write databuf to output file stream - first arg is src & last is dest */
    ret = fwrite(buf, buf_len, 1, tip->ofile);
    if (ferror(tip->ofile) || ret != 1) {
        perror("write_data fwrite");
        clearerr(tip->ofile);
        return 1;
    }
#ifdef DEBUG_SS
        WHERE;
#endif

	/* if output is to stdout, then flush it */
	if (tip->ofile_stdout)
	{
#ifdef DEBUG_SS
        WHERE;
#endif
		fflush(tip->ofile);
	}

    return 0;
}


/* Formerly flush_subbuf_file() in blktrace.c */
/* Used for both STDOUT display and for file output also */
static int flush_subbuf(struct thread_information *tip,
                 struct tip_subbuf *ts)
{
    unsigned int offset = 0;
    int events = 0;
	struct trace_event_element *t;
    __u32 elt_len = 0;  /* length of data in next trace element */
#ifdef DEBUG_SS
        WHERE;
#endif

    /*
     * surplus from last run
     */
    if (tip->leftover_ts) {
#ifdef DEBUG_SS
        WHERE;
#endif
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

    while (offset + sizeof(struct trace_event_element) <= ts->len) {
#ifdef DEBUG_SS
        WHERE;
#endif
        t = ts->buf + offset;

        if (verify_trace(t->magic)) 
		{
			fprintf(stderr, "verify_trace failed\n");
            write_data(tip, ts->buf, offset);
            return -1;
        }
		fprintf(stderr, "verify_trace passed\n");

		elt_len = t->elt_len;

        if (offset + sizeof(struct trace_event_element) + elt_len > ts->len)
            break;

        offset += sizeof(struct trace_event_element) + elt_len;
        tip->events_processed++;
        tip->data_read += sizeof(struct trace_event_element) + elt_len;
        events++;
    }

    if (write_data(tip, ts->buf, offset))
	{
		fprintf(stderr, "write_data failed\n");
        return -1;
	}
#ifdef DEBUG_SS
        WHERE;
#endif

    /*
     * leftover bytes, save them for next time
     */
    if (offset != ts->len) {
#ifdef DEBUG_SS
        WHERE;
#endif
        tip->leftover_ts = ts;
        ts->len -= offset;
        memmove(ts->buf, ts->buf + offset, ts->len);
    } else {
#ifdef DEBUG_SS
        WHERE;
#endif
        free(ts->buf);
        free(ts);
    }

    return events;
}

static int write_tip_events(struct thread_information *tip)
{
#ifdef DEBUG_SS
        WHERE;
#endif
    /* get the first ts from tip->fifo */
    struct tip_subbuf *ts = subbuf_fifo_dequeue(tip);
#ifdef DEBUG_SS
        WHERE;
#endif

    if (ts)
	{
#ifdef DEBUG_SS
        WHERE;
#endif
        return flush_subbuf(tip, ts);
	}

    return 0;
}

/*
 *  * scans the tips we know and writes out the subbuffers we accumulate
 *   */
void get_and_write_events(void)
{
    struct base_information *bip;
    struct thread_information *tip;
    int i, j, events, ret;//, tips_running;
#ifdef DEBUG_SS
        WHERE;
#endif

    while (!is_producerdone()) {
        events = 0;

        for_each_bip(bip, i) {
            for_each_tip(bip, tip, j) {
                ret = write_tip_events(tip);
                if (ret > 0)
                    events += ret;
            }
        }

        /* if no events in all of tips in all bips, then sleep 100 ms */
        if (!events)
            usleep(100000);
    }
#ifdef DEBUG_SS
        WHERE;
#endif
#if 0
/* In blktrace, the tracing stops and then the event collection stops.
 * So, it is guaranteed there that the event streaming below will stop
 * at some point, hence it makes sense to wait till events = 0
 * But in our case, the kernel module (pdatadumo) is still loaded/working
 * while we start & stop event collection (psiphon). Thus, no guarantee
 * of events = 0, we just arbitrarily need to stop whenever user requests.
 * Thus, the follow loop could be infinite in our case, hence commented.
 */
    /*
	 * reap stored events
	 */
    do {
        events = 0;
        tips_running = 0;
        for_each_bip(bip, i) {
            for_each_tip(bip, tip, j) {
                ret = write_tip_events(tip);
                if (ret > 0)
                    events += ret;
                /* for every thread not exited, increment count */
                tips_running += !tip->exited;
            }
        }
        usleep(10);
    } while (events || tips_running);
    /* repeat above loop until events present and not all threads exited */
#endif
}


static void wait_for_threads(void)
{
	if (output_name && !strcmp(output_name, "-"))
	{
#ifdef DEBUG_SS
        WHERE;
#endif
		get_and_write_events();		// for stdout
	}
	else	//for file output
	{
    	/*
     	 * we just wait around for siphon threads to exit
     	 */
       	struct thread_information *tip;
        struct base_information *bip;
        int i, j, tips_running;
#ifdef DEBUG_SS
        WHERE;
#endif

        do 
		{
            tips_running = 0;
            usleep(100000);

            for_each_bip(bip, i)
				for_each_tip(bip, tip, j)
                    tips_running += !tip->exited;
        } while (tips_running);
	}
}

static int fill_ofname(struct thread_information *tip, char *dst,
               char *host_name)
{
    struct stat sb;
    int len = 0;
#ifdef DEBUG_SS
        WHERE;
#endif

    if (output_dir == NULL)
	{
		fprintf(stderr, "output_dir should not be NULL\n");
		return 1;
	}
    len = sprintf(dst, "%s/", output_dir);

    /* check whether dst exists */
    if (stat(dst, &sb) < 0) {
            perror("stat dst");
            return 1;
    }

    /* form output filename */
    //sprintf(dst + len, "%s.%s%d", host_name, tip->ii.base_name, tip->cpu);
    sprintf(dst + len, "%s.%s.%s.%d", host_name, tip->ii.base_name, progname, tip->cpu);

    return 0;
}

/* Return 0 for success */
static int tip_open_output(struct thread_information *tip)
{
#ifdef DEBUG_SS
        WHERE;
#endif
	int pipeline = output_name && !strcmp(output_name, "-");	/* Indicates stdout */
    int mode, vbuf_size;
    char op[128];

	if (pipeline)
	{
#ifdef DEBUG_SS
        WHERE;
#endif
		strcpy(op, "stdoutfile");
		tip->ofile = fdopen(STDOUT_FILENO, "w");
		tip->ofile_stdout = 1;
		tip->ofile_mmap = 0;
		tip->get_subbuf = get_subbuf;
	    mode = _IOLBF;					/* set line buffering mode for setvbuf() below */
    	vbuf_size = STDOUT_BUF;		/* 4KB */
	}
	else	//output files
	{
#ifdef DEBUG_SS
        WHERE;
#endif
	    struct stat st;

		/* Get the host name */
		host_name = malloc(HOSTNAME_LEN);
		if (gethostname(host_name, HOSTNAME_LEN))
		{
			perror("gethostnamme");
			return 1;
		}	
		if (fill_ofname(tip, op, host_name))/* fill output file name into op */
		{
			perror("fill_ofname");
	    	return 1;
		}
		free(host_name); 
		if (recreate_output)
		    tip->ofile = fopen(op, "w+");		/* open output file in write mode*/
	    else
		{
			tip->ofile = fopen(op, "a+");       /* open output file in append mode*/
	    	if (stat(op, &st) < 0) 
			{
				perror("File open was not successful. Check file ownership, if NFS");
				return 1;
			}
			else
			{
//				tip->ofile = fopen(op, "a+");       /* open output file in append mode*/
				tip->fs_size = st.st_size;
				tip->fs_max_size = st.st_size;
			}
#ifdef DEBUG_SS
	        TRACE("tip->fs_size = %llu\n", tip->fs_size);
#endif
	        tip->fs_off = tip->fs_size & (page_size - 1);
#ifdef DEBUG_SS
    	    TRACE("tip->fs_off = %lu\n", tip->fs_off);
#endif
		}
		tip->ofile_stdout = 0;
	    tip->ofile_mmap = 1;			/* output file needs to be mmap'ed */
		tip->get_subbuf = mmap_subbuf;
//	    mode = _IOLBF;					/* is _IOFBF in blktrace, but we use _IOLBF */
	    mode = _IOFBF;
	    vbuf_size = OFILE_BUF;		/* 128KB*/
	}

    /* error if output file could not be opened for writing */
    if (tip->ofile == NULL) {
        perror(op);
        return 1;
    }
#ifdef DEBUG_SS
        WHERE;
#endif

    /* Malloc buffer of 128KB for output file */
    tip->ofile_buffer = malloc(vbuf_size);
    if (setvbuf(tip->ofile, tip->ofile_buffer, mode, vbuf_size)) {
        perror("setvbuf");
        close_thread(tip);
        return 1;
    }
#ifdef DEBUG_SS
        WHERE;
#endif

    return 0;
}

struct thread_information* gettip_by_localname(char *localname)
{
	struct base_information *bip = NULL;
	struct thread_information *tip = NULL;
	int i, j;

	for_each_bip(bip, i)
	{
		for_each_tip(bip, tip, j)
    	{
			if (!strcmp(tip->ii.localname, localname))
			{
				fprintf(stdout, "File %s found\n", localname);
				return tip;
			}
		}
	}
	fprintf(stdout, "File %s not found\n", localname);
	return NULL;
}

static int threadparm_init(struct base_information *bip)
{
    struct thread_information *tip;
    int j;
#ifdef DEBUG_SS
        WHERE;
#endif

    /* iterating over tips */
    for_each_tip(bip, tip, j)
	{
        /* there is one thread per cpu */
        tip->cpu = j;
        tip->ii.ifd = -1;
        tip->ii.base_name = strdup(bip->path);
        sprintf(tip->ii.localname, "%s%d", tip->ii.base_name, tip->cpu);
    	sprintf(tip->ii.fn, "%s/%s%d", debugfs_path, 
							tip->ii.base_name, tip->cpu);

        /* open output file */
        if (tip_open_output(tip))
            return -1;
    }
	return 0;
}


static int start_siphon(void)
{
#if 0
	int len, inot_iter = 0;
	char inotbuf[NOTIFY_BUF_LEN];
	int inotfd;
    size_t bufsiz = sizeof(struct inotify_event) + PATH_MAX + 1;
    struct inotify_event* event = malloc(bufsiz);
#endif 

    struct base_information *bip;
	struct thread_information *tip;
    int i, j, size;
#ifdef DEBUG_SS
        WHERE;
#endif

	if (write_pid_to_debugfs())
	{
		fprintf(stderr, "Error in write_pid_to_debugfs\n");
		return -1;
	}

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
		if (threadparm_init(bip))
		{
			fprintf(stderr, "Failed to initialize thread parameters\n");
			break;
		}
	}
#ifdef DEBUG_SS
        WHERE;
#endif
#if 0
	inotfd = inotify_init();
	if (inotfd < 0)
	{
		perror("inotify_init");
		return -1;
	}
	int watch_desc = inotify_add_watch(inotfd, debugfs_path, IN_CREATE);
    if (watch_desc < 0)
    {
		perror("inotify_add_watch");
        fprintf(stderr, "watch_desc < 0\n");
        return -1;
    }
	fprintf(stdout, "psiphon waiting on debugfs directory...\n");
	i = 0;
	while (i < (nbases * ncpus))
    {
        len = read(inotfd, inotbuf, NOTIFY_BUF_LEN);
		if (len < 0)
		{
			if (errno == EINTR)
			{
				fprintf(stderr, "Need to reissue system call\n");
				fprintf(stderr, "read() was blocked, now exiting\n");
			}
			else
				perror("read");
			return -1;
		}
		inot_iter = 0;
		while(inot_iter < len)
		{
			struct inotify_event *event;
			event = (struct inotify_event*) &inotbuf[inot_iter];
			if (event->len)
			{
				tip = gettip_by_localname(event->name);
				if (tip)
				{
					i++;
				}
				else
					fprintf(stderr, "Couldnt find tip for file %s\n",
                            event->name);
			}
			inot_iter += NOTIFY_EVENT_SIZE + event->len;
		}
    }
	inotify_rm_watch(inotfd, watch_desc);

#endif
	for_each_bip(bip, i)
	{
		for_each_tip(bip, tip, j)
    	{
        	/* for every cpu thread, creating execution thread */
	        /* thread_information is starting addr of all thread str */
    	    if (pthread_create(&tip->thread, NULL, thread_main, tip)) {
        	    perror("pthread_create");
            	close_thread(tip);
            	break;
        	}
		}
	}
#ifdef DEBUG_SS
        WHERE;
#endif

    /* if atleast 1 thread failed, stop all threads & return error */
    if (i != nbases) 
	{
		fprintf(stderr, "Stopping all consumer threads because i(%d) != %d\n",
					i, nbases);
        __for_each_bip(bip, base_information, i, j)
            stop_threads(bip);
        return 1;
    }
#ifdef DEBUG_SS
        WHERE;
#endif
	return 0;
}

static int invalid_output_name(char *path)
{
#ifdef DEBUG_SS
        WHERE;
#endif
	if (!strcmp(path, "-") || !strcmp(path, "f"))
	{
		output_name = strdup(path);
		return 0;
	}
	return 1;
}

static int resize_bases(char *path)
{
    int size = (nbases + 1) * sizeof(struct base_information);
#ifdef DEBUG_SS
        WHERE;
#endif

    /* retains existing info but adjusts size */
    base_information = realloc(base_information, size);
    if (!base_information) {
        fprintf(stderr, "Out of memory, for basefile %s (%d)\n", path, size);
        return 1;
    }
    /* noting new input base path */
    base_information[nbases].path = strdup(path);
#ifdef DEBUG_SS
        WHERE;
#endif
    nbases++;
    return 0;
}

unsigned int get_major_for_devname(char *name)
{
    char s[32];
    char *p;
	int len;
  	int fd;
	char path[64];
	char buf[32];
	unsigned int maj, min;

    if (strncmp(name, "/dev/", 5) != 0) 
	{
		fprintf(stderr, "Provide a /dev/xxx pathname for -t option\n");
		return 0;
    }

    name += 5;

    if (strlen(name) > 31)
        goto fail;
    strcpy(s, name);
    for (p = s; *p; p++)
        if (*p == '/')
            *p = '!';

	/* Following code snippet adapted from try_name() of RHEL kernels */
	sprintf(path, "/sys/block/%s/dev", s);
	fd = open(path, 0, 0);
	if (fd < 0)
	    goto fail;
	len = read(fd, buf, 32);
	close(fd);
	if (len <= 0 || len == 32 || buf[len - 1] != '\n')
	    goto fail;
	buf[len - 1] = '\0';
	if (sscanf(buf, "%u:%u", &maj, &min) == 2) 
	{
		return maj;
	}

fail:
    return 0;
}


static char usage_str[] = \
    "-i <basefile> [ -r debugfspath ] [ -w time ] [ -D out_dir ] [ -v ] [ -k ]" \
	" [ -o <-/f> ] [ -t trace_dev ] \n\n" \
    "\t-i Base file to read from\n" \
    "\t-r Path to mounted debugfs, defaults to /sys/kernel/debug\n" \
    "\t-o Send output to stdout(-o -) or files (-o f)\n" \
    "\t-D Directory to prepend to output file names\n" \
    "\t-t Device being traced\n" \
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
    struct stat sb;
    int c;
    int stop_watch = 0;
	unsigned int omajor, tmajor;

#ifdef DEBUG_SS
        WHERE;
#endif

    while ((c = getopt_long(argc, argv, S_OPTS, l_opts, NULL)) >= 0) {
        switch (c) {
		case 'c':
			recreate_output = 1;
			break;
        case 'r':
#ifdef DEBUG_SS
        WHERE;
#endif
            debugfs_path = optarg;
            break;
        case 'o':
#ifdef DEBUG_SS
        WHERE;
#endif
			if (invalid_output_name(optarg))
			{
            	fprintf(stderr, "-o to be followed by \"-\" or \"f\"\n");
				return 1;
			}
            break;
        case 'i':
#ifdef DEBUG_SS
        WHERE;
#endif
            /* base name specified */
            if (resize_bases(optarg) != 0)
                return 1;
            break;
        case 'k':
#ifdef DEBUG_SS
        WHERE;
#endif
            /* used to kill the running siphon */
            kill_running_siphon = 1;
            break;
        case 'w':
#ifdef DEBUG_SS
        WHERE;
#endif
            stop_watch = atoi(optarg);
            if (stop_watch <= 0) {
                fprintf(stderr,
                    "Invalid stopwatch value (%d secs)\n",
                    stop_watch);
                return 1;
            }
            break;
        case 'v':
#ifdef DEBUG_SS
        WHERE;
#endif
            printf("%s version %s\n", argv[0], psiphon_version);
            return 0;
        case 'b':
#ifdef DEBUG_SS
        WHERE;
#endif
            buf_size = strtoul(optarg, NULL, 10);
            if (buf_size <= 0 || buf_size > 16*1024) {
                fprintf(stderr,
                    "Invalid buffer size (%lu)\n",buf_size);
                return 1;
            }
            buf_size <<= 10;
            break;
        case 'n':
#ifdef DEBUG_SS
        WHERE;
#endif
            buf_nr = strtoul(optarg, NULL, 10);
            if (buf_nr <= 0) {
                fprintf(stderr,
                    "Invalid buffer nr (%lu)\n", buf_nr);
                return 1;
            }
            break;
        case 'D':
#ifdef DEBUG_SS
        WHERE;
#endif
            output_dir = optarg;
            break;
        case 't':
#ifdef DEBUG_SS
        WHERE;
#endif
            trace_dev = optarg;
            break;
        default:
#ifdef DEBUG_SS
        WHERE;
#endif
            show_usage(argv[0]);
            return 1;
        }
    }

    page_size = getpagesize();
#ifdef DEBUG_SS
        WHERE;
#endif

    while (optind < argc) {
        if (resize_bases(argv[optind++]) != 0)
            return 1;
    }
#ifdef DEBUG_SS
        WHERE;
#endif

    if (nbases == 0) {
        show_usage(argv[0]);
        return 1;
    }
#ifdef DEBUG_SS
        WHERE;
#endif

    if (!strcmp(output_name, "f") && trace_dev == NULL)
    {
        fprintf(stderr, "Default output behaviour is to file (-o f) \n");
        fprintf(stderr, "Please provide trace device (-t trace_dev)\n");
        return 1;
    }

	if (!strcmp(output_name, "f") && output_dir == NULL)
	{
		fprintf(stderr, "Default output behaviour is to file (-o f) \n");
		fprintf(stderr, "Provide output directory (-D output_dir)\n");
		return 1;
	}

	if (!strcmp(output_name, "f") && output_dir != NULL)
	{
		if (!(tmajor = get_major_for_devname(trace_dev)))
			return 1;

#ifdef DEBUG_SS
		fprintf(stderr, "trace device %s has major = %u\n", trace_dev, tmajor);
#endif
	    /* check whether dst exists */
	    if (stat(output_dir, &sb) < 0) 
		{
    	    if (errno != ENOENT) 
			{
	            perror("stat");
    	       return 1;
        	}
	        /* try to make the dir if it doesnt exist */
	        if (mkdir(output_dir, 0755) < 0) 
			{
	            perror(output_dir);
            	fprintf(stderr, "Can't make output_dir\n");
            	return 1;
        	}
    	}

		/* Check to see that this path is not on the same disk that we are tracihg */
		stat(output_dir, &sb);
		omajor = MAJOR(sb.st_dev);
#ifdef DEBUG_SS
		fprintf(stderr, "output directory %s has major = %u\n", output_dir, omajor);
#endif
		if (omajor == tmajor)
		{
			fprintf(stderr, "[WARNING] Output directory %s is on the device being traced %s\n", 
						output_dir, trace_dev);
			fprintf(stderr, "Please provide output directory on another device\n");
			//return 1;
		}
	}

    if (!debugfs_path)
        debugfs_path = default_debugfs_path;

    if (statfs(debugfs_path, &st) < 0) {
#ifdef DEBUG_SS
        WHERE;
#endif
        perror("statfs debugfs");
        fprintf(stderr,"%s does not appear to be a valid path\n",
            debugfs_path);
        return 1;
    } else if (st.f_type != (long) DEBUGFS_TYPE) {
#ifdef DEBUG_SS
        WHERE;
#endif
        fprintf(stderr,"%s does not appear to be a debug filesystem\n",
            debugfs_path);
        return 1;
    }
#ifdef DEBUG_SS
        WHERE;
#endif

    /* if -k option specified, then stop siphoning */
    if (kill_running_siphon) {
        stop_siphon(0);
    }
#ifdef DEBUG_SS
        WHERE;
#endif

    ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpus < 0) {
        fprintf(stderr, "sysconf(_SC_NPROCESSORS_ONLN) failed\n");
        return 1;
    }
#ifdef DEBUG_SS
	fprintf(stdout, "\nNumber of cpus = %d\n", ncpus);
        WHERE;
#endif
    signal(SIGINT, handle_sigint);
    signal(SIGHUP, handle_sigint);
    signal(SIGTERM, handle_sigint);
    signal(SIGALRM, handle_sigint);
    signal(SIGPIPE, SIG_IGN);
#ifdef DEBUG_SS
        WHERE;
#endif

    /* start all siphoning threads */
    if (start_siphon() != 0)
        return 1;
#ifdef DEBUG_SS
        WHERE;
#endif

    /* registering to be called at normal exit */
    atexit(exit_siphon);
#ifdef DEBUG_SS
        WHERE;
#endif

    if (stop_watch)
        alarm(stop_watch);
#ifdef DEBUG_SS
        WHERE;
#endif

    wait_for_threads();
#ifdef DEBUG_SS
        WHERE;
#endif
	stop_siphon(0);
#ifdef DEBUG_SS
        WHERE;
#endif

	/* Will not reach this due to exit() in above stop_siphon() */
    return 0;
}


