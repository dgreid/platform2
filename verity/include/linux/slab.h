/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 * 
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_SLAB_H_
#define VERITY_INCLUDE_LINUX_SLAB_H_
#include <sys/types.h>

void *kzalloc(size_t sz, int flags);
void *kcalloc(size_t n, size_t sz, int flags);
void kfree(void *x);

#endif  /* VERITY_INCLUDE_LINUX_SLAB_H_ */
