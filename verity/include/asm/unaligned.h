/* Copyright (C) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 *
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_ASM_UNALIGNED_H_
#define VERITY_INCLUDE_ASM_UNALIGNED_H_

#include <linux/types.h>

static inline u32 __get_unaligned_be32(const u8 *p)
{
	return p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static inline u32 get_unaligned_be32(const void *p)
{
	return __get_unaligned_be32((const u8 *)p);
}

#endif  /* VERITY_INCLUDE_ASM_UNALIGNED_H_ */
