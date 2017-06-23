

#ifndef IOONLY
/*
* Scan disk for provided project - dump all content, not just hash
* Sujesha Sudevalayam
* 
* This file only implements the scandisk functionality.
*/

#include <linux/kernel.h>	/* We're doing kernel work */
#include <linux/module.h>       /* module_init, module_exit */
#include <linux/hash.h>         /* hash_ptr */
#include <linux/list.h>     /* hlist_node, hlist_head */
#include <linux/blkdev.h>       /* request_queue_t, Sector */
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/debugfs.h>
#include <linux/relay.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/percpu.h>
#include <linux/types.h>		/* sector_t */
#include <linux/root_dev.h>
#include <linux/delay.h>

#include <linux/sched.h>	/* Put ourselves to sleep and wake up later */
#include <linux/init.h>		/* For __init and __exit */
#include <linux/interrupt.h>	/* For irqreturn_t */

#ifndef IOONLY
#include <linux/kthread.h>
#include "pdd_scandisk.h"
#endif
#ifdef PREADWRITEDUMP
	#include "prwd_iodisk.h"
	#include "prwd_events.h"			/* PRI_SECT */
#else
	#include "pdd_iodisk.h"
	#include "pdd_events.h"			/* PRI_SECT */
	#include "md5.h"
	#include "pdd_lblktab.h"
#endif
#include "config.h"


#if 0
#if((LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)))
  #include <linux/workqueue.h>  /* We schedule tasks here */ /* http://tldp.org/LDP/lkmpg/2.6/html/lkmpg.html#AEN1209 */
#endif
#endif

#ifndef IOONLY
struct task_struct *kthreadp = NULL;
atomic_t scanning = ATOMIC_INIT(0);	/* set to 1 while scanning */
DECLARE_WAIT_QUEUE_HEAD(scanning_waitq);
#endif

#if 0
/* http://www.linuxquestions.org/questions/linux-newbie-8/lkmpg-linux-kernel-module-programming-guide-and-me-help-724648/ */
#if ((LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)))
    struct workqueue_struct* my_workqueue;
#else
    DECLARE_DELAYED_WORK(scandisk_work, (void*)scandisk_routine);
#endif
#endif


static const char zeroarray[65537] = { 0 };
int die = 0;		/* set this to 1 for shutdown */

extern unsigned int gmajor;
extern unsigned int gminor;
extern char devname[];
extern dev_t dev;
extern struct rchan *scanchan;	/* relay channel for scanning phase */

#if YESHASHTAB
extern struct lblktab_t scantab;
#endif
extern unsigned long long totalscanc;

/* return 0 if success */
int get_start_end_lba(struct block_device *bdev, sector_t *start, sector_t *end)
{
	struct hd_struct *part = NULL;

	if (bdev)
		part = bdev->bd_part;

	if (part)
	{
		*start = part->start_sect;
		*end = part->start_sect + part->nr_sects;
        printk(KERN_DEBUG "PDD:bdev->bd_part is !NULL, so this is partition.\n");
	}
	else
	{
        *start = 0;
        *end = *start + get_capacity(bdev->bd_disk);
        printk(KERN_DEBUG "PDD:bdev->bd_part is NULL, so this is parent disk.\n");
	}
	return 0;

}

/* Return 1 if true */
int is_parentdisk(struct block_device *bdev)
{
	/* Doesnt matter if it is parent disk or not, so return 1 */
	//TODO: check what it means if bdev->bd_part == bdev & 
	//		say whether this is parent, eg, /dev/sda
	return 1;
}

int _read_block(struct block_device *bdev, sector_t start, unsigned int numsect, unsigned char *buf)
{
	int bytes_read = 0;
	unsigned char *p;
	unsigned int blkoff;
	Sector sect;
	void *data;

    p = buf;
    for (blkoff=0; blkoff<numsect; blkoff++)
    {
        /* Every lba is a sector */
        data = read_dev_sector(bdev, start+blkoff, &sect);
        memcpy(p, data, SECT_LEN);
        put_dev_sector(sect);	/* To release page cache page */
        p += SECT_LEN;
    }
    bytes_read = BLKSIZE;

    return bytes_read;
}

#ifdef IOONLY
/* stub for testing */
int scan_and_process(dev_t *dev)
{
	return 0;
}

