// Copyright 2026, haiku-odroid-c2 port authors. MIT License.
// The little of Haiku's SupportDefs.h that IRDecoder needs, for the host
#ifndef _SUPPORT_DEFS_H
#define _SUPPORT_DEFS_H
#include <stdint.h>
#include <stddef.h>
typedef int32_t int32;
typedef uint8_t uint8;
typedef uint32_t uint32;
typedef int64_t int64;
typedef int64_t bigtime_t;
#define B_COUNT_OF(a) (sizeof(a) / sizeof(a[0]))
#endif
