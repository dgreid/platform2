/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 * 
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_TYPES_H_
#define VERITY_INCLUDE_LINUX_TYPES_H_

#include <stdbool.h>
#include <sys/types.h>

/* Since we're shadowing linux/types.h we should pull in asm if we can */
#include <asm/types.h>

typedef u_int8_t u8;
typedef u_int32_t u32;
typedef u_int64_t u64;
/* Assume CONFIG_LBDAF */
typedef u64 sector_t;

/* Not a tested atomic implementation, but enough for testing and generating
 * trees in a single-threaded capacity. */
typedef struct {
	volatile int counter;
} atomic_t;

#define NR_CPUS 4 /* TODO(msb) put this in the proper header file */

#endif  /* VERITY_INCLUDE_LINUX_TYPES_H_ */
