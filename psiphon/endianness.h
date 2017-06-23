#ifndef _ENDIANNESS_H_
#define _ENDIANNESS_H_

#include <asm/types.h>
#include <byteswap.h>

#define ENDIAN_MAGIC  0x65617400 /* same magic as blktrace_api.h */
#define ENDIAN_VERSION    (0x07)

#define CHECK_MAGIC(t)      ((magic & 0xffffff00) == ENDIAN_MAGIC)

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define be16_to_cpu(x)      __bswap_16(x)
#define be32_to_cpu(x)      __bswap_32(x)
#define be64_to_cpu(x)      __bswap_64(x)
#define cpu_to_be16(x)      __bswap_16(x)
#define cpu_to_be32(x)      __bswap_32(x)
#define cpu_to_be64(x)      __bswap_64(x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define be16_to_cpu(x)      (x)
#define be32_to_cpu(x)      (x)
#define be64_to_cpu(x)      (x)
#define cpu_to_be16(x)      (x)
#define cpu_to_be32(x)      (x)
#define cpu_to_be64(x)      (x)
#else
#error "Bad arch" 
#endif

int verify_trace(__u32 magic);

#endif /* _ENDIANNESS_H_ */
