#if YESHASHTAB

/*
 * Implementation of the logical blocks table type, 
 * In pdatadump, this hash-table is based on block hash
 *
 * A logical block's size is integral multiple of sector size (512)
 *
 * Author : Sujesha Sudevalayam
 * Credit: Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#include <linux/string.h>
#include <linux/errno.h>
#include "pdd_lblktab.h"

#ifndef IOONLY
struct lblktab_t scantab;
static int scantab_alive = 0;
#endif
struct lblktab_t iotab;
static int iotab_alive = 0;

/* --------uses hashtab to create hashtable of blocks-----------------*/

/* In pcollect, the key being input here is the blockID */
/* In pdatadump, the key being input here is the block bhash */
static u32 lblkhash(struct hashtab *h, const void *key)
{
        char *p, *keyp;
        unsigned int size;
        unsigned int val;

		if (!h || !key)
		{
        	printk(KERN_CRIT "PDD:NULL table or key in lblkhash().\n");
			return -1;
		}

#ifdef DEBUG_SS
       	printk(KERN_DEBUG "PDD:Non-null table or key in lblkhash(). Good.\n");
#endif
        val = 0;
        keyp = (char *)key;
        size = strlen(keyp);
        for (p = keyp; (p - keyp) < size; p++)
                val = (val << 4 | (val >> (8*sizeof(unsigned int)-4))) ^ (*p);
#ifdef DEBUG_SS
       	printk(KERN_DEBUG "PDD:Calculated val in lblkhash().\n");
#endif
        return val & (h->size - 1);
}

/* In pcollect, compare block IDs */
/* In pdatadump, compare block bhash */
static int lblkcmp(struct hashtab *h, const void *key1, const void *key2)
{
        char *keyp1, *keyp2;
#ifdef DEBUG_SS
       	printk(KERN_DEBUG "PDD:Beginning lblkcmp().\n");
#endif

        keyp1 = (char *)key1;
        keyp2 = (char *)key2;
#ifdef DEBUG_SS
       	printk(KERN_DEBUG "PDD:Assigned keys before strcmp.\n");
		printk(KERN_DEBUG "PDD:Keys being strcmp are %s and %s\n", 
				keyp1, keyp2);
#endif
        return memcmp(keyp1, keyp2, HASHLEN+MAGIC_SIZE);
}

int lblktab_init(struct lblktab_t *l, unsigned int size)
{

#ifdef DEBUG_SS
       	printk(KERN_DEBUG "PDD:Beginning lblktab_init().\n");
#endif
        l->table = hashtab_create(lblkhash, lblkcmp, size);
        if (!l->table)
                return -1;
#ifdef DEBUG_SS
       	printk(KERN_DEBUG "PDD:Successful lblktab_init().\n");
#endif
        l->nprim = 0;
        return 0;
}

#ifndef IOONLY
int scantab_init(struct lblktab_t *l, unsigned int size)
{
    if (scantab_alive)
    {
#ifdef DEBUG_SS
        printk(KERN_DEBUG "PDD:scantab already exists!\n");
#endif  
        return 0;
    }
    if(lblktab_init(&scantab, size))
        return -1;

    scantab_alive = 1;
    return 0;
}   
#endif
    
int iotab_init(struct lblktab_t *l, unsigned int size)
{
    if (iotab_alive)
    {
#ifdef DEBUG_SS
        printk(KERN_DEBUG "PDD:iotab already exists!\n");
#endif 
        return 0;
    }   
    if(lblktab_init(&iotab, size))
        return -1;

    iotab_alive = 1;
    return 0;
}  

void lblktab_exit(struct lblktab_t *l)
{
    if (l->table != NULL)
    {
        hashtab_destroy(l->table);
    }
#ifdef DEBUG_SS
    printk(KERN_DEBUG "PDD:Successful lblktab_exit().\n");
#endif
}

#ifndef IOONLY
void scantab_exit(void)
{
    /* The check of flag scantab_alive is done to ensure that repeated
     * calls to hashtab_destroy() do not happen. In fact, the flag is
     * set to 0 immediately before the actual call to hashtab_destroy()
     * because once invoked, it might take substantial time to finish
     * (since the hashtable could be potentially huge), and in the 
     * mean time, we do not want to entertain any more invocations.
     */
    if (scantab_alive)
    {
        scantab_alive = 0;
        lblktab_exit(&scantab);
    }
}
#endif

void iotab_exit(void)
{
    /* The check of flag iotab_alive is done to ensure that repeated
     * calls to hashtab_destroy() do not happen. In fact, the flag is
     * set to 0 immediately before the actual call to hashtab_destroy()
     * because once invoked, it might take substantial time to finish
     * (since the hashtable could be potentially huge), and in the 
     * mean time, we do not want to entertain any more invocations.
     */
    if (iotab_alive)
    {
        iotab_alive = 0;
        lblktab_exit(&iotab);
    }
}

#endif	//YESHASHTAB
