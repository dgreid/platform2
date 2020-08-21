/* Copyright (C) 2012 The Chromium OS Authors. All rights reserved.
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

#include_next <linux/types.h>

typedef __u8 u8;
typedef __u16 u16;
typedef __u32 u32;
typedef __u64 u64;
/* Assume CONFIG_LBDAF */
typedef __u64 sector_t;

#endif /* VERITY_INCLUDE_LINUX_TYPES_H_ */
