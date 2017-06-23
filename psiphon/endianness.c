/* This file contains general functions to handle endianness using
 * an element in struct called "magic". It is not dependent on the
 * structure itself, only the magic value needs to be passed here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <byteswap.h>
#include <endian.h>

#include <asm/types.h>
#include <linux/types.h>

#include "endianness.h"
#include "trace_struct.h"

int data_is_native = -1;
struct trace_event_element;

int verify_trace(__u32 magic)
{   
    if (!CHECK_MAGIC(magic)) {
        fprintf(stderr, "bad trace magic %x\n", magic);
        return 1;
    }
    if ((magic & 0xff) != ENDIAN_VERSION) {
        fprintf(stderr, "unsupported trace version %x\n",
            magic & 0xff); 
        return 1;
    }   
        
    return 0;
}


/*
 * check whether data is native or not
 */
int check_data_endianness(struct trace_event_element *t)
{           
    if ((t->magic & 0xffffff00) == ENDIAN_MAGIC) {
        data_is_native = 1;
        return 0;
    }       
            
	fprintf(stderr, "%u didnt match %u\n", t->magic, ENDIAN_MAGIC);
    t->magic = __bswap_32(t->magic);
    if ((t->magic & 0xffffff00) == ENDIAN_MAGIC) {
        data_is_native = 0;
        return 0;
    }       

	fprintf(stderr, "%u didnt match %u\n", t->magic, ENDIAN_MAGIC);
    return 1;
}             

