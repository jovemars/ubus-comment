/*
 * utils - misc libubox utility functions
 *
 * Copyright (C) 2012 Felix Fietkau <nbd@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __LIBUBOX_UTILS_H
#define __LIBUBOX_UTILS_H

#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/*
 * calloc_a(size_t len, [void **addr, size_t len,...], NULL)
 *
 * allocate a block of memory big enough to hold multiple aligned objects.
 * the pointer to the full object (starting with the first chunk) is returned,
 * all other pointers are stored in the locations behind extra addr arguments.
 * the last argument needs to be a NULL pointer
 */

#define calloc_a(len, ...) __calloc_a(len, ##__VA_ARGS__, NULL)

void *__calloc_a(size_t len, ...);

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#define __BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

#ifdef __OPTIMIZE__
extern int __BUILD_BUG_ON_CONDITION_FAILED;
#define BUILD_BUG_ON(condition)                    \
    do {                            \
        __BUILD_BUG_ON(condition);            \
        if (condition)                    \
            __BUILD_BUG_ON_CONDITION_FAILED = 1;    \
    } while(0)
#else
#define BUILD_BUG_ON __BUILD_BUG_ON
#endif

#ifdef __APPLE__

#define CLOCK_REALTIME    0
#define CLOCK_MONOTONIC    1

void clock_gettime(int type, struct timespec *tv);

#endif

#ifdef __GNUC__
#define _GNUC_MIN_VER(maj, min) (((__GNUC__ << 8) + __GNUC_MINOR__) >= (((maj) << 8) + (min)))
#else
#define _GNUC_MIN_VER(maj, min) 0
#endif

#if defined(__linux__) || defined(__CYGWIN__)
#include <byteswap.h>
#include <endian.h>

#elif defined(__APPLE__)
#include <machine/endian.h>
#include <machine/byte_order.h>
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)
#elif defined(__FreeBSD__)
#include <sys/endian.h>
#define bswap_32(x) bswap32(x)
#define bswap_64(x) bswap64(x)
#else
#include <machine/endian.h>
#define bswap_32(x) swap32(x)
#define bswap_64(x) swap64(x)
#endif

#ifndef __BYTE_ORDER
#define __BYTE_ORDER BYTE_ORDER
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN
#endif
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#endif

static inline uint16_t __u_bswap16(uint16_t val)
{
    // right shift 8 bits to keep the higher byte
    // left shift 8 bits to keep the lower byte
    // bitwise-OR to combine them together
    // for example, 0xaabb becomes 0xbbaa
    return ((val >> 8) & 0xffu) | ((val & 0xffu) << 8);
}

#if _GNUC_MIN_VER(4, 2)
// GNUC extensive built-in function
// __builtin_bswap32(x): Returns x with the order of the bytes reversed; 
//                       for example, 0xaabbccdd becomes 0xddccbbaa. 
//                       Byte here always means exactly 8 bits. 
// __builtin_bswap64(x): Similar to __builtin_bswap32, except the argument
//                       and return types are 64-bit. 
//                       for example, 0x8899aabbccddeeff becomes 0xffeeddccbbaa9988
#define __u_bswap32(x) __builtin_bswap32(x)
#define __u_bswap64(x) __builtin_bswap64(x)
#else
// #include <byteswap.h>
// bswap_16(x);
// bswap_32(x);
// bswap_64(x);
// Desc: These macros are GNU extensions.
//       These macros return a value in which the order of the bytes in their
//       2-, 4-, or 8-byte arguments is reversed.
#define __u_bswap32(x) bswap_32(x)
#define __u_bswap64(x) bswap_64(x)
#endif

