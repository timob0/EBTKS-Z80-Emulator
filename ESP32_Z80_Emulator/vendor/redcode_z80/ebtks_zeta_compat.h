/* EBTKS minimal portability adapter for the redcode/Z80 C engine.
 * Intended ONLY for little-endian 8-bit-byte ESP32 Xtensa and host x86.
 * Upstream supports Z80_EXTERNAL_HEADER to supply these types/macros.
 * Keep the upstream Z80.c/Z80.h unmodified and separately licensed.
 */
#ifndef EBTKS_ZETA_COMPAT_H
#define EBTKS_ZETA_COMPAT_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
# error "EBTKS redcode Z80 adapter requires a little-endian target"
#endif
#if CHAR_BIT != 8
# error "EBTKS redcode Z80 adapter requires 8-bit bytes"
#endif
typedef uint8_t zuint8;
typedef int8_t zsint8;
/* The upstream Zeta integral.h exposes these natural C scalar types.
 * Omission caused cascades of REGISTER_16/INX/OUTX parser errors. */
typedef char zchar;
typedef unsigned int zuint;
typedef int zsint;
typedef uint16_t zuint16;
typedef uint32_t zuint32;
typedef size_t zusize;
typedef uint8_t zbool;
typedef union {
    uint16_t uint16_value;
    struct { uint8_t at_0, at_1; } uint8_values;
} ZInt16;
typedef union {
    uint32_t uint32_value;
    uint16_t uint16_array[2];
    uint8_t uint8_array[4];
} ZInt32;
#define Z_NULL ((void *)0)
#define Z_TRUE ((zbool)1)
#define Z_FALSE ((zbool)0)
#define Z_EMPTY
#define Z_UNUSED(expr) ((void)(expr));
#define Z_CAST(type) (type)
#define Z_USIZE(v) ((zusize)(v))
#define Z_USIZE_MAXIMUM SIZE_MAX
#define Z_UINT16(v) UINT16_C(v)
#define Z_UINT32(v) UINT32_C(v)
#define Z_UINT16_BIG_ENDIAN(v) __builtin_bswap16((zuint16)(v))
#define Z_UINT32_BIG_ENDIAN(v) __builtin_bswap32((zuint32)(v))
#define Z_UINT8_ROTATE_LEFT(v, n) ((zuint8)((((zuint8)(v)) << (n)) | (((zuint8)(v)) >> (8-(n)))))
#define Z_UINT8_ROTATE_RIGHT(v, n) ((zuint8)((((zuint8)(v)) >> (n)) | (((zuint8)(v)) << (8-(n)))))
#define Z_MEMBER_OFFSET(type, member) offsetof(type, member)
#define Z_MICROSOFT_STD_CALL
#define Z_ALWAYS_INLINE inline __attribute__((always_inline))
#define Z_API_EXPORT
#define Z_API_IMPORT
#ifdef __cplusplus
# define Z_EXTERN_C_BEGIN extern "C" {
# define Z_EXTERN_C_END }
#else
# define Z_EXTERN_C_BEGIN
# define Z_EXTERN_C_END
#endif
#endif /* EBTKS_ZETA_COMPAT_H */
