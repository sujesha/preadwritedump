/*
* Logging I/O disk activity for provided project (data dumping version)
* Sujesha Sudevalayam
* 
* This file only implements the iodisk functionality & logs all write content
*/

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
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/percpu.h>
#include "prwd_iodisk.h"
#include "prwd_lblk.h"
#include "prwd_events.h"
#include "pcollectfuncs.h"

atomic_t outs_ios = ATOMIC_INIT(0);
DECLARE_WAIT_QUEUE_HEAD(outs_ios_waitq);

static int probes_reg_flag = 0;

#if YESHASHTAB
#ifndef IOONLY
extern struct lblktab_t scantab;
#endif
extern struct lblktab_t iotab;
extern int nomore_hashtab_insert;
#endif

extern unsigned int gmajor;
extern unsigned int gminor;

/* Return 0 for success */
int handle_read_block_prwd(char *databuf, u32 nbytes,
					sector_t blockID, unsigned int pid, 
	            char *processname, unsigned major, unsigned minor)
{
	struct process_node p;
	s64 ptime;
	int rc = 0;
    lblk_datum *ldatum = NULL;
//	printk(KERN_DEBUG "PRWD: %s\n", __FUNCTION__);

	ldatum = (lblk_datum*)kzalloc(sizeof(lblk_datum), GFP_ATOMIC);
    if (ldatum == NULL)
    {
	printk(KERN_CRIT "PRWD:kzalloc failed for GFP_ATOMIC ldatum in I/O\n");
	return -1;
    }

	strcpy(p.processname, processname);
	p.pid = pid;
	p.major = major;
	p.minor = minor;

	ptime = ktime_to_ns(myktime_get());
    note_blocknode_attrs(ldatum);

	rc = write_event(PRO_TA_DFR, ldatum, databuf, blockID, nbytes, &p,
	    0, 0, ptime);

#if NOHASHTAB
	kfree(ldatum);	//potential memory leak otherwise
	ldatum = NULL;
#endif
	return rc;
}

int handle_write_block(char *databuf, u32 nbytes, sector_t blockID, 
		unsigned int pid, char *processname, unsigned major, unsigned minor)
{
	struct process_node p;
	int stat = 0;
    lblk_datum *ldatum = NULL;
	s64 ptime;

    // RAHUL: this is called inside ATOMIC, so use GFP_ATOMIC
	//ldatum = (lblk_datum*)kzalloc(sizeof(lblk_datum), GFP_KERNEL);
	ldatum = (lblk_datum*)kzalloc(sizeof(lblk_datum), GFP_ATOMIC);
    if (ldatum == NULL)
    {
	printk(KERN_CRIT "PRWD:kzalloc failed for GFP_ATOMIC ldatum in I/O\n");
	return -1;
    }

	/* Note process attributes */
	strcpy(p.processname, processname);
	p.pid = pid;
	p.major = major;
	p.minor = minor;

    note_blocknode_attrs(ldatum);

#ifdef DEBUG_SS
	printk(KERN_DEBUG "PRWD:Write for new block.\n");
#endif
		ptime = ktime_to_ns(myktime_get());
	stat = write_event(PRO_TA_OFNW, ldatum, databuf, blockID, nbytes, &p,
				0, 0, ptime);
	if (stat)
		{
			printk(KERN_CRIT "PRWD:write_event for I/O failed @ new\n");
			return stat;
		}

#if NOHASHTAB
		/* If this flag is enabled, we want to not use any hash-tables 
		 * for scanning and I/O trace collection. 
		 */
		kfree(ldatum);
		ldatum = NULL;
		return stat;
#endif

}

