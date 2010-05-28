/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 * 
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_MEMPOOL_H_
#define VERITY_INCLUDE_LINUX_MEMPOOL_H_
#include <sys/types.h>

typedef struct {
	int min_nr;
	int out;
} mempool_t;

mempool_t *mempool_create_page_pool(int min_nr, int order);
void mempool_destroy(mempool_t *m);
void *mempool_alloc(mempool_t *m, int flags);
void mempool_free(void *e, mempool_t *m);

#endif  /* VERITY_INCLUDE_LINUX_MEMPOOL_H_ */
