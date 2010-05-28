/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 * 
 * These files provide equivalent implementations for kernel calls for
 * compatibility with files under SRC/include.
 */

#include <asm/page.h>
#include <linux/mempool.h>
#include <stdio.h>
#include <stdlib.h>

mempool_t *mempool_create_page_pool(int min_nr, int order)
{
	mempool_t *m = (mempool_t *) calloc(1, sizeof(mempool_t));
	if (!m) return m;
	m->min_nr = min_nr;
	return m;
}

void mempool_destroy(mempool_t *m) 
{
	if (!m) return;
	if (m->out > 0) {
		fprintf(stderr, "ALL ELEMENTS NOT RETURNED TO MEMPOOL\n");
	}
	free(m);
}

void *mempool_alloc(mempool_t *m, int flags) 
{
	m->out++;
	return calloc(1, sizeof(struct page));
}

void mempool_free(void *e, mempool_t *m) 
{
	m->out--;
	free(e);
}
