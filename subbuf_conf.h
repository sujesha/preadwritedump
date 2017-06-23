#ifndef _SUBBUF_CONF_H
#define _SUBBUF_CONF_H

#include "config.h"

#ifndef CONFIG_X86_64
	//32-bit
	#define SUBBUF_SIZE_SCANDEF     (512U<<10)
	#define SUBBUF_SIZE_IODEF     (1U<<20)
	#define N_SUBBUFS_SCANDEF       (4)
	#define N_SUBBUFS_IODEF       (48)
#else
	//64-bit
	#define SUBBUF_SIZE_SCANDEF     (512U<<10)
	#define SUBBUF_SIZE_IODEF     (1U<<20)
	#define N_SUBBUFS_SCANDEF       (4)
	#define N_SUBBUFS_IODEF       (98)
#endif


/* Config for scanning */
#if PARM_SUBBUF_SIZE_SPEC_SCAN
    #define CURR_SUBBUF_SIZE_SCAN (PARM_SUBBUF_SIZE_VAL_SCAN << 20)
#else
    #define CURR_SUBBUF_SIZE_SCAN SUBBUF_SIZE_SCANDEF
#endif

#if PARM_N_SUBBUFS_SPEC_SCAN
    #define CURR_N_SUBBUFS_SCAN PARM_N_SUBBUFS_VAL_SCAN
#else
    #define CURR_N_SUBBUFS_SCAN N_SUBBUFS_SCANDEF
#endif

/* Config for I/O trace */
#if PARM_SUBBUF_SIZE_SPEC_IO
    #define CURR_SUBBUF_SIZE_IO (PARM_SUBBUF_SIZE_VAL_IO << 20)
#else
    #define CURR_SUBBUF_SIZE_IO SUBBUF_SIZE_IODEF
#endif

#if PARM_N_SUBBUFS_SPEC_IO
    #define CURR_N_SUBBUFS_IO PARM_N_SUBBUFS_VAL_IO
#else
    #define CURR_N_SUBBUFS_IO N_SUBBUFS_IODEF
#endif


#endif /* _SUBBUF_CONF_H */
