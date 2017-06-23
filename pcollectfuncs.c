
#include <linux/version.h>
#include <asm/types.h>
#include <linux/blkdev.h>       /* request_queue_t, bio */
#include "pcollectfuncs.h"
#include "prwd_events.h"

extern atomic_t outs_ios;
extern wait_queue_head_t outs_ios_waitq;

//Do not include prwd_iodisk.h here since it will cause loop with 
//pcollectfuncs.h
int handle_read_block_prwd(char *databuf,
					sector_t blockID, u32 nbytes, unsigned int pid, 
                    char *processname, unsigned major, unsigned minor);

/* Copied out from set_remap_sector() below */
sector_t adjust_sector(struct bio *bio)
{
    struct block_device *bdev = bio->bi_bdev;
#if((LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)))
	sector_t adjustedsector = bio->bi_sector;
#else
	sector_t adjustedsector = bio->bi_iter.bi_sector;
#endif

    if (bio_sectors(bio) && bdev != bdev->bd_contains) {
        struct hd_struct *p = bdev->bd_part;
        adjustedsector += p->start_sect;
	}
	return adjustedsector;
}

void set_remap_sector(struct bio *bio, struct old_bio_data_prwd *old_bio)
{
    struct block_device *bdev = bio->bi_bdev;
	//printk(KERN_DEBUG "PRWD: %s\n", __FUNCTION__);

    /*SSS: Noting the sector address and the device into old */
#if((LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)))
    old_bio->bi_sector = bio->bi_sector;
#else
    old_bio->bi_sector = bio->bi_iter.bi_sector;
#endif
    old_bio->bd_dev = bio->bi_bdev->bd_dev;

    /*SSS: if there is at least 1 sector & bdev is a partition */
    if (bio_sectors(bio) && bdev != bdev->bd_contains) {
        struct hd_struct *p = bdev->bd_part;

        /* SSS: if partition, note its sector address & device into old */
        old_bio->bi_sector += p->start_sect;
        old_bio->bd_dev = bdev->bd_contains->bd_dev;
    }
}

struct old_bio_data_prwd *vc_stack_endio_prwd(struct bio *bio, bio_end_io_t *fn, int gfp, char* processname, unsigned int tgid)
{
    struct old_bio_data_prwd *old_bio;
	//printk(KERN_DEBUG "PRWD: %s\n", __FUNCTION__);

    /* to be deallocated by the caller of vc_unstack_endio */
    old_bio = (struct old_bio_data_prwd *)
        kmalloc(sizeof(struct old_bio_data_prwd), gfp);
    if (!old_bio)
	{
        return NULL;
	}

    /* save old bio data */
    /*SSS: bi_end_io is the original kernel function, it is being stored 
 *          here for later restoration in vc_unstack_endio */
    old_bio->bi_end_io = bio->bi_end_io;
    /*SSS: bi_end_io is the original private data, it is being stored 
 *          here for later restoration in vc_unstack_endio */
    old_bio->bi_private = bio->bi_private;
	//printk(KERN_DEBUG "PRWD: going for myktime_get() in %s\n", __FUNCTION__);
    old_bio->qtime = ktime_to_ns(myktime_get());
#if((LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)))
    old_bio->bi_size = bio->bi_size;
#else
    old_bio->bi_size = bio->bi_iter.bi_size;
#endif	//KERNEL_VERSION
    /*SSS: set_remap_sector copies the sector & device */
#if 0
    old_bio->bi_sector = bio->bi_sector;
    old_bio->bd_dev = bio->bi_bdev->bd_dev;
#else
    set_remap_sector(bio,old_bio);
#endif

	/*SSS*/
	strcpy(old_bio->processname, processname);
	old_bio->tgid = tgid;
#if((LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)))
	old_bio->requested_len = bio->bi_size;
#else
	old_bio->requested_len = bio->bi_iter.bi_size;
#endif
	old_bio->major = MAJOR(bio->bi_bdev->bd_dev);
	old_bio->minor = MINOR(bio->bi_bdev->bd_dev);

    if(bio_data_dir(bio) == WRITE)
		printk(KERN_CRIT "PRWD: WRITE request not expected here!\n");

	//printk(KERN_DEBUG "PRWD: request proceeding in %s\n", __FUNCTION__);

    /* modify and save old bio */
    /*SSS: we change the function to be executed at bi_end_io() to vc_bio_end*/
    bio->bi_end_io = fn;
    /*SSS: saved our old_bio as private */
    bio->bi_private = (void *) old_bio;

    return old_bio;
}

struct old_bio_data_prwd * vc_unstack_endio_prwd(struct bio *bio)
{
    struct old_bio_data_prwd *old_bio;

	//printk(KERN_DEBUG "PRWD: %s\n", __FUNCTION__);

    /* set bio's old data */
    /* saving private as old bio */
    old_bio = (struct old_bio_data_prwd *) bio->bi_private;

    /*SSS: restoring private's bi_end_io & private as bio's */
    bio->bi_end_io = old_bio->bi_end_io;
    bio->bi_private = old_bio->bi_private;

