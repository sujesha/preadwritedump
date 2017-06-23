/*
* Module registration/deregistration for content-dumping provided project
* Sujesha Sudevalayam
* 21 Sep - 2 Oct 2012 
* 
* Brief of provided project - preadwritedump.ko module
* 
*       - An install script used to load this module, passes diskname argument
*       - At init, sets up an interrupt for scandisk, and probes for iodisk
*       - At interrupt, scandisk_routine sets scanning=1 and starts 
*           scanning sector-by-sector, chunking, writing events to relay
*       - At probe, for read request, print blockID & timestamp to trace
*       - At probe, for write request, retrieve bio data and print to trace
*       - At module exit, die set to 1 -- signaled to scandisk_routine which
*           stops scanning and sets scanning=0. module_exit waits for scanning=0
*           Also, waits for pending I/O events to complete before exiting
*       - scandisk events logged to "scanevents" file and iodisk events logged
*           to "ioevents" file
*       - Every recorded event should also have hostname, for later 
*           differentiation of I/O activity of different VMs
* 		- Since we log blocks' content, block-level dedup is needed,
*			done by maintaining hashtable on MD5 hash of block. Every node
*			should contain timestamp information also, and this timestamp
*			is printed into trace to indicate duplicate content recorded.
*
* Output of preadwritedump.ko module - multiple trace files
*                              - this is input to Pre-processing phase/script
*/

/* 
*
* Credits:
*       http://tldp.org/LDP/lkmpg/2.6/html/lkmpg.html
*       Ricardo Koller's io_probe.c
*       Christian Bienia for mbuffer.c
*       Linux kernel
*       Stephen Smalley, <sds@epoch.ncsc.mil> for hashtab.c
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
#include <linux/types.h>        /* sector_t */
#include <linux/kthread.h>

#include <linux/sched.h>    /* Put ourselves to sleep and wake up later */
#include <linux/init.h>     /* For __init and __exit */
#include <linux/interrupt.h>    /* For irqreturn_t */
#ifndef IOONLY
	#include "pdd_scandisk.h"
#endif
#include "prwd_events.h"
#include "prwd_iodisk.h"
#include "relayframe.h"
#include "config.h"
#include "kernel_signal_usr.h"

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 20)
	#define nr_cpu_ids NR_CPUS
#endif

#if 0
#if ((LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)))
    struct work_struct Task;
	extern struct workqueue_struct* my_workqueue;
#else
	extern struct delayed_work scandisk_work;
#endif
#endif

char devname[10];
dev_t dev;

#if YESHASHTAB
unsigned int blktab_size = 0;
extern unsigned int num_bucketentries = 0;
extern int nomore_hashtab_insert;
extern struct lblktab_t iotab;
#ifndef IOONLY
extern struct lblktab_t scantab;
#endif
#endif

#ifndef IOONLY
extern struct rchan *scanchan;	/* relay channel for scanning phase */
#endif
extern long wrapptime;
extern struct rchan *iochan;     /* relay channel for read/write req */
extern struct dentry *signalfile;
extern struct siginfo usrinfo;
extern int usrpid;
extern wait_queue_head_t usrpid_waitq;

#ifndef IOONLY
extern struct task_struct *kthreadp;
extern wait_queue_head_t scanning_waitq;
extern atomic_t scanning;
#endif
extern wait_queue_head_t outs_ios_waitq;
extern atomic_t outs_ios;

#ifndef IOONLY
extern int die;
#endif

extern unsigned long long totalioc;
#ifndef IOONLY
extern unsigned long long *scancount;   /* per-file bytecount for scanning */
#endif
extern unsigned long long *iocount;     /* per-file bytecount for I/O */

int isPrime(unsigned int num)
{
	unsigned int i = 0;

	if(num%2==0)
		return 0;
	for(i=3; i<=num/2; i+=2)
	{
		if(num%i==0)
			return 0;
	}
	return 1;
}

