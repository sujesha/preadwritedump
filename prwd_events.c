/* For all functions that write to relay channel */

#include <linux/kernel.h>   /* We're doing kernel work */
#include <linux/module.h>
#include <linux/delay.h>

#include <linux/debugfs.h>
#include <linux/relay.h>
#include <linux/version.h>
#include <linux/percpu.h>			/* per-cpu variables */
#include <linux/errno.h>
#include <linux/slab.h>
#include "relayframe.h"
#include "subbuf_conf.h"
#include "prwd_events.h"
#include "config.h"
#include "trace_struct.h"

#ifndef IOONLY
struct dentry *dir1 = NULL;
#endif
struct dentry *dir2 = NULL;
struct dentry *pddroot = NULL;

#if((LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)))
#ifndef IOONLY
struct pdd_trace *pt1 = NULL;
#endif
struct pdd_trace *pt2 = NULL;
#endif
long wrapptime = 0;
#if 0
long runningcount = 0;
#endif

/* Relay channel of scanning phase is different than for online phase */
#ifndef IOONLY
struct rchan *scanchan = NULL;	/* relay channel for scanning phase */
unsigned long long totalscanc = 0;
unsigned long long *scancount = NULL;
#endif	//IOONLY
struct rchan *iochan = NULL;     /* relay channel for read/write req */
static int relays_started_flag = 0;
unsigned long long totalioc = 0;
unsigned long long *iocount = NULL;

#if YESHASHTAB
#ifndef IOONLY
extern struct lblktab_t scantab;
#endif	//IOONLY
extern struct lblktab_t iotab;
#endif	//YESHASHTAB

#if 0
/* Invoked when event encountered in scandisk.c and iodisk.c */
//long long unsigned get_timestamp()
void get_timestamp(lblk_datum *ldatum)
{
	static s64 prevtime = 0;
#if 0
	struct timespec now;
	ktime_get_ts(&now);
	return ktime_to_ns(timespec_to_ktime(now));
#endif 
	ldatum->ptime = 1000 * 1000 * 1000 * ktime_to_ns(ktime_get());	///testing only!!!
    if (ldatum->ptime < prevtime)
    {
	wrapptime++;
    }
	prevtime = ldatum->ptime;
}

/* Invoked when event encountered in scandisk.c and iodisk.c */
//long long unsigned get_timestamp()
void get_timestamp(lblk_datum *ldatum)
{
    struct timespec now;
    ktime_get_ts(&now);
    ldatum->ptime = ktime_to_ns(timespec_to_ktime(now));
//  ldatum->ptime = ktime_to_ns(ktime_get());
}
#endif	//0

ktime_t myktime_get(void)
{
    struct timespec now;
    ktime_get_ts(&now);
    return timespec_to_ktime(now);
}

#ifndef IOONLY
/* Scandisk relay channel init routine (for scanning phase of pcollect) */
int scandisk_relaychan_init(void)
{
#if((LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)))
    int rc = relaychan_init("pscanevents", &scanchan,
		CURR_SUBBUF_SIZE_SCAN, CURR_N_SUBBUFS_SCAN, NULL);
#else
    int rc = relaychan_init("pscanevents", &scanchan,
		CURR_SUBBUF_SIZE_SCAN, CURR_N_SUBBUFS_SCAN, pt1);
#endif
    if (rc)
	{
		printk(KERN_ERR "PRWD:scanchan init failed\n");
		scanchan = NULL;
	}

	return rc;
}
#endif	//IOONLY

#ifndef IOONLY
/* Scandisk relay channel exit routine */
void scandisk_relaychan_exit()
{
	if (scanchan != NULL)
		relaychan_exit(&scanchan);
}
#endif	//IOONLY

/* Iodisk relay channel init routine (for online phase of pcollect) */
int iodisk_relaychan_init(void)
{
#if((LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)))
    int rc = relaychan_init("pioevents", &iochan,
		CURR_SUBBUF_SIZE_IO, CURR_N_SUBBUFS_IO, NULL);
#else
    int rc = relaychan_init("pioevents", &iochan,
		CURR_SUBBUF_SIZE_IO, CURR_N_SUBBUFS_IO, pt2);
#endif
    if (rc)
	{
		printk(KERN_ERR "PRWD:iochan init failed\n");
		iochan = NULL;
	}

	return rc;
}

/* Iodisk relay channel exit routine */
void iodisk_relaychan_exit()
{
	if (iochan != NULL)
		relaychan_exit(&iochan);
}

#if((LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)))
static int pdd_dropped_open(struct inode *inode, struct file *filp)
{
    filp->private_data = inode->i_private;

    return 0;
}

static ssize_t pdd_dropped_read(struct file *filp, char __user *buffer,
		size_t count, loff_t *ppos)
{
    struct pdd_trace *pt = filp->private_data;
    char buf[16];

    snprintf(buf, sizeof(buf), "%u\n", atomic_read(&pt->dropped));

    /* copy data from the buf to user space buffer */
    return simple_read_from_buffer(buffer, count, ppos, buf, strlen(buf));
}

static const struct file_operations pdd_dropped_fops = {
    .owner =    THIS_MODULE,
    .open =     pdd_dropped_open,
    .read =     pdd_dropped_read,
};

