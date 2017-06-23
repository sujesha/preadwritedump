#ifndef _HARDDISK_INFO_H_
#define _HARDDISK_INFO_H_

struct file* file_open(const char* path, int flags, int rights);
void file_close(struct file* file);
int file_read(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size);
int file_write(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size);

dev_t name_to_dev_t(char *name);

#endif /* _HARDDISK_INFO_H_ */
