#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <linux/kernel.h>
#ifndef PREADWRITEDUMP
	#include "md5.h"
#endif

#define NUMBITS_SECTSIZE (9)
#define SECTSIZE (1 << NUMBITS_SECTSIZE)
#define NUMBITS_4K  (12)
#define K4_SIZE (1 << NUMBITS_4K)
#define SECT_TO_4K_SHIFTNUM (NUMBITS_4K - NUMBITS_SECTSIZE)

/* Defining default compile-time configuration (over-ridden by Makefile) */
#define SECT_LEN (1<<NUMBITS_SECTSIZE)

#ifndef BLKSIZE
	#define NUMBITS_BLKSIZE (NUMBITS_4K)
	#define BLKSIZE (SECT_LEN << SECT_TO_4K_SHIFTNUM)
#endif

#ifndef HOSTNAME
	#define HOSTNAME "PROVM"
#endif

#define BLKTAB_SIZE (1<<10)

/* In linux kernel, processname (comm) is array of 16 char 
 * If this changes, change it here.
 */
#define PNAME_LEN 16

/* 
 * Defining modules according to configuration 
 */
#define xstringify(X) stringify(X)
#define stringify(X) #X

/* Hashing-related module configuration */
#define HASHLEN MD5_SIZE
#define HASHLEN_STR (HASHLEN*2 + 1)
#define getHash(args...) md5(args)

/* Use hashMagic in addition, to deal with hash-collisions */
#if NOHASHMAGIC
	#define ENABLE_HASHMAGIC 0
#else
	#define ENABLE_HASHMAGIC 1
#endif

#if ENABLE_HASHMAGIC
	#define MAGIC_SIZE 4
#else
	#define MAGIC_SIZE 0
#endif

/* Scandisk Relay channel's configuration parameters */
#if PARM_SUBBUF_SIZE_SCAN
	#define PARM_SUBBUF_SIZE_SPEC_SCAN 1
	#define PARM_SUBBUF_SIZE_VAL_SCAN PARM_SUBBUF_SIZE_SCAN
#else
	#define PARM_SUBBUF_SIZE_SPEC_SCAN 0
#endif

#if PARM_N_SUBBUFS_SCAN
	#define PARM_N_SUBBUFS_SPEC_SCAN 1
	#define PARM_N_SUBBUFS_VAL_SCAN PARM_N_SUBBUFS_SCAN
#else
	#define PARM_N_SUBBUFS_SPEC_SCAN 0
#endif

/* I/O trace Relay channel's configuration parameters */
#if PARM_SUBBUF_SIZE_IO
    #define PARM_SUBBUF_SIZE_SPEC_IO 1
    #define PARM_SUBBUF_SIZE_VAL_IO PARM_SUBBUF_SIZE_IO
#else
    #define PARM_SUBBUF_SIZE_SPEC_IO 0
#endif

#if PARM_N_SUBBUFS_IO
    #define PARM_N_SUBBUFS_SPEC_IO 1
    #define PARM_N_SUBBUFS_VAL_IO PARM_N_SUBBUFS_IO
#else
    #define PARM_N_SUBBUFS_SPEC_IO 0
#endif



#endif	/* _CONFIG_H_ */

