/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 * 
 * These files provide equivalent implementations for kernel calls for
 * compatibility with files under SRC/include.
 */

#include <stdlib.h>

#include <asm/bug.h>
#include <linux/scatterlist.h>

void sg_init_table(struct scatterlist *sg, int pages) 
{
	/* Pages is ignored at present. */
	if (pages > 1) abort();
	sg->buffer = NULL;
	sg->length = 0;
	sg->offset = 0;
}

void sg_set_buf(struct scatterlist *sg, const void *buf, unsigned int len)
{
	sg->buffer = buf;
	sg->length = len;
	sg->offset = 0;
}

void sg_init_one(struct scatterlist *sg, const void *buf, unsigned int len)
{
	sg_init_table(sg, 1);
	sg_set_buf(sg, buf, len);
}


void sg_set_page(struct scatterlist *sg, struct page *page,
		 unsigned int len, unsigned int offset)
{
	sg_set_buf(sg, page->data, len);
	sg->offset = offset;
}

void sg_destroy(struct scatterlist *sg)
{
	sg_init_table(sg, 0);
}
