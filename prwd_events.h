#ifndef _PRWD_EVENTS_H_
#define _PRWD_EVENTS_H_

#include "prwd_lblk.h"
#include "subbuf_conf.h"

#if 0
/*SSS: 4 buffers of 512 KB each in relay channel, default in blktrace also */
#define N_SUBBUFS       (4)
#define SUBBUF_SIZE     (512U<<10)
#endif

/* From Documentation/lguest/lguest.c
 * Note that u64 is always unsigned long long, which works on all Linux 
 * systems: this means that we can use %llu in printf for any u64.
 */
//#ifndef CONFIG_X86_64
#define PRI_SECT    "%llu"
//#else
//#define PRI_SECT    "%lu"
//#endif

#ifndef CONFIG_X86_64
	//32-bit
	#define PRI_SIZEOF "%u"
	#define PRI_SIZET "%u"
	#define HASHTAB_TOTSIZE_MB 24
#else
	//64-bit
	#define PRI_SIZEOF "%lu"
	#define PRI_SIZET "%lu"
	#define HASHTAB_TOTSIZE_MB 200
#endif

//#define TOT_VMALLOC_USED (((HASHTAB_SIZE_MB << 20)*2 + (HASHTAB_TOTSIZE_MB << 20)*2 + (SUBBUF_SIZE_SCANDEF * N_SUBBUFS_SCANDEF + SUBBUF_SIZE_IODEF * N_SUBBUFS_IODEF)))


/* Trace categories - multiple of these flags together form below events*/
enum {
        PRO_TC_READ         = 1 << 0,       /* reads */
        PRO_TC_WRITE        = 1 << 1,       /* writes */
        PRO_TC_SCANDISK     = 1 << 2,       /* scandisk phase */
        PRO_TC_IODISK       = 1 << 3,       /* online (io) phase */
        PRO_TC_FIXED        = 1 << 4,       /* related to blocks */
        PRO_TC_VARIABLE     = 1 << 5,       /* related to chunks */
        PRO_TC_NEW          = 1 << 6,       /* unseen/unique content */
        PRO_TC_DEDUP        = 1 << 7,       /* dedup content */
        PRO_TC_ZERO			= 1 << 8,       /* dedup content */
        PRO_TC_IODUMP		= 1 << 9,       /* online (io) phase with dump */

        PRO_TC_END          = 1 << 15,      /* only 16-bits, reminder */
};

#define PRO_TC_SHIFT            (16)
#define PRO_TC_ACT(act)         ((act) << PRO_TC_SHIFT)

/* 
 * Basic trace events (analogous to actions in blktrace)
 */
enum event_type{
    __PRO_TA_SVNx = 1,   /* scanning phase new chunk */
    __PRO_TA_SVDx,       /* scanning phase dedup chunk */
    __PRO_TA_SFN,       /* scanning phase new block */
    __PRO_TA_SFD,       /* scanning phase dedup block */
    __PRO_TA_SFxR,       /* scanning phase read event */
    __PRO_TA_SFNW,       /* scanning phase write event new block */
    __PRO_TA_SFDW,       /* scanning phase write event dedup block */
    __PRO_TA_SVNW,       /* scanning phase new chunk replacement */
    __PRO_TA_SVDW,       /* scanning phase dedup chunk replacement */
    __PRO_TA_OFR,       /* online phase read event */
    __PRO_TA_DFR,       /* online phase read event with datadump */
    __PRO_TA_OFNW,       /* online phase write event - new block content */
    __PRO_TA_OFDW,       /* online phase write event - dedup block content */
    __PRO_TA_OVNW,       /* online phase new chunk */
    __PRO_TA_OVDW,       /* online phase dedup chunk */
    __PRO_TA_OFWR,       /* online phase read event (self-generated) */
	__PRO_TA_SZ,		 /* scanning phase zero block */
};

/*
 *	(preadwritedump) Trace actions in full.
 */