int do_pdd_trace_dropped_setup(void)
{
#ifndef IOONLY
	pt1 = kzalloc(sizeof(*pt1), GFP_KERNEL);
	if (!pt1)
	{
		printk(KERN_CRIT "PRWD:Couldn't kzalloc pt1 for dropped counts\n");
		return -1;
	}
#endif	//IOONLY
	pt2 = kzalloc(sizeof(*pt2), GFP_KERNEL);
	if (!pt2)
	{
		printk(KERN_CRIT "PRWD:Couldn't kzalloc pt2 for dropped counts\n");
		return -1;
	}

#ifndef IOONLY
	atomic_set(&pt1->dropped, 0);
#endif	//IOONLY
	atomic_set(&pt2->dropped, 0);

	pddroot = debugfs_create_dir("preadwritedumpdir", NULL);
	if (!pddroot)
	{
		printk(KERN_CRIT "PRWD:debugfs_create_dir for preadwritedumpdir failed\n");
		return -1;
	}

#ifndef IOONLY
	dir1 = debugfs_create_dir("pscanevents", pddroot);
	if (!dir1)
	{
		printk(KERN_CRIT "PRWD:debugfs_create_dir for pscanevents drops failed\n");
		return -1;
	}
#endif	//IOONLY

	dir2 = debugfs_create_dir("pioevents", pddroot);
	if (!dir2)
	{
		printk(KERN_CRIT "PRWD:debugfs_create_dir for pioevents drops failed\n");
		return -1;
	}

#ifndef IOONLY
	pt1->dropped_file = debugfs_create_file("dropped", 0444, dir1, pt1, 
				&pdd_dropped_fops);
	if (!pt1->dropped_file)
	{
		printk(KERN_CRIT "PRWD:debugfs_create_file failed for io dropped\n");
	return -1;
	}
#endif	//IOONLY

	pt2->dropped_file = debugfs_create_file("dropped", 0444, dir2, pt2, 
				&pdd_dropped_fops);
	if (!pt2->dropped_file)
	{
		printk(KERN_CRIT "PRWD:debugfs_create_file failed for scan dropped\n");
	return -1;
	}

	return 0;
}
#endif	//KERNEL_VERSION

int start_relays(void)
{
    int rc;

    if (relays_started_flag == 1)
    {
		rc = 0;
		return rc;
    }

#if((LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)))
    if (do_pdd_trace_dropped_setup())
		return -1;
#endif	//KERNEL_VERSION

	relays_started_flag = 1;
	
#ifndef IOONLY
    rc = scandisk_relaychan_init();
    if (rc)
    {
		scanchan = NULL;
		return rc;
    }
#endif	//IOONLY

    rc = iodisk_relaychan_init();
    if (rc)
    {
		iochan = NULL;
#ifndef IOONLY
		scandisk_relaychan_exit();
		scanchan = NULL;
#endif	//IOONLY
    }

    return rc;
}

void end_relays(void)
{
    if (!relays_started_flag)
		return;

#ifndef IOONLY
    if (scanchan != NULL) {
		scandisk_relaychan_exit();
    }
#endif	//IOONLY
    if (iochan != NULL) {
		iodisk_relaychan_exit();
    }

#if((LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)))
#ifndef IOONLY
    debugfs_remove(pt1->dropped_file);
#endif	//IOONLY
    debugfs_remove(pt2->dropped_file);
#ifndef IOONLY
    debugfs_remove(dir1);
#endif	//IOONLY
    debugfs_remove(dir2);
    debugfs_remove(pddroot);
#ifndef IOONLY
	kfree(pt1);
#endif	//IOONLY
	kfree(pt2);
#endif	//KERNEL_VERSION

    relays_started_flag = 0;
}

/* get memory */
unsigned char* alloc_kmem(int len)
{
    unsigned char *mem;
    mem = (unsigned char*) kzalloc(sizeof(unsigned char) * len, GFP_ATOMIC);
    // RAHUL: used inside atomic context, so use GFP_ATOMIC
    //mem = (unsigned char*) kzalloc(sizeof(unsigned char) * len, GFP_KERNEL);
    if(mem == NULL)
    {
	printk(KERN_ERR "PRWD:Couldn't allocate memory! Ignoring request.\n");
	return NULL;
    }
    return mem;
}

void free_mem(unsigned char *mem)
{
    if (mem != NULL)
	kfree(mem);
    mem = NULL;
}

void markMagicSample(unsigned char *buf, int len, unsigned char *mag)
{
	return;
}

void note_blocknode_attrs(lblk_datum *ldatum)
{
//	get_timestamp(ldatum);
    if (ldatum == NULL)
    {
	printk(KERN_CRIT "PRWD: ldatum is NULL in note_blocknode_attrs\n");
	return;
    }

    ldatum->chanoffset = 0;
    ldatum->cpunum = 0;

}

