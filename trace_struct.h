#ifndef _TRACE_STRUCT_H_
#define _TRACE_STRUCT_H_

#define ENDIAN_MAGIC  0x65617400 /* same magic as blktrace_api.h */
#define ENDIAN_VERSION    (0x07)

#define CHECK_MAGIC(t)      ((magic & 0xffffff00) == ENDIAN_MAGIC)

struct trace_event_element
{
	__u32 magic;
    __u32 elt_len;  /* length of data in next trace element */
};

#endif