/* Probe for "Q" event  */
static void my_make_request(struct bio *bio)
{
    struct old_bio_data_prwd *old_bio;
#if((LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)))
	struct bio_vec *bv;
	unsigned j;
#else
	struct bio_vec bvec;
	struct bio_vec *bv;
	struct bvec_iter j;
#endif
	unsigned long flags;
	int rc = 0;
	int i;
	char processname[PNAME_LEN];	/* 16 used in kernel code also! */
    char *databuf = NULL;
	u32 len = 0;
		sector_t adjustedsector = 0;

	strncpy(processname, current->comm, 
			(strlen(current->comm)<PNAME_LEN)?strlen(current->comm):PNAME_LEN);
	processname[PNAME_LEN-1] = '\0';    //at least last char should be '\0'

	/* To ensure that processname is a single word */
	for(i=0; i<strlen(processname); i++)
	    if (processname[i] == ' ')
	    {
	        processname[i] = '\0';
	        break;
	    }

		/* Major number should match the input device */
		if (MAJOR(bio->bi_bdev->bd_dev) != gmajor)
		jprobe_return();

		/* If input device is having minor 0, then all minor numbers okay,
		 * else the minor numbers should also match
		 */
		if (gminor!=0 && MINOR(bio->bi_bdev->bd_dev) != gminor)
		jprobe_return();

#if 1
	/* Commented this to remove any inconsistency in trace data */
    if ((strstr(processname, "kjournald")) ||
		      (strstr(processname, "scandisk_prog")) ||
		      (strstr(processname, "printk")) ||
		      (strstr(processname, "flush-8:0")) ||
				(strstr(processname, "psiphon")) ||
				(strstr(processname, "mmap")) ||
		       (strstr(processname, "klogd")))
    {
		/* We want to ignore disk writes caused due to kjournald
		 * writing its journaling logs and also disk reads that
		 * may be caused due to scanning phase. We also ignore disk
		 * writes caused by the kernel logging/debugging process.
		 * Disk writes by the logging daemon "psiphon" is done to 
		 * NFS so that those wont be logged here.
		 */
	jprobe_return();
    }
#endif

#ifdef CAPTURE_KVM_ONLY
	if (!strstr(processname, "kvm") 
		&& !strstr(processname, "qemu")) {
		/* We want to capture events only for kvm processes here */
		jprobe_return();
	}
#endif

	if (strstr(processname, "psiphon") || strstr(processname, "mmap"))
		printk(KERN_DEBUG "processname %s is captured!\n", processname);

    atomic_inc(&outs_ios);


#if 0
		/* All requests have bio_offset == 0 i.e, page-aligned */
		if (bio_offset(bio)	== 0)
			printk(KERN_DEBUG "Request is page-aligned for rw(%d)\n",
					bio_data_dir(bio)==READ?1:0);
		else
			printk(KERN_DEBUG "Request is NOTTTT page-aligned %u for rw(%d)\n",
					bio_offset(bio), bio_data_dir(bio)==READ?1:0);

		/* All requests are aligned at 8 sectors within partition.... */
		if ((bio->bi_sector & 0x7) == 0)
			printk(KERN_DEBUG "Sector-aligned block within partition %u\n",
				(unsigned int)bio->bi_sector);
		else
			printk(KERN_DEBUG "Non-aligned at %u within partition\n", 
				(unsigned int)(bio->bi_sector & 0x7));

		/* .... but, are non-aligned wrt physical blocks on disk */
		if ((adjust_sector(bio) & 0x7) == 0)
			printk(KERN_DEBUG "Adjusted sector aligned %u\n",
				(unsigned int) adjust_sector(bio));
		else
			printk(KERN_DEBUG "Non-aligned adjusted sector at %u\n", 
				(unsigned int)(adjust_sector(bio) & 0x7));
#endif

		if (bio_data_dir(bio) == READ)
		{
		    /* stack the bio with our info */
		    /*     Our info = old_bio, and it has been set to bio->private
		     *     Also change the function to be executed at bi_end_io() 
		     *     to vc_bio_end_prwd().
		     */
			//printk(KERN_DEBUG "PRWD: %s READ\n", __FUNCTION__);
		    if(!(old_bio = vc_stack_endio_prwd(bio, vc_bio_end_prwd,
					GFP_ATOMIC, processname, 777)))
			{
			printk("PRWD: unable to allocate old_bio\n");
			atomic_dec(&outs_ios);
		        wake_up(&outs_ios_waitq);
	        	jprobe_return();
    		}
			
			/* DONE for READ, return */
		jprobe_return();
		}

		/* Else (bio_data_dir(bio) == WRITE) is true */
#if((LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)))
	databuf = kzalloc(bio->bi_size, GFP_ATOMIC);
#else
    databuf = kzalloc(bio->bi_iter.bi_size, GFP_ATOMIC);
#endif

#if 0
/* Commenting this to speed things up! */
			if (!databuf)
			{
				/* Can this happen if apt-get upgrade or apt-get update like
				 * commands try to fetch huge data and write to disk?
				 * If so, this will ensure that we dont seg-fault but
				 * we may end up losing write data. How to fix this?
				 * 1. Perhaps write out this part, free(databuf) and start
				 * again? 
				 * 2. Perhaps set a MAX limit on the size of write that we
				 * will "process" at one go, considering the next part
				 * as another write, even though they are data from the
				 * same original write request above?
				 * FIXME
				 */
				printk(KERN_CRIT "PRWD:Couldnt allocate databuf\n");
				//kfree(tempdata);
       			atomic_dec(&outs_ios);
		        wake_up(&outs_ios_waitq);
		        jprobe_return();
			}
#endif

			/* Each bio consists a list of bio_vectors, each containing data */
#if((LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)))
	bio_for_each_segment(bv, bio, j) 
#else
	bio_for_each_segment(bvec, bio, j) 
#endif
	{
		//char *tempdata;
		char *data;
#if((LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)))
		bv = &bvec;		
#endif			
		data = bvec_kmap_irq(bv, &flags);
		if (data == NULL)
		{
			printk(KERN_CRIT "PRWD:Error bvec_kmap_irq returned NULL!\n");
			atomic_dec(&outs_ios);
			wake_up(&outs_ios_waitq);
			jprobe_return();
		}	

	    len += bv->bv_len;
	    memcpy(databuf + len - bv->bv_len, data, bv->bv_len);
	    bvec_kunmap_irq(data, &flags);
    }
#if 1
			adjustedsector = adjust_sector(bio);
#else
			adjustedsector = bio->bi_sector;
#endif

#if 1
	    /* We issue a single write event for each write request 
	     * instead of one write request for every bio_vec 
	     */
			if (len > 524288)
				printk(KERN_DEBUG "PRWD: Huge len in write = %u\n", len);

	    rc = handle_write_block(databuf, len, adjustedsector,
	            current->tgid, processname,
	            MAJOR(bio->bi_bdev->bd_dev),
	            MINOR(bio->bi_bdev->bd_dev));
#else
			/* We issue 2 writes if len is too big */
			if (relaychannel capacity is less)
			{
				u32 chanavail = available space in relaychannel
				u32 chanavailsectors = chanavail >> 9;

	    	rc = handle_write_block(databuf, chanavail, adjustedsector,
	            current->tgid, processname,
	            MAJOR(bio->bi_bdev->bd_dev),
	            MINOR(bio->bi_bdev->bd_dev));
	    	rc = handle_write_block(databuf+chanavail, len-chanavail, 
					adjustedsector+chanavailsectors,
	            current->tgid, processname,
	            MAJOR(bio->bi_bdev->bd_dev),
	            MINOR(bio->bi_bdev->bd_dev));
			}
#endif
	    if (rc)
	    {
	        printk(KERN_CRIT "PRWD:Error in handle_write_block()\n");
	    }
			kfree(databuf);
		atomic_dec(&outs_ios);
	        wake_up(&outs_ios_waitq);

		//printk(KERN_DEBUG "PRWD: %s jprobe_return()\n", __FUNCTION__);
	jprobe_return();
}

/*
 * The jprobe structure
 */
static struct jprobe jp_make_request = {
    .kp.addr = (kprobe_opcode_t *) generic_make_request,
    .entry = (kprobe_opcode_t *) my_make_request
};

int start_probes(void)
{
	int rc = 0;
	if (probes_reg_flag == 0)
	{
		rc = register_jprobe(&jp_make_request);
		if (!rc)
			probes_reg_flag = 1;
	}
	
	return rc;
}

void end_probes(void)
{
	if (probes_reg_flag == 1)
	{
	    unregister_jprobe(&jp_make_request);
		probes_reg_flag = 0;
	}

	return;
}
