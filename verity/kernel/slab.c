/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 * 
 * These files provide equivalent implementations for kernel calls for
 * compatibility with files under SRC/include.
 */

#include <linux/slab.h>
#include <stdlib.h>

void *kzalloc(size_t sz, int flags) 
{
	return calloc(1, sz);
}

void *kcalloc(size_t n, size_t sz, int flags) 
{
	return calloc(n, sz);
}

void kfree(void *x) 
{
	if (x)
		free(x);
}
