/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 * 
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_ASM_ATOMIC_H_
#define VERITY_INCLUDE_ASM_ATOMIC_H_

#include <linux/types.h>

/* uses gcc built-ins */
int atomic_cmpxchg(atomic_t *a, int oldval, int newval);
void atomic_set(atomic_t *a, int newval);
int atomic_read(atomic_t *a);

#endif  /* VERITY_INCLUDE_ASM_ATOMIC_H_ */