static inline int get_eventname(int traceaction, char *event)
{
	int i = 0;
	int s = traceaction & PRO_TC_ACT(PRO_TC_SCANDISK);
	int o = traceaction & PRO_TC_ACT(PRO_TC_IODISK);
	int dd = traceaction & PRO_TC_ACT(PRO_TC_IODUMP);
	int w = traceaction & PRO_TC_ACT(PRO_TC_WRITE);
	int r = traceaction & PRO_TC_ACT(PRO_TC_READ);
	int f = traceaction & PRO_TC_ACT(PRO_TC_FIXED);
	int v = traceaction & PRO_TC_ACT(PRO_TC_VARIABLE);
	int n = traceaction & PRO_TC_ACT(PRO_TC_NEW);
	int d = traceaction & PRO_TC_ACT(PRO_TC_DEDUP);
	int z = traceaction & PRO_TC_ACT(PRO_TC_ZERO);

	/* Only one of these at a time! */
	if (w && r)
		return -1;
	if (s && o)
		return -1;
	if (f && v)
		return -1;
	if (n && d)
		return -1;

	if (s)
		event[i++] = 'S';
	if (o)
		event[i++] = 'O';
	if (dd)
		event[i++] = 'D';
	if (f)
		event[i++] = 'F';
	if (v)
		event[i++] = 'V';
	if (n)
		event[i++] = 'N';
	if (d)
		event[i++] = 'D';
	if (w)
		event[i++] = 'W';
	if (r)
		event[i++] = 'R';
	if (z)
		event[i++] = 'Z';

	event[i] = '\0';
	return 0;
}

#if 0
DEFINE_PER_CPU(char[PRINT_HEADER], scandisk_buf);
int write_event_scan(struct trace_event_element t, char *relaybuf, __u32 size,
	char *data, u32 nbytes, int contentflag, int *g)
{
    unsigned long flags;
    struct rchan_buf *rbuf = NULL;

    /****************** BEGIN OF INTERRUPT CONTEXT ********************/
    local_irq_save(flags);      //disabling interrupts on this processor
    rbuf = scanchan->buf[(*g = get_cpu())]; //will preempt_disable
    printk(KERN_DEBUG "%c Relaychan[%u] produced = "PRI_SIZET", consumed = "PRI_SIZET"\n", 'S', *g, rbuf->subbufs_produced, rbuf->subbufs_consumed);
    put_cpu();		      //will preempt_enable

    memcpy(get_cpu_var(scandisk_buf), relaybuf, size);

    relay_write(scanchan, &t, sizeof(struct trace_event_element));
    relay_write(scanchan, get_cpu_var(scandisk_buf), size);
    if (contentflag)
	relay_write(scanchan, data, nbytes);
    relay_write(scanchan, "\n", 1);     /* new line per entry */

    put_cpu_var(scandisk_buf);
    local_irq_restore(flags);       //enabling interrupts on this processor
    /****************** END OF INTERRUPT CONTEXT ********************/

    return 0;
}
#endif

//DEFINE_PER_CPU(char[PRINT_HEADER], iodisk_buf);
int write_event_io(struct trace_event_element t, char *relaybuf, __u32 size,
	char *data, u32 nbytes, int contentflag, int *g)
{
    //tempunsigned long flags;
#ifdef DEBUG_SS
    struct rchan_buf *rbuf = NULL;
#endif
	unsigned char *tempbuf;
	void *p;
	unsigned int tsize = 0;

	/* Create the temp buf */
    if (contentflag)
	{
		tempbuf = kzalloc(sizeof(struct trace_event_element)+size+nbytes+1, 
				GFP_KERNEL);
	}
	else
	{
		tempbuf = kzalloc(sizeof(struct trace_event_element)+size+1, 
				GFP_KERNEL);
	}

	p = tempbuf;
	memcpy(p, &t, sizeof(struct trace_event_element));
	p += sizeof(struct trace_event_element);
	tsize += sizeof(struct trace_event_element);

	memcpy(p, relaybuf, size);
	p += size;
	tsize += size;

    if (contentflag)
	{
		memcpy(p, data, nbytes);
		p += nbytes;
		tsize += nbytes;
	}
	memcpy(p,"\n",1);		/* new line per entry */
	tsize += 1;

    /****************** BEGIN OF INTERRUPT CONTEXT ********************/
    //templocal_irq_save(flags);      //disabling interrupts on this processor
#ifdef DEBUG_SS
    rbuf = iochan->buf[(*g = get_cpu())];   //will preempt_disable
    printk(KERN_DEBUG "%c Relaychan[%u] produced = "PRI_SIZET", consumed = "PRI_SIZET"\n", 'O', *g, rbuf->subbufs_produced, rbuf->subbufs_consumed);
    put_cpu();		      //will preempt_enable
#endif

#if 0
    memcpy(get_cpu_var(iodisk_buf), relaybuf, size);
    relay_write(iochan, &t, sizeof(struct trace_event_element));
    relay_write(iochan, get_cpu_var(iodisk_buf), size);
    if (contentflag)
		relay_write(iochan, data, nbytes);
    relay_write(iochan, "\n", 1);     /* new line per entry */
    put_cpu_var(iodisk_buf);
#endif

	relay_write(iochan, tempbuf, tsize);

    //templocal_irq_restore(flags);       //enabling interrupts on this processor
    /****************** END OF INTERRUPT CONTEXT ********************/

	kfree(tempbuf);				//to avoid memory leak

    return 0;
}

