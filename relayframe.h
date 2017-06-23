#ifndef _RELAYFRAME_H
#define _RELAYFRAME_H

#include "config.h"


#define PRINT_HEADER    (256)
#define PRINT_BUF       (PRINT_HEADER + BLKSIZE)

#define RELAY_TRY_AGAIN (-789)
#define KFREE_LDATUM (-790)

#if((LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)))
struct pdd_trace 
{
	struct rchan *rchan;
	struct dentry *dropped_file;
	atomic_t dropped;
};
#endif

int relaychan_init(char *eventsfile, struct rchan **chan, size_t size, 
			size_t n, void *private_data);
void relaychan_exit(struct rchan **chan);

#endif