// #include <endian.h>
// uint16_t htobe16(uint16_t host_16bits);
// uint16_t htole16(uint16_t host_16bits);
// uint16_t be16toh(uint16_t big_endian_16bits);
// uint16_t le16toh(uint16_t little_endian_16bits);
// uint32_t htobe32(uint32_t host_32bits);
// uint32_t htole32(uint32_t host_32bits);
// uint32_t be32toh(uint32_t big_endian_32bits);
// uint32_t le32toh(uint32_t little_endian_32bits);
// uint64_t htobe64(uint64_t host_64bits);
// uint64_t htole64(uint64_t host_64bits);
// uint64_t be64toh(uint64_t big_endian_64bits);
// uint64_t le64toh(uint64_t little_endian_64bits);
// Desc: NONstandard. These functions convert the byte encoding of integer values from the
//       byte order that the current CPU (the "host") uses, to and from
//       little-endian and big-endian byte order.
//       The number, 16, 32 or 64, in the name of each function indicates the size of
//       integer handled by the function.
//       "be" means big endian, "le" means little endian

#if __BYTE_ORDER == __LITTLE_ENDIAN

#define cpu_to_be64(x) __u_bswap64(x)
#define cpu_to_be32(x) __u_bswap32(x)
#define cpu_to_be16(x) __u_bswap16((uint16_t) (x))

#define be64_to_cpu(x) __u_bswap64(x)
#define be32_to_cpu(x) __u_bswap32(x)
#define be16_to_cpu(x) __u_bswap16((uint16_t) (x))

#define cpu_to_le64(x) (x)
#define cpu_to_le32(x) (x)
#define cpu_to_le16(x) (x)

#define le64_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define le16_to_cpu(x) (x)

#else /* __BYTE_ORDER == __LITTLE_ENDIAN */

#define cpu_to_le64(x) __u_bswap64(x)
#define cpu_to_le32(x) __u_bswap32(x)
#define cpu_to_le16(x) __u_bswap16((uint16_t) (x))

#define le64_to_cpu(x) __u_bswap64(x)
#define le32_to_cpu(x) __u_bswap32(x)
#define le16_to_cpu(x) __u_bswap16((uint16_t) (x))

#define cpu_to_be64(x) (x)
#define cpu_to_be32(x) (x)
#define cpu_to_be16(x) (x)

#define be64_to_cpu(x) (x)
#define be32_to_cpu(x) (x)
#define be16_to_cpu(x) (x)

#endif

#ifndef __packed
// The packed attribute specifies that a variable or structure field should have
// the smallest possible alignment - one byte for a variable, and one bit for
// a field, unless you specify a larger value with the aligned attribute.
#define __packed __attribute__((packed))
#endif

#ifndef __constructor
// The constructor attribute causes the function to be called automatically
// before execution enters main().
// The destructor attribute causes the function to be called automatically
// after main() completes or exit () is called.
// An optional integer priority can be provided to control the order in which
// constructor and destructor functions are run.
// A smaller priority number runs before a larger priority number;
// the opposite relationship holds for destructors.
#define __constructor __attribute__((constructor))
#endif

#ifndef __hidden
// This attribute affects the linkage of the declaration to which it is attached.
// It can be applied to variables and types as well as functions.
// There are four supported visibility_type values: default, hidden, protected or internal.
// default   - visible to other modules and, in shared libraries, may be overridden.
// hidden    - cannot be referenced directly by other modules, but can be referenced
//             indirectly via function pointers.
// internal  - never called from another module.
// protected - visible to other modules, but cannot be overridden by another module. 
#define __hidden __attribute__((visibility("hidden")))
#endif

#ifndef BITS_PER_LONG
#define BITS_PER_LONG (8 * sizeof(unsigned long))
#endif

static inline void bitfield_set(unsigned long *bits, int bit)
{
    bits[bit / BITS_PER_LONG] |= (1UL << (bit % BITS_PER_LONG));
}

static inline bool bitfield_test(unsigned long *bits, int bit)
{
    return !!(bits[bit / BITS_PER_LONG] & (1UL << (bit % BITS_PER_LONG)));
}

#endif