#ifndef IOONLY
int reserve_event_scan(struct trace_event_element t, char *relaybuf,__u32 size,
	char *data, u32 nbytes, int contentflag, int *g, size_t total_size)
{
    unsigned long flags;
    struct rchan_buf *rbuf = NULL;
	void *p, *reserved = NULL;
	//int i;

    /****************** BEGIN OF INTERRUPT CONTEXT ********************/
    local_irq_save(flags);      //disabling interrupts on this processor

    /* p points to the reserved area of "total_size" # of bytes */
	*g = smp_processor_id();
    reserved = p = relay_reserve(scanchan, total_size);

	if (!reserved)
	{
	    local_irq_restore(flags);       //enabling interrupts on this processor
		return RELAY_TRY_AGAIN;	//meaningful only to scanning, not I/O
	}
	
    rbuf = scanchan->buf[*g];   //will preempt_disable
#ifdef DEBUG_SS
    printk(KERN_DEBUG "%c Relaychan[%u] produced = " PRI_SIZET ", consumed = " PRI_SIZET"\n", 'S', *g, rbuf->subbufs_produced, rbuf->subbufs_consumed);
#endif
	rbuf->offset -= total_size;	
    __relay_write(scanchan, &t, sizeof(struct trace_event_element));
    __relay_write(scanchan, relaybuf, size);
    if (contentflag)
		__relay_write(scanchan, data, nbytes);
    __relay_write(scanchan, "\n", 1);     /* new line per entry */

    local_irq_restore(flags);       //enabling interrupts on this processor
	/****************** END OF INTERRUPT CONTEXT ********************/

	return 0;
}
#endif	//IOONLY

int reserve_event_io(struct trace_event_element t, char *relaybuf, __u32 size,
	char *data, u32 nbytes, int contentflag, int *g, size_t total_size)
{
    //tempunsigned long flags;
    struct rchan_buf *rbuf = NULL;
	void *p, *reserved = NULL;
	//int i;

    /****************** BEGIN OF INTERRUPT CONTEXT ********************/
    //templocal_irq_save(flags);      //disabling interrupts on this processor
	/*  If you know you cannot be preempted by another task (ie. you are in 
	 * interrupt context, or have preemption disabled) you can use 
	 * smp_processor_id().
	 */
	*g = smp_processor_id();
    reserved = p = relay_reserve(iochan, total_size);

 //   memcpy(get_cpu_var(iodisk_buf), relaybuf, size);

    /* p points to the reserved area of "total_size" # of bytes */
	if (!reserved)
	{
		printk(KERN_DEBUG "PRWD: I/O event stalled due to full buffer\n");
	    //templocal_irq_restore(flags);       //enabling interrupts on this processor
		return RELAY_TRY_AGAIN;
	}

    rbuf = iochan->buf[*g];

#ifdef DEBUG_SS
    printk(KERN_DEBUG "%c Relaychan[%u] produced = "PRI_SIZET", consumed = "PRI_SIZET"\n", 'O', *g, rbuf->subbufs_produced, rbuf->subbufs_consumed);
#endif
    //put_cpu();		      //will preempt_enable
	rbuf->offset -= total_size;	
    __relay_write(iochan, &t, sizeof(struct trace_event_element));
    __relay_write(iochan, relaybuf, size);
    if (contentflag)
		__relay_write(iochan, data, nbytes);
    __relay_write(iochan, "\n", 1);     /* new line per entry */

#if 0
	/* if reservation successful, i.e. no buffer overflow */
	*((struct trace_event_element *)p) = t;
	p += sizeof(struct trace_event_element);

	/* Copy relaybuf of "size" # of bytes */
	for (i=0; i<size; i++)
		*(char *)p = relaybuf[i];
	p += size;

	/* If block content also to be written ... */
	if (contentflag)
	{
	    /* ... copy the data contents of "nbytes" # of bytes */
		for (i=0; i<nbytes; i++)
			*(char *)p = data[i];
	    p += nbytes;
	}

	/* Whether there is content or not, we need to finish with '\n' */
	*(char *)p = '\n';

	//commit(rbuf, reserved, total_size); 

    //put_cpu_var(iodisk_buf);
#endif
    //templocal_irq_restore(flags);       //enabling interrupts on this processor
	/****************** END OF INTERRUPT CONTEXT ********************/

	return 0;
}

#if 0
static inline void *my_relay_reserve(struct rchan *chan, size_t length)
{
	void *reserved;
	struct rchan_buf *buf = chan->buf[smp_processor_id()];

	printk(KERN_DEBUG "PRWD: Trying my_reserve (%lu)\n", length);
	if (unlikely(buf->offset + length > buf->chan->subbuf_size)) {
		printk(KERN_DEBUG "PRWD: Trying to relay_switch_subbuf\n");
        length = relay_switch_subbuf(buf, length);
		printk(KERN_DEBUG "PRWD: Done with relay_switch_subbuf\n");
        if (!length)
		{
			printk(KERN_DEBUG "PRWD: my_reserve has NULL\n");
			return NULL;
		}
	}
	reserved = buf->data + buf->offset;
//	buf->offset += length;	dont update the offset

	return reserved;
}


