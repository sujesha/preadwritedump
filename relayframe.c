/* This file has the generic routines for relay setup */

#include <linux/module.h>       /* module_init, module_exit */
#include <linux/kprobes.h>      /* jprobe, register_jprobe, unregister_jprobe */
#include <linux/hash.h>         /* hash_ptr */
#include <linux/list.h>     /* hlist_node, hlist_head */
#include <linux/syscalls.h> /* sys_open, sys_read, sys_write */
#include <linux/blkdev.h>       /* request_queue_t, bio */
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/debugfs.h>
#include <linux/relay.h>
#include <linux/relay.h>
#include <linux/version.h>
#include <linux/percpu.h>
#include "relayframe.h"

int nomore_hashtab_insert = 0;

/*
 * Keep track of how many times we encountered a full subbuffer, to aid
 * the user space app in telling how many lost events there were.
 */
/* Reused from blk_subbuf_start_callback of blktrace */
static int scan_subbuf_start_callback(struct rchan_buf *buf, void *subbuf,
        void *prev_subbuf, size_t prev_padding)
{
#if((LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)))
    struct pdd_trace *pt;
#endif

    if (!relay_buf_full(buf))
        return 1;

#if((LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)))
    pt = buf->chan->private_data;
    atomic_inc(&pt->dropped);
#endif

    return 0;
}

/*
 * Keep track of how many times we encountered a full subbuffer, to aid
 * the user space app in telling how many lost events there were.
 */
/* Reused from blk_subbuf_start_callback of blktrace */
static int io_subbuf_start_callback(struct rchan_buf *buf, void *subbuf,
        void *prev_subbuf, size_t prev_padding)
{
#if((LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)))
	struct pdd_trace *pt;
#endif

    if (!relay_buf_full(buf))
        return 1;

#if NOHASHTAB
	printk(KERN_CRIT "PDD: Dropped event, but NOHASHTAB anyway!\n");
#else
	if (nomore_hashtab_insert == 0)
		printk(KERN_CRIT "PDD:Dropped event, toggle nomore_hashtab_insert\n");
	nomore_hashtab_insert = 1;
#endif

#if((LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)))
	pt = buf->chan->private_data;
	atomic_inc(&pt->dropped);
#endif

    return 0;
}

/* Reused from blk_remove_buf_file_callback of blktrace */
static int pro_remove_buf_file_callback(struct dentry *dentry)
{
    debugfs_remove(dentry);
    return 0;
}

#if((LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)))
static struct dentry * pro_create_buf_file_callback(const char *filename,
        struct dentry *parent,
        umode_t mode,
        struct rchan_buf *buf,
        int *is_global)
#else
static struct dentry * pro_create_buf_file_callback(const char *filename,
        struct dentry *parent,
        int mode,
        struct rchan_buf *buf,
        int *is_global)
#endif
{
	/* Reused from blk_create_buf_file_callback of blktrace */
    return debugfs_create_file(filename, mode, parent, buf,
            &relay_file_operations);
}

/* Reused from blk_relay_callbacks{} of blktrace */
static struct rchan_callbacks scan_callbacks = {
    .subbuf_start       = scan_subbuf_start_callback,
    .create_buf_file    = pro_create_buf_file_callback,
    .remove_buf_file    = pro_remove_buf_file_callback,
};

/* Reused from blk_relay_callbacks{} of blktrace */
static struct rchan_callbacks io_callbacks = {
    .subbuf_start       = io_subbuf_start_callback,
    .create_buf_file    = pro_create_buf_file_callback,
    .remove_buf_file    = pro_remove_buf_file_callback,
};

/* Generic relay channel init routine */
int relaychan_init(char *eventsfile, struct rchan **c, 
	size_t subbuf_size, size_t n_subbufs, void *private_data)
{
	struct rchan *chan;

#if((LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)))
	printk(KERN_CRIT "PDD:relay channels was introduced in kernel 2.6.17.\n");
	printk(KERN_CRIT "PDD:Please upgrade kernel to use relay channels\n");
	return -1;
#endif

    /* opening relay to files file0, file1,.. in root dir */
#if((LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)))
	if (strstr(eventsfile, "scan"))
	    chan = relay_open(eventsfile, NULL, subbuf_size, n_subbufs,
            &scan_callbacks);
	else
	    chan = relay_open(eventsfile, NULL, subbuf_size, n_subbufs,
            &io_callbacks);
#else
	if (strstr(eventsfile, "scan"))
    	chan = relay_open(eventsfile, NULL, subbuf_size, n_subbufs,
            &scan_callbacks, private_data);
	else
    	chan = relay_open(eventsfile, NULL, subbuf_size, n_subbufs,
            &io_callbacks, private_data);
#endif
    if (!chan)
    {
        printk("PDD:%s channel creation failed\n", eventsfile);
		*c = chan;
        return -1;
    }
    else
    {
        printk("PDD:%s channel ready\n", eventsfile);
		*c = chan;
        return 0;
    }
}

/* Generic relay channel exit routine for both scanchan & iochan */
void relaychan_exit(struct rchan **c) //, struct pro_trace *pt)
{
	struct rchan *chan;

	chan = *c;

	if (chan != NULL)
	{
    	relay_flush(chan);
	    relay_close(chan);
	}
}