    return old_bio;
}

/*SSS: function to be executed at bi_end_io() replaced with vc_bio_end */
/*SSS: after entry at my_make_request, eventual execution will result in 
 *      this replaced function being called.
 *      Why didnt we just have another probe for this place? Because all
 *		the correct sector values and bi_size values are available only
 *		at generic_make_request()
 */
////////////DEFINE_PER_CPU(char[PRINT_BUF], end_io_buf);
#if((LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)))
int vc_bio_end_prwd(struct bio *bio, unsigned int bytes_done, int err)
#else
void vc_bio_end_prwd(struct bio *bio, int err)
#endif
{
    struct old_bio_data_prwd *old_bio;
	unsigned long flags;
//    s64 qtime;
	char *databuf = NULL;
	u32 len = 0;
#if((LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)))
	unsigned int j = 0;
	struct bio_vec *bv;	
#else
	struct bvec_iter j;
	struct bio_vec bvec;	
	struct bio_vec *bv;	
#endif
#if 1
	int rc;
#endif

	//printk(KERN_DEBUG "PRWD: %s\n", __FUNCTION__);
    /*SSS: old_bio is pointer to private of bio 
 *          Basically, restore the bi_end_io & bi_private after
 *          first retrieving our private data back into old_bio */
    old_bio = vc_unstack_endio_prwd(bio);


//    qtime = old_bio->qtime;

#if 0
	/* This is true, but commenting it just to speed things up */
	if (bio_data_dir(bio) != READ)
		printk(KERN_DEBUG "we donot expect WRITE requests to reach here!\n");
#endif

	databuf = kzalloc(old_bio->requested_len, GFP_ATOMIC);
#if 0
	/* If databuf is not allocated, we will see a segmentation fault
	 * ahead anyway, so not checking this here just to speed things
	 * up in the case where allocation is success (which is usual case).
	 */
	if (!databuf)
	{
		printk(KERN_CRIT "PRWD: Couldnt allocate databuf\n");
		//kfree(tempdata);
		atomic_dec(&outs_ios);
		wake_up(&outs_ios_waitq);
#if((LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)))
		return -1;
#endif
	}
#endif
		
    //templocal_irq_save(flags);      //disabling interrupts on this processor
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
#if((LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)))
			return -1;
#endif
		}
		len += bv->bv_len;
		memcpy(databuf + len - bv->bv_len, data, bv->bv_len);
		bvec_kunmap_irq(data, &flags);
	}

#if 0
		if (len > 524288)
			printk(KERN_DEBUG "PRWD: Huge len in read = %u\n", len);

		/* This mismatch never happens :-) */
		if (len != old_bio->requested_len)
			printk(KERN_CRIT "PRWD: len (%u) doesnt match requested len (%u)\n",
				len, old_bio->requested_len);

	/* Does this mismatch occur? ALWAYS bio has 0 and old_bio has non-zero. */
	if (MINOR(bio->bi_bdev->bd_dev) != old_bio->minor)
		printk(KERN_CRIT "PRWD: Mismatched MINOR in bio (%u) and old_bio (%u)\n",
			MINOR(bio->bi_bdev->bd_dev), old_bio->minor);
	else
		printk(KERN_CRIT "PRWD: MINOR matches!\n");

	/* This mismatch occurs ALWAYS, with and without the adjustment done, 
	 * here as well as	in set_remap_sector()!  */
	//if (adjust_sector(bio) != old_bio->bi_sector)
	if (bio->bi_sector != old_bio->bi_sector)
		printk(KERN_CRIT "PRWD: Mismatched sectors in bio(%llu) & old_bio(%llu)\n",
			bio->bi_sector, old_bio->bi_sector);
			//adjust_sector(bio), old_bio->bi_sector);
	else
		printk(KERN_CRIT "PRWD: sector matches!\n");

	/* This mismatch occurs a lot! */
	if (len != bio->bi_size)
		printk(KERN_CRIT "PRWD: len (%u) does not match bi_size(%u)\n",
			len, bio->bi_size);
#endif

#if 0
	rc = handle_read_block_prwd(databuf, len, bio->bi_sector, 
					old_bio->tgid, old_bio->processname, 
//					777, "dontcare",
							MAJOR(bio->bi_bdev->bd_dev),
							MINOR(bio->bi_bdev->bd_dev));
#else
	rc = handle_read_block_prwd(databuf, len, old_bio->bi_sector, 
					old_bio->tgid, old_bio->processname, 
					old_bio->major, old_bio->minor);
#endif
	if (rc)
	{
		printk(KERN_CRIT "PRWD: Error in handle_read_block_prwd\n");
	}
	kfree(databuf);
    kfree(old_bio);

    /* SSS: decrement to say that we have finished one request */
    atomic_dec(&outs_ios);
    wake_up(&outs_ios_waitq);
    //templocal_irq_restore(flags);      //enabling interrupts on this processor

    /*SSS: invoke the original bi_end_io() */
#if((LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)))
    return bio->bi_end_io(bio, bytes_done, err);
#else
    bio->bi_end_io(bio, err);
#endif
}