int my_reserve_event_io(struct trace_event_element t, char *relaybuf,__u32 size,
	char *data, u32 nbytes, int contentflag, int *g, size_t total_size)
{
    unsigned long flags;
	void *p, *reserved = NULL;
	//int i;

    /****************** BEGIN OF INTERRUPT CONTEXT ********************/
    local_irq_save(flags);      //disabling interrupts on this processor
	*g = smp_processor_id();
    reserved = p = my_relay_reserve(iochan, total_size);

 //   memcpy(get_cpu_var(iodisk_buf), relaybuf, size);

    /* p points to the reserved area of "total_size" # of bytes */
	if (!reserved)
	{
		printk(KERN_DEBUG "PRWD: I/O event dropped due to full buffer\n");
	    local_irq_restore(flags);       //enabling interrupts on this processor
		return -1;
	}

    __relay_write(iochan, &t, sizeof(struct trace_event_element));
    __relay_write(iochan, relaybuf, size);
    if (contentflag)
		__relay_write(iochan, data, nbytes);
    __relay_write(iochan, "\n", 1);     /* new line per entry */

    local_irq_restore(flags);       //enabling interrupts on this processor
	/****************** END OF INTERRUPT CONTEXT ********************/

	return 0;
}
#endif

int write_event(int traceaction, struct block_node *ldatum,
    unsigned char *data, sector_t blockID, u32 nbytes,
    struct process_node *pnode,
    unsigned long long dupoffset, unsigned char dupcpu, s64 ptime)
{
	char event[5];
	char dig_str[33];	//just a dummy place-holder in format now
	int rc = 0;
	int size;
	unsigned char relaybuf[PRINT_HEADER];
    struct rchan_buf *rbuf = NULL;
	int contentflag = 0;
#if YESHASHTAB
	char dupid[80] = "";
#endif
	__u32 elt_len;
    struct trace_event_element temp;
	size_t total_size = 0;
	unsigned int g;

	/*  If you know you cannot be preempted by another task (ie. you are in 
	 * interrupt context, or have preemption disabled) you can use 
	 * smp_processor_id().
	 */
	g = smp_processor_id();

#if NOHASHTAB
	strcpy(dig_str, "dummymd5");	//initialize dont care
#elif YESHASHTAB
	dig_str[0]='\0';	//initialize
#else
	printk(KERN_DEBUG "PRWD: at least one of NOHASHTAB & YESHASHTAB!\n");
	return -1;
#endif
    if (get_eventname(traceaction, event))
		printk(KERN_CRIT "PRWD: Event type wrong/mssing %d\n", traceaction);

#if 0
	/* Since preadwritedump has different paths for logging reads and writes,
	 * so the processname can not be relied upon for read requests. 
	 * We move this check to my_generic_request() instead.
	 * Prof. Raju Rangaswamy had also confirmed this case of read requests.
	 * This is because although my_generic_request() is launched by
	 * concerned process in most cases, the read request callback can be
	 * received by interrupting any other process as well, so current 
	 * will have name of that interrupted process.
	 */
    if ((event[0] != 'S' && strstr(pnode->processname, "kjournald")) ||
      (event[0] != 'S' && strstr(pnode->processname, "scandisk_prog")) ||
      (event[0] != 'S' && strstr(pnode->processname, "printk")) ||
      (event[0] != 'S' && strstr(pnode->processname, "klogd")))
    {
		/* We want to ignore disk writes caused due to kjournald
		 * writing its journaling logs and also disk reads that
		 * may be caused due to scanning phase. We also ignore disk
		 * writes caused by the kernel logging/debugging process.
		 */
		return 0;
    }
#endif

    if (traceaction!=PRO_TA_SZ && ldatum == NULL)
    {
		printk(KERN_CRIT "PRWD: why ldatum NULL ?\n");
		return -1;
    }

    /* Setting variables which dont need interrupt context */
    switch (traceaction)
    {
		case PRO_TA_SFN:    /* scanning phase new block */
#if YESHASHTAB
		    MD5Human(ldatum->bhashkey, dig_str);
#endif
		    contentflag = 1;
		    size = (__u32)snprintf(relaybuf,
		    PRINT_HEADER,
		    "%s %lld %s " PRI_SECT " %u %s %d ",
		    event,
#ifdef DEBUG_SS
		    (cts = ktime_to_ns(myktime_get())),
#else
		    ptime,
#endif
		    xstringify(HOSTNAME), /* distinguish different VMs */
		    blockID,
		    nbytes,
		    dig_str,
		    contentflag
		    );
		    if (size >= PRINT_HEADER)
			{
				printk(KERN_ERR "PRWD:SFN: relaybuf truncated to PRINT_HEADER, \
				instead of %u\n", size);
		    	rc = -1;
			}
		    break;

#if YESHASHTAB
		case PRO_TA_SFD:    /* scanning phase duplicate block */
		    MD5Human(ldatum->bhashkey, dig_str);
		    sprintf(dupid, "%u:%llu", dupcpu, dupoffset);
		    contentflag = 0;
		    size = (__u32)snprintf(relaybuf,
			    PRINT_BUF,
			    "%s %lld %s " PRI_SECT " %u %s %d %s",
			    event,
#ifdef DEBUG_SS
			    (cts = ktime_to_ns(myktime_get())),
#else
			    ptime,
#endif
			    xstringify(HOSTNAME), /* distinguish different VMs */
			    blockID,
		    	nbytes,
			    dig_str,
			    contentflag,
			    dupid
			    );
	    	if (size >= PRINT_HEADER)
			{
				printk(KERN_ERR "PRWD:SFD: relaybuf truncated to PRINT_HEADER, \
				instead of %u\n", size);
	    		rc = -1;
			}
		    break;
#endif

	case PRO_TA_SZ:     /* scanning phase zero block */
		    contentflag = 0;
		    size = (__u32)snprintf(relaybuf,
			    PRINT_BUF,
			    "%s %lld %s " PRI_SECT " %u %s",
			    event,
#ifdef DEBUG_SS
			    (cts = ktime_to_ns(myktime_get())),
#else
			    ptime,
#endif
			    xstringify(HOSTNAME), /* distinguish different VMs */
			    blockID,
			    nbytes,
			    "zero"
			    );
		    if (size >= PRINT_HEADER)
			printk(KERN_ERR "PRWD:SZ: relaybuf truncated to PRINT_HEADER, \
					instead of %u\n", size);
		    rc = -1;
		    break;

	case PRO_TA_OFR:    /* online phase read request */
		    contentflag = 0;
		    size = (__u32)snprintf(relaybuf,
			    PRINT_BUF,
			    "%s %lld %s " PRI_SECT " %u %s %u %u %u",
			    event,
#ifdef DEBUG_SS
			    (cts = ktime_to_ns(myktime_get())),
#else
			    ptime,
#endif
			    xstringify(HOSTNAME), /* distinguish different VMs */
			    blockID,
			    nbytes,
			    pnode->processname,
			    pnode->pid,
			    pnode->major,
			    pnode->minor
			    );
		    if (size >= PRINT_HEADER)
			{
				printk(KERN_ERR "PRWD:OFR: relaybuf truncated to PRINT_HEADER, \
				instead of %u\n", size);
		    	rc = -1;
			}
		    break;

	case PRO_TA_DFR:    /* online phase read request */
		    contentflag = 1;
		    size = (__u32)snprintf(relaybuf,
			    PRINT_BUF,
			    "%s %lld %s " PRI_SECT " %u %s %u %u %u ",
			    event,
#ifdef DEBUG_SS
			    (cts = ktime_to_ns(myktime_get())),
#else
			    ptime,
#endif
			    xstringify(HOSTNAME), /* distinguish different VMs */
			    blockID,
			    nbytes,
			    pnode->processname,
			    pnode->pid,
			    pnode->major,
			    pnode->minor
			    );
		    if (size >= PRINT_HEADER)
			{
				printk(KERN_ERR "PRWD:DFR: relaybuf truncated to PRINT_HEADER, \
				instead of %u\n", size);
		    	rc = -1;
			}
		    break;

		case PRO_TA_OFNW:   /* online phase write - new block */
#if YESHASHTAB
		    MD5Human(ldatum->bhashkey, dig_str);
#endif
		    contentflag = 1;
		    size = (__u32)snprintf(relaybuf,
			    PRINT_BUF,
			    "%s %lld %s " PRI_SECT " %u %s %d %s %u %u %u ",
			    event,
#ifdef DEBUG_SS
			    (cts = ktime_to_ns(myktime_get())),
#else
			    ptime,
#endif
			    xstringify(HOSTNAME), /* distinguish different VMs */
			    blockID,
			    nbytes,
			    dig_str,
			    contentflag,
			    pnode->processname,
			    pnode->pid,
			    pnode->major,
			    pnode->minor
			    );
		    if (size >= PRINT_HEADER)
			{
				printk(KERN_ERR "PRWD:OFNW: relaybuf truncated to PRINT_HEADER, \
				instead of %u\n", size);
		    	rc = -1;
			}
		    break;

#if YESHASHTAB
	case PRO_TA_OFDW:   /* online phase write - dedup block */
		    MD5Human(ldatum->bhashkey, dig_str);
		    sprintf(dupid, "%u:%llu", dupcpu, dupoffset);
		    contentflag = 0;
		    size = snprintf(relaybuf,
			    PRINT_BUF,
			    "%s %lld %s " PRI_SECT " %u %s %d %s %u %u %u %s",
			    event,
#ifdef DEBUG_SS
			    (cts = ktime_to_ns(myktime_get())),
#else
			    ptime,
#endif
			    xstringify(HOSTNAME), /* distinguish different VMs */
			    blockID,
			    nbytes,
			    dig_str,
			    contentflag,
			    pnode->processname,
			    pnode->pid,
			    pnode->major,
			    pnode->minor,
			    dupid
			    );
		    if (size >= PRINT_HEADER)
			{
				printk(KERN_ERR "PRWD:OFDW: relaybuf truncated to PRINT_HEADER, \
				instead of %u\n", size);
		    	rc = -1;
			}
		    break;
#endif

		default:
		    printk(KERN_CRIT "PRWD: Unknown TA type\n");
		    return -1;
    }

#ifdef DEBUG_SS
    if (cts < pts)
	printk(KERN_CRIT "PRWD:TS order Error pts=%lld, cts=%lld\n", pts, cts);
    pts = cts;

    printk(KERN_DEBUG "PRWD:relaybuf = %s\n", relaybuf);
#endif

    if (contentflag)
		elt_len = size + nbytes + 1;
    else
		elt_len = size + 1;

    /* total_size should include the '\n' character */
    total_size = elt_len + sizeof(struct trace_event_element);

    temp.magic = (__u32) ENDIAN_MAGIC | ENDIAN_VERSION;
    temp.elt_len = elt_len;

	/* For debugging the dropped events issue */
   	rbuf = iochan->buf[g];
#if 0
	/* Useless, misses to print when continuous writes by flush */
	if (strstr(pnode->processname, "flush-8:0"))
		runningcount += total_size;
	else
	{
		if (runningcount != 0)
		{
			printk(KERN_DEBUG "last burst by flush was total %u bytes\n", 
				(unsigned int)total_size);
			runningcount = 0;
		}
	}
    if (event[0] == 'O' && 
		//strstr(pnode->processname, "flush-8:0") &&
		total_size > 524370)
		//(int)((rbuf->chan->n_subbufs - (rbuf->subbufs_produced - rbuf->subbufs_consumed)))<2)
    {
		printk(KERN_DEBUG "%s Relaychan[%u] sub-buffers left = %d , \
			offset = %lu , neededbytes= %u , process=%s\n", 
			event, g,
			(int)((rbuf->chan->n_subbufs - (rbuf->subbufs_produced - rbuf->subbufs_consumed))),
			rbuf->offset,
			(unsigned int)total_size, pnode->processname);
    }
#endif

#ifndef IOONLY
    if (traceaction & PRO_TC_ACT(PRO_TC_SCANDISK))
    {
		rc = reserve_event_scan(temp, relaybuf, size, data, nbytes,
			contentflag, &g, total_size);
		while (rc == RELAY_TRY_AGAIN)
		{
			msleep_interruptible(1000);	//wait a sec and try again
			printk(KERN_DEBUG "PRWD: scanning, wait a sec and try again" PRI_SECT"\n", blockID);
			rc = reserve_event_scan(temp, relaybuf, size, data, nbytes,
				contentflag, &g, total_size);
		}
    }
    else
    {
		rc = write_event_io(temp, relaybuf, size, data, nbytes,
			contentflag, &g);
    }
#else
#if 1
{
	rc = reserve_event_io(temp, relaybuf, size, data, nbytes,
			contentflag, &g, total_size);
	if (rc == RELAY_TRY_AGAIN)
	{
		printk(KERN_DEBUG "Drop event silently since reserve didnt work\n");
		rc = -1;	//drop event silently
	}
	/* msleep() can not be used since we are in interrupt context, no
	 * alternative but to just silently drop the event.
	 * Allowing the write to proceed at this point will result in overwriting
	 * the relay-channel which messes up the log file format.
	int numtrials=0;
	while (rc == RELAY_TRY_AGAIN && numtrials < 5)
	{
		msleep_interruptible(1);	//wait 1 msec and try again
		rc = reserve_event_io(temp, relaybuf, size, data, nbytes,
			contentflag, &g, total_size);
		numtrials++;
	}
	if (numtrials == 5)
		printk(KERN_CRIT "PRWD: reserve_event_io failed after 5 tries\n");
	 */
}
#else
{
	/* To ensure that events dont get dropped. If events are dropped,
	 * it basically means that data has been over-written in the relay
	 * channel buffers. This results in ill-formed traces, which can
	 * not be parse successfully later.
	 */
	if ((int)((rbuf->chan->n_subbufs - (rbuf->subbufs_produced - rbuf->subbufs_consumed)))>0)
	{
		rc = write_event_io(temp, relaybuf, size, data, nbytes,
			contentflag, &g);
	}
	else 
	{
		printk(KERN_DEBUG "Event dropped silently\n");
		rc = -1;	//silently drop event
	}
}
#endif
#endif

	if (!rc) //no error
    {
#ifndef IOONLY
		if (traceaction & PRO_TC_ACT(PRO_TC_SCANDISK))
		{
		    if (contentflag)
		    {
				ldatum->chanoffset = scancount[g] + (sizeof(struct trace_event_element) + size);
				ldatum->cpunum = g;
		    }
		    scancount[g] += (elt_len + sizeof(struct trace_event_element));
		    totalscanc += (elt_len + sizeof(struct trace_event_element));
		}
		else
		{
		    if (contentflag)
		    {
				ldatum->chanoffset = iocount[g] + (sizeof(struct trace_event_element)+size);
				ldatum->cpunum = g;
		    }
		    iocount[g] += (elt_len + sizeof(struct trace_event_element));
		    totalioc += (elt_len + sizeof(struct trace_event_element));
		}
#else
	    if (contentflag)
	    {
			ldatum->chanoffset = iocount[g] + (sizeof(struct trace_event_element)+size);
			ldatum->cpunum = g;
	    }
	    iocount[g] += (elt_len + sizeof(struct trace_event_element));
	    totalioc += (elt_len + sizeof(struct trace_event_element));
#endif
    }

#ifdef DEBUG_SS
    printk(KERN_DEBUG "%c Relaychan[%u] produced = "PRI_SIZET", consumed = "PRI_SIZET"\n", event[0], g, rbuf->subbufs_produced, rbuf->subbufs_consumed);
    if (event[0] == 'O')
    {
    	rbuf = iochan->buf[g];   //will preempt_disable
		printk(KERN_DEBUG "%c Relaychan[%u] sub-buffers left = %d, \
			offset = %lu\n", event[0], g,
			(int)((rbuf->chan->n_subbufs - (rbuf->subbufs_produced - rbuf->subbufs_consumed))),
			rbuf->offset);
    }
#endif //DEBUG_SS

	/* Even if we silently drop above, here we return 0 */
	if (rc == -1)
		rc = 0;
    return rc;
}

