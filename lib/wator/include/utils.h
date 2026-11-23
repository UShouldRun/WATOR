/**
 * @file utils.h
 * @brief Fixed-width integer and float typedefs
 *
 * Shared aliases used across every translation unit. Bare int / long /
 * unsigned are never used for data; always pick the exact width.
 *
 *  u8  u16  u32  u64   unsigned
 *  i8  i16  i32  i64   signed
 *  f32 f64            float
 */
#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;

typedef float    f32;
typedef double   f64;

#endif // !__UTILS_H__