static int __init preadwritedump_init(void)
{
    int rc = 0;

	unsigned int TOT_VMALLOC_USED = (SUBBUF_SIZE_SCANDEF * N_SUBBUFS_SCANDEF)/(1024*1024) + (SUBBUF_SIZE_IODEF * N_SUBBUFS_IODEF)/(1024*1024);

#ifdef DEBUG_SS
	printk(KERN_DEBUG "PRWD: (SUBBUF_SIZE_SCANDEF * N_SUBBUFS_SCANDEF)/(1024*1024)= %u MB\n", (SUBBUF_SIZE_SCANDEF * N_SUBBUFS_SCANDEF)/(1024*1024));
	printk(KERN_DEBUG "PRWD: (SUBBUF_SIZE_IODEF * N_SUBBUFS_IODEF)/(1024*1024) = %u MB\n", (SUBBUF_SIZE_IODEF * N_SUBBUFS_IODEF)/(1024*1024));
	printk(KERN_DEBUG "PRWD: TOT_VMALLOC_USED = %u MB\n", TOT_VMALLOC_USED);
#endif	

	printk(KERN_DEBUG "PRWD: Assuming total vmalloc space of %u usable\n",
			TOT_VMALLOC_USED);
#ifndef CONFIG_X86_64
	if (TOT_VMALLOC_USED > 110)
	{
		printk(KERN_DEBUG "PRWD: Assuming too much vmalloc space above\n");
		return -1;
	}
	printk(KERN_DEBUG "Vmalloc assumption is fine -- %u <= %u\n",
					TOT_VMALLOC_USED, 110);
#endif

#ifndef IOONLY
    scancount = kzalloc(nr_cpu_ids * sizeof(unsigned long long), GFP_KERNEL);
    if (!scancount)
    {
        printk(KERN_CRIT "Error in malloc for scancount \n");
        return -1;
    }
#endif

    iocount = kzalloc(nr_cpu_ids * sizeof(unsigned long long), GFP_KERNEL);
    if (!iocount)
    {
        printk(KERN_CRIT "Error in malloc for iocount \n");
        return -1;
    }

	/* Setting up for kernel to send signals to consumer */
	rc = kernel_signal_usr_init();
    if (rc)
    {
        printk(KERN_CRIT "PRWD:Setup for signals failed \n");
        goto out_signals;
    }

	sprintf(devname, "%s", xstringify(DISKNAME));
   	printk(KERN_DEBUG "PRWD:Disk name = %s\n", devname);
	dev = name_to_dev_t((char*)devname);
	if (dev == 0)
	{
		printk(KERN_CRIT "Getting gmajor:gminor didnt work for %s\n",
						devname);
		goto out_signals;
	}

    rc = start_relays();
    if (rc)
    {
        printk(KERN_CRIT "PRWD:relay channels can't be initialized.\n");
        goto out_relay;
    }

    rc = start_probes();
    if (rc)
    {
        printk(KERN_CRIT "PRWD:generic_make_request jprobe can't be registered.\n");
        goto out_jp;
    }

#ifndef IOONLY
	/* Scheduling scandisk_routine via kthreadp, because otherwise msleep()
	 * is causing VM freezing trouble.
	 */
	kthreadp = kthread_run(scandisk_routine, NULL, "scandisk_prog");
	if (!kthreadp)
	{
		printk(KERN_CRIT "PRWD:scandisk_routine thread failed\n");
		goto out_jp;
	}
		
    printk(KERN_NOTICE "PRWD:preadwritedump module loaded after creating kthreadp\n");
#endif

out:
    return rc;

out_jp:
    end_probes();
out_relay:
    end_relays();
    kernel_signal_usr_exit();
out_signals:
#ifndef IOONLY
    kfree(scancount);
#endif
    kfree(iocount);
    goto out;
}

static void __exit preadwritedump_exit(void)
{
	char countstr[20];
	int stat = 0;

#if YESHASHTAB
	if (nomore_hashtab_insert == 0)
		printk(KERN_DEBUG "PRWD: No events dropped so far\n");
	else
		printk(KERN_DEBUG "PRWD: Some event was dropped => no iotab inserts\n");
#endif

	printk(KERN_DEBUG "PRWD:calling end_probes\n");
	end_probes();

#ifndef IOONLY
	if (die == 0)
	{
    	/* Turn die = 1 so that scan disk will stop soon in case its not over yet */
    	die = 1;

		printk(KERN_DEBUG "PRWD:Waiting for scandisk_routine to exit\n");

    	/* Wait for scandisk_routine to exit gracefully */
		wait_event(scanning_waitq, atomic_read(&scanning)==0);
	}
#endif

#ifndef IOONLY
	if (kthreadp != NULL)
	{
		printk(KERN_DEBUG "PRWD:kthreadp is existing, so stop it\n");
		kthread_stop(kthreadp);
	}
	printk(KERN_DEBUG "PRWD:kthread_stop() done\n");
	kthreadp = NULL;
#endif

#if 0
#if((LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)))
    flush_workqueue(my_workqueue);  /* wait till all "old ones" finished */
    destroy_workqueue(my_workqueue);
#else
	cancel_delayed_work(&scandisk_work);
	flush_scheduled_work();
#endif
#endif

    /* wait for all i/os depending on our module to return control */
	printk(KERN_DEBUG "PRWD:Waiting for all i/os\n");
	wait_event(outs_ios_waitq,atomic_read(&outs_ios)==0);

	/* Write out the bytecounts to I/O relay channel. This entry at
	 * end of the files will show that the exit_module routine was invoked.
	 */
	sprintf(countstr, "%llu\n", totalioc);
	totalioc += strlen(countstr);
	relay_write(iochan, countstr, strlen(countstr));

    /* Flush once all ios are finished */
   	relay_flush(iochan);

	/* By now, all pending writes to relay channel done 
	 * So, send signal to usrpid (consumer) notifying them of the same.
	 * Upon receiving signal, consumer should read up pending data
	 * from corresponding files and once all threads stop, the usrpid
	 * should be deleted from signalconfpid, and then exit. Thus,
	 * the deletion of pid from signalconfpid should indicate here 
	 * that it is safe to delete the relay channel files from debugfs
	 * using wake_up(&usrpid_waitq);
	 */
	printk(KERN_DEBUG "PRWD:Send signal and wait for pid=0\n");
	stat = get_task_send_signal();
	if (!stat)
		wait_event(usrpid_waitq, usrpid==0);
	else
		printk(KERN_CRIT "PRWD:Consumer signaling problem, exit immediately\n");

	printk(KERN_DEBUG "PRWD:Go to end_relays()\n");

    end_relays();
	kernel_signal_usr_exit();

#if YESHASHTAB
	printk(KERN_DEBUG "PRWD:Will delete lblktab\n");
    iotab_exit();
#endif

    printk(KERN_NOTICE "PRWD:preadwritedump module unloaded\n");
}

/*
 * Register the initializer and finalizer module functions.
 */
module_init(preadwritedump_init);
module_exit(preadwritedump_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sujesha Sudevalayam <sujesha@cse.iitb.ac.in>");