#else
/* return 0 for success */
int scan_and_process(dev_t *dev)
{	
	s64 ptime;
	int rc = 0;
	struct block_device *bdev;
	sector_t start_lba, end_lba, iter, blockID = 0;
	unsigned char *buf;
	int bytes_read;
	unsigned int numsect = BLKSIZE/SECT_LEN;

    // Initialize the disk parameters for scan.
	bdev = bdget(*dev);
	if (!bdev)
	{
		printk(KERN_CRIT "PDD:bdget(dev) returned NULL");
        return -1;
   	}
	if (unlikely((get_start_end_lba(bdev, &start_lba, &end_lba))))
	{
		printk(KERN_CRIT "PDD:get_start_end_lba() unsuccessful. Exiting\n");
	    bdput(bdev);
		return -1;
	}
	if (is_parentdisk(bdev) && start_lba != 0)
	{
		printk(KERN_CRIT "PDD:start_lba != 0. Exiting\n");
	    bdput(bdev);
		return -1;
	}
    
    buf = (unsigned char *)kzalloc(BLKSIZE, GFP_KERNEL);
    if (unlikely(buf==NULL)) {
		printk(KERN_CRIT "PDD:No memory to allocate buffer for reading blocks.\n");
	    bdput(bdev);
        return -ENOMEM;
    }

	printk(KERN_NOTICE "PDD:start_lba = " PRI_SECT ", end_lba = " PRI_SECT".\n", start_lba, end_lba);

    /* Every lba is a sector */
    //end_lba = start_lba + 0x0101UL;		/* For TEST only: Comment it for normal use */
	//end_lba = start_lba + 2097152UL; /* For TEST only: Comment it for normal use */
	//end_lba = start_lba + 1048576UL; /* For TEST only: Comment it for normal use */
   	for (iter = start_lba; iter < end_lba; iter += numsect)
	{
		//if ((iter & 0x000F) == 0)   //16 sectors (~8KB of data)
		//if ((iter & 0x00FF) == 0)   //256 sectors (~128KB of data)
		if ((iter & 0x07FF) == 0)   //2K sectors (~1MB of data)
		{
#ifdef DEBUG_SS
	        printk(KERN_DEBUG "PDD:checking scan status at lba (iter) = " PRI_SECT".\n", iter);
#endif
			if (die == 1)
			{
				printk(KERN_NOTICE "PDD:die = 1 within the scan loop, so break out.\n");
				if (bdev)
					bdput(bdev);
				return 0;
			}
            // Release the block device
	        //bdput(bdev);
            // Sleep for 1/10 sec after processing 2K sectors
//			set_current_state(TASK_INTERRUPTIBLE);
//			schedule_timeout(HZ);
            // Get the block device again
            //bdev = bdget(*dev);
			msleep_interruptible(1000);
            while (!bdev)
            {
                printk(KERN_WARNING "PDD: bdget returned NULL. Wait and retry.");
                //set_current_state(TASK_INTERRUPTIBLE);
                //schedule_timeout(HZ/10);
				msleep_interruptible(1000);
                bdev = bdget(*dev);
            }
		}
        // Read the next block
        bytes_read = _read_block(bdev, iter, numsect, buf);

		/* Check for zero block */
		if (memcmp(buf, zeroarray, bytes_read) == 0)
        {
			ptime = ktime_to_ns(myktime_get());
			rc = write_event(PRO_TA_SZ, NULL, NULL, blockID, BLKSIZE, NULL,
					0, 0, ptime);
#ifdef DEBUG_SS
	       	printk(KERN_DEBUG "PDD:Zero-block.\n");
#endif
			if (rc)
			{
				printk(KERN_ERR "PDD:write_event() failed for zero block\n");
				break;
			}
		}
		else
		{
#ifdef DEBUG_SS
	       	printk(KERN_DEBUG "PDD:Non-zero block.\n");
#endif
			rc = stash_new_block(buf, blockID);
			if (rc)
			{
        		printk(KERN_ERR "PDD:stash_new_block failed, rc = %d.\n", rc);
				break;
			}
		}
		blockID++;
	}
    // Scan is complete. Finally release the block device
    if (bdev)
		bdput(bdev);
	kfree(buf);	//potential memory leak otherwise

	return rc;
}
#endif

#ifdef IOONLY
/* stub for testing 
 * Return 0 to indicate success 
 */
int scandisk_routine(void *arg)
{
    /* Since this thread may or may not have exited by the time we wish to
     * rmmod the kernel module, hence we would like to keep it uniform
     * and force the thread to wait until pdatadump_exit() invokes
     * kthread_stop(). So, change the state of the thread from "RUNNING"
     * to "TASK_INTERRUPTIBLE" and keep waiting in a loop as long as 
     * kthread_stop() is not called.
     */
    set_current_state(TASK_INTERRUPTIBLE);
    while(!kthread_should_stop())
    {
        schedule();
        set_current_state(TASK_INTERRUPTIBLE);
    }
    set_current_state(TASK_RUNNING);
    return rc;
}
#endif

/* 
 *  This function can be called on every timer interrupt.
 *  But we just call it once, to trigger diskscan after module init is done
 *  http://tldp.org/LDP/lkmpg/2.6/html/lkmpg.html#AEN1209 tells how to setup
 *
 * Return 0 to indicate success 
 */
int scandisk_routine(void *arg)
{
	int rc = 0;
	char countstr[20];
	//struct work_struct *work = arg;

	/* scanning = 1 here to indicate scanning in progress */
	atomic_inc(&scanning);

	rc = scan_and_process(&dev);
	if (rc)
	{
		printk(KERN_CRIT "PDD:scan_and_process() returned error\n");
	}
    /* Scanning done. Set scanning = 0 and wake-up other threads 
	 * waiting for scanning to complete.
	 */
	if (die != 1 && !rc)
		printk(KERN_INFO "PDD:Scanning complete\n");
    if (atomic_read(&scanning)==1)
	{
		atomic_dec(&scanning);
		wake_up(&scanning_waitq);
	}
	die = 1;	/* setting to 1 to indicate that scandisk has exited */

    /* Flush once scanning is finished */
	printk(KERN_INFO "PDD:Gonna relay_flush\n");
	relay_flush(scanchan);

	/* Write out the bytecounts to scanning relay channel.	 */
	sprintf(countstr, "%llu\n", totalscanc);
	totalscanc += strlen(countstr);
	relay_write(scanchan, countstr, strlen(countstr));

#if YESHASHTAB
	/* Delete scanning hash-table */
    scantab_exit();
#endif

	printk(KERN_INFO "PDD:Done with scandisk_routine(), " \
					"waiting around for kthread_should_stop\n");

	/* Since this thread may or may not have exited by the time we wish to
	 * rmmod the kernel module, hence we would like to keep it uniform
	 * and force the thread to wait until pdatadump_exit() invokes
	 * kthread_stop(). So, change the state of the thread from "RUNNING"
	 * to "TASK_INTERRUPTIBLE" and keep waiting in a loop as long as 
	 * kthread_stop() is not called.
	 */
	set_current_state(TASK_INTERRUPTIBLE);
	while(!kthread_should_stop())
	{
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);
	return rc;
}

#endif
