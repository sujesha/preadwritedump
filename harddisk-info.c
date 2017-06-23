/*Accesing filesystem to get major/minor number of the input device/harddisk */

#include <linux/blkdev.h>       /* request_queue_t, Sector */
#include <linux/root_dev.h>
#include "harddisk-info.h"

/* 
You should be aware that that you should avoid file I/O when possible. The main idea is to go "one level deeper" and call VFS level functions instead of the syscall handler directly:
 */
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>

/* major and minor of the input device to be profiled */
unsigned int gmajor;
unsigned int gminor;
	
/* Opening a file (similar to open): */
struct file* file_open(const char* path, int flags, int rights) {
    struct file* filp = NULL;
    mm_segment_t oldfs;
    int err = 0;

    oldfs = get_fs();
    set_fs(get_ds());
    filp = filp_open(path, flags, rights);
    set_fs(oldfs);
    if(IS_ERR(filp)) {
        err = PTR_ERR(filp);
        return NULL;
    }
    return filp;
}


/* Close a file (similar to close): */

void file_close(struct file* file) {
    filp_close(file, NULL);
}

/* Reading data from a file (similar to pread): */

int file_read(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = vfs_read(file, data, size, &offset);

    set_fs(oldfs);
    return ret;
}

/* Writing data to a file (similar to pwrite):*/

int file_write(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = vfs_write(file, data, size, &offset);

    set_fs(oldfs);
    return ret;
}


static inline int isdigit(int ch)
{
    return (ch >= '0') && (ch <= '9');
}

static dev_t try_name(char *name, int part)
{
  char path[64];
  char buf[32];
  int range;
  dev_t res;
  char *s;
  int len;
  struct file *fd;
  unsigned int maj, min;
  char tempdev[4];

    mm_segment_t old_fs = get_fs();
    set_fs(KERNEL_DS);

  /* read device number from .../dev */
  sprintf(path, "/sys/block/%s/dev", name);
  printk("Trying /sys/block/%s/dev", name);
  fd = file_open(path, 0, 0);
  if (fd < 0 || fd == NULL)
    goto next;
  len = file_read(fd, 0, buf, 32);
  file_close(fd);
  if (len <= 0 || len == 32 || buf[len - 1] != '\n')
    goto fail;
  buf[len - 1] = '\0';
  if (sscanf(buf, "%u:%u", &maj, &min) == 2) {
    gmajor = maj;
    gminor = min;
    /*
     * Try the %u:%u format -- see print_dev_t()
     */
    res = MKDEV(maj, min);
    if (maj != MAJOR(res) || min != MINOR(res))
      goto fail;
  } else {
    /*
     * Nope.  Try old-style "0321"
     */
    res = new_decode_dev(simple_strtoul(buf, &s, 16));
    if (*s)
      goto fail;
  }
  goto proceed;

    /************ /sys/block/sdb/device/block/sdb/sdb1/dev ******/
next:
    strncpy(tempdev, name, 3);
    tempdev[3]='\0';
    sprintf(path, "/sys/block/%s/device/block/%s/%s/dev", tempdev,tempdev,name);
	printk("Trying /sys/block/%s/device/block/%s/%s/dev", tempdev,tempdev,name);
  fd = file_open(path, 0, 0);
  if (fd < 0 || fd == NULL)
    goto fail;
  len = file_read(fd, 0, buf, 32);
  file_close(fd);
  if (len <= 0 || len == 32 || buf[len - 1] != '\n')
    goto fail;
  buf[len - 1] = '\0';
  if (sscanf(buf, "%u:%u", &maj, &min) == 2) {
    gmajor = maj;
    gminor = min;
    /*
     * Try the %u:%u format -- see print_dev_t()
     */
    res = MKDEV(maj, min);
    if (maj != MAJOR(res) || min != MINOR(res))
      goto fail;
	printk("PDD: gmajor=%d, gminor=%d\n", gmajor, gminor);
  } else {
    /*
     * Nope.  Try old-style "0321"
     */
    res = new_decode_dev(simple_strtoul(buf, &s, 16));
    if (*s)
      goto fail;
  }

proceed:
  /* if it's there and we are not looking for a partition - that's it */
  if (!part)
    return res;

  /* otherwise read range from .../range */
  sprintf(path, "/sys/block/%s/range", name);
  fd = file_open(path, 0, 0);
  if (fd < 0 || fd == NULL)
    goto fail;
  len = file_read(fd, 0, buf, 32);
  file_close(fd);
  if (len <= 0 || len == 32 || buf[len - 1] != '\n')
    goto fail;
  buf[len - 1] = '\0';
  range = simple_strtoul(buf, &s, 10);
  if (*s)
    goto fail;

    set_fs(old_fs);

  /* if partition is within range - we got it */
  if (part < range)
    return res + part;
fail:
  return 0;
}

/* Borrowed from do_mounts.c in 2.6.27-14-generic */
dev_t name_to_dev_t(char *name)
{
    char s[32];
    char *p;
    dev_t res = 0;
    int part;

    if (strncmp(name, "/dev/", 5) != 0) {
        unsigned maj, min;

        if (sscanf(name, "%u:%u", &maj, &min) == 2) {
            res = MKDEV(maj, min);
            if (maj != MAJOR(res) || min != MINOR(res))
                goto fail;
        } else {
            res = new_decode_dev(simple_strtoul(name, &p, 16));
            if (*p)
                goto fail;
        }
        goto done;
    }

    name += 5;
    res = Root_NFS;
    if (strcmp(name, "nfs") == 0)
        goto done;
    res = Root_RAM0;
    if (strcmp(name, "ram") == 0)
        goto done;

    if (strlen(name) > 31)
        goto fail;
    strcpy(s, name);
    for (p = s; *p; p++)
        if (*p == '/')
            *p = '!';
    res = try_name(s, 0);
    if (res)
        goto done;
    /*
     * try non-existant, but valid partition, which may only exist
     * after revalidating the disk, like partitioned md devices
     */
    while (p > s && isdigit(p[-1]))
        p--;
    if (p == s || !*p || *p == '0')
        goto fail;

    /* try disk name without <part number> */
    part = simple_strtoul(p, NULL, 10);
    *p = '\0';
    res = try_name(s, part);
    if (res)
        goto done;

    /* try disk name without p<part number> */
    if (p < s + 2 || !isdigit(p[-2]) || p[-1] != 'p')
        goto fail;
    p[-1] = '\0';
    res = try_name(s, part);
    if (res)
        goto done;
fail:
    return 0;
done:
    return res;
}

