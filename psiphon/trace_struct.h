#ifndef _TRACE_STRUCT_H_
#define _TRACE_STRUCT_H_

struct trace_event_element
{
	__u32 magic;
    __u32 elt_len;  /* length of data in next trace element */
};

#endif
