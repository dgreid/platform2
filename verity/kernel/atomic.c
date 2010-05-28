/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 * 
 * These files provide equivalent implementations for kernel calls for
 * compatibility with files under SRC/include.
 */

#include <asm/atomic.h>

int atomic_cmpxchg(atomic_t *a, int oldval, int newval)
{
	return __sync_val_compare_and_swap(&(a->counter), oldval, newval);
}

void atomic_set(atomic_t *a, int newval)
{
	a->counter = newval;
}

int atomic_read(atomic_t *a)
{
	return a->counter;
}
