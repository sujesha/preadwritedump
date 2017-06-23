/*
 * Modeled from pdd_lblktab.h but without the hash-table functionality!
 *
 * Author : Sujesha Sudevalayam
 * Credit : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#ifndef _PRWD_LBLK_H_
#define _PRWD_LBLK_H_

#include <asm/types.h>
#include "config.h"

/* This is separated out from block_node, since this info need not be
 * stored in lblktab hash-table, though needed for event printing
 */
struct process_node{
	char processname[PNAME_LEN];
	unsigned int pid;
	unsigned int major;
	unsigned int minor;
};

struct block_node{
    unsigned long long chanoffset;
    //unsigned char bhashkey[HASHLEN + MAGIC_SIZE]; /* magic appended to hash */
    unsigned char cpunum;
};      
typedef struct block_node lblk_datum;



#endif  /* _PRWD_LBLK_H_ */