#ifndef IOONLY
/* The below stashing of new block into hashtable is only
 * for purpose of reducing the length of output logs. So,
 * if once in a rare while, if there is a race condition
 * which causes the insert to fail due to another thread
 * having just inserted it, it is not a huge problem.
 * Hence, -EEXIST error is not considered very grave below.
 */
int stash_new_block(unsigned char* buf, sector_t blockID)
{
	int stat;
	lblk_datum *ldatum = NULL;
	s64 ptime;
#if YESHASHTAB
	int rc;
	u32 outindex;   
	unsigned long flags;
	lblk_datum *dupldatum = NULL;
	unsigned long long dupoffset = 0;
	unsigned char dupcpu = 0;
#endif

	ldatum = (lblk_datum*)kzalloc(sizeof(lblk_datum), GFP_KERNEL);
	if (ldatum == NULL)
	{
		printk(KERN_CRIT "PRWD:kzalloc failed for GFP_KERNEL ldatum in scanning\n");
		return -1;
	}

#if YESHASHTAB
	/* Hash(+magic) the block */
	getHashKey(buf, BLKSIZE, ldatum->bhashkey);
#ifdef DEBUG_SS
    printk(KERN_DEBUG "PRWD:getHashKey() done: buf = %s, key = %s, blockID = %llu\n",
			buf, ldatum->bhashkey, blockID);
#endif
#endif

	note_blocknode_attrs(ldatum);

#ifdef DEBUG_SS
	printk(KERN_DEBUG "PRWD:note_blocknode_attrs() done.\n");
#endif

#if YESHASHTAB
	dupldatum = (lblk_datum*) hashtab_search(scantab.table,
					ldatum->bhashkey, &outindex, &flags);
#ifdef DEBUG_SS
	printk(KERN_DEBUG "PRWD:hashtab_search() done.\n");
#endif
	if (dupldatum)	/* This is a duplicate block */
	{
#ifdef DEBUG_SS
		printk(KERN_DEBUG "PRWD:Found duplicate block in lblktab with timestamp %llu\n", dupldatum->ptime);
#endif
		dupoffset = dupldatum->chanoffset;
		dupcpu = dupldatum->cpunum;
		read_unlock_irqrestore(&scantab.table->hashlock[outindex], flags);

		ptime = ktime_to_ns(myktime_get());
		stat = write_event(PRO_TA_SFD, ldatum, buf, blockID, BLKSIZE, 
				NULL, dupoffset, dupcpu, ptime);
		if (stat)
			printk(KERN_DEBUG "PRWD:Error in write_event: stash_new_block1\n");
		kfree(ldatum); //potential memory leak otherwise
	}
	else		/* This is a new block */
	{
#ifdef DEBUG_SS
		printk(KERN_DEBUG "PRWD:New block for lblktab.\n");
#endif
#endif	//YESHASHTAB

		ptime = ktime_to_ns(myktime_get());
		stat = write_event(PRO_TA_SFN, ldatum, buf, blockID, BLKSIZE, NULL,
				0, 0, ptime);
		if (stat)
		{
			printk(KERN_DEBUG "PRWD:Error in write_event: stash_new_block2\n");
			return stat;
		}
#if NOHASHTAB
		/* If this flag is enabled, we want to not use any hash-tables 
		 * for scanning and I/O trace collection. The point is that if
		 * no entries are made to the hash-table, then the above 
		 * hashtab search will always return NULL, hence effectively unused hashtab
		 * Since this is a temporary arrangement, doing it this way instead of
		 * adding #ifdef NOHASHTAB around all hashtab size calculation, 
		 * num buckets calculation, hashtab creation & initialization, etc.
		 */
		kfree(ldatum);
		ldatum = NULL;
		return stat;
#elif YESHASHTAB

		/* Insert into hashtab only if above write_event was success */
		rc = hashtab_insert(scantab.table, ldatum->bhashkey, ldatum);
		if (rc == -EEXIST)
		    printk(KERN_DEBUG "PRWD:Race condition for new block stash\n");
		else if (rc)
		    printk(KERN_ERR "PRWD:Error while inserting block in hash-table\n");

		/* ldatum has to be left un-freed if and only if it was inserted into
		 * hash-table, free it otherwise
		 */
		if (rc)
	    	kfree(ldatum);
	}

	ldatum = NULL;
	return stat;
#else
	assert(0);	//either YESHASHTAB or NOHASHTAB has to be true!
#endif
}
#endif //IOONLY

