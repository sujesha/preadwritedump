#ifndef _PCOLLECTFUNCS_H_
#define _PCOLLECTFUNCS_H_

#include "config.h"

struct old_bio_data_prwd {
    void        	*bi_private;
    bio_end_io_t    *bi_end_io;
    s64     		qtime;
    sector_t    	bi_sector;
    unsigned int    bi_size;
    dev_t       	bd_dev;

	/*SSS*/
	unsigned 		major;
	unsigned 		minor;
	u32 			requested_len;
	unsigned int 	tgid;	//pid 
	char 			processname[PNAME_LEN];
};


sector_t adjust_sector(struct bio *bio);
void set_remap_sector(struct bio *bio, struct old_bio_data_prwd *old_bio);
struct old_bio_data_prwd *vc_stack_endio_prwd(struct bio *bio, 
						bio_end_io_t *fn, int gfp, 
						char *processname, unsigned int tgid);
struct old_bio_data_prwd * vc_unstack_endio_prwd(struct bio *bio);

#if((LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)))
int vc_bio_end_prwd(struct bio *bio, unsigned int bytes_done, int err);
#else
void vc_bio_end_prwd(struct bio *bio, int err);
#endif


#endif //_PCOLLECTFUNCS_H_