#define PRO_TA_SFN     (__PRO_TA_SFN | PRO_TC_ACT(PRO_TC_SCANDISK) | \
                         PRO_TC_ACT(PRO_TC_FIXED) | PRO_TC_ACT(PRO_TC_NEW))
#define PRO_TA_SFD     (__PRO_TA_SFD | PRO_TC_ACT(PRO_TC_SCANDISK) | \
                         PRO_TC_ACT(PRO_TC_FIXED) | PRO_TC_ACT(PRO_TC_DEDUP))
#define PRO_TA_SZ		(__PRO_TA_SZ | PRO_TC_ACT(PRO_TC_SCANDISK) | \
						PRO_TC_ACT(PRO_TC_ZERO))
#define PRO_TA_OFR     (__PRO_TA_OFR | PRO_TC_ACT(PRO_TC_IODISK) | \
                         PRO_TC_ACT(PRO_TC_FIXED) | PRO_TC_ACT(PRO_TC_READ))
#define PRO_TA_OFNW     (__PRO_TA_OFNW | PRO_TC_ACT(PRO_TC_IODISK) | \
                         PRO_TC_ACT(PRO_TC_FIXED) | PRO_TC_ACT(PRO_TC_NEW) | \
                        PRO_TC_ACT(PRO_TC_WRITE))
#define PRO_TA_OFDW     (__PRO_TA_OFDW | PRO_TC_ACT(PRO_TC_IODISK) | \
                         PRO_TC_ACT(PRO_TC_FIXED) | PRO_TC_ACT(PRO_TC_DEDUP)| \
                        PRO_TC_ACT(PRO_TC_WRITE))
#define PRO_TA_DFR     (__PRO_TA_DFR | PRO_TC_ACT(PRO_TC_IODUMP) | \
                         PRO_TC_ACT(PRO_TC_FIXED) | PRO_TC_ACT(PRO_TC_READ))

/* not needed */
#define PRO_TA_SFxR     (__PRO_TA_SFxR | PRO_TC_ACT(PRO_TC_SCANDISK) | \
                         PRO_TC_ACT(PRO_TC_FIXED) | PRO_TC_ACT(PRO_TC_READ))
/* not needed */
#define PRO_TA_SFNW     (__PRO_TA_SFNW | PRO_TC_ACT(PRO_TC_SCANDISK) | \
                         PRO_TC_ACT(PRO_TC_FIXED) | PRO_TC_ACT(PRO_TC_NEW) | \
                        PRO_TC_ACT(PRO_TC_WRITE))
/* not needed */
#define PRO_TA_SFDW     (__PRO_TA_SFDW | PRO_TC_ACT(PRO_TC_SCANDISK) | \
                         PRO_TC_ACT(PRO_TC_FIXED) | PRO_TC_ACT(PRO_TC_DEDUP) | \
                        PRO_TC_ACT(PRO_TC_WRITE))
/* not needed */
#define PRO_TA_OFWR     (__PRO_TA_OVDW | PRO_TC_ACT(PRO_TC_IODISK) | \
                         PRO_TC_ACT(PRO_TC_FIXED)|PRO_TC_ACT(PRO_TC_READ) \
                        | PRO_TC_ACT(PRO_TC_WRITE))


int start_relays(void);
void end_relays(void);

#ifndef IOONLY
int scandisk_relaychan_init(void);
void scandisk_relaychan_exit(void);
int stash_new_block(unsigned char* buf, sector_t blockID);
#endif
int iodisk_relaychan_init(void);
void iodisk_relaychan_exit(void);

int write_event(int traceaction, struct block_node *ldatum,
    unsigned char *data, sector_t blockID, u32 nbytes,
    struct process_node *pnode,
    unsigned long long dupoffset, unsigned char dupcpu, s64 ptime);

void get_timestamp(lblk_datum *ldatum);

unsigned char* alloc_kmem(int len);
void free_mem(unsigned char *mem);
ktime_t myktime_get(void);
dev_t name_to_dev_t(char *name);
void note_blocknode_attrs(lblk_datum *ldatum);

#endif
