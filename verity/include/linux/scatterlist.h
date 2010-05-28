/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 * 
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_SCATTERLIST_H_
#define VERITY_INCLUDE_LINUX_SCATTERLIST_H_
#include <asm/page.h>
#include <stdio.h>
#include <stdlib.h>

/* We only support one page. */
struct scatterlist {
	const void *buffer;
	size_t length;
	size_t offset;
};

void sg_init_table(struct scatterlist *sg, int pages);
void sg_set_page(struct scatterlist *sg, struct page *page,
		 unsigned int len, unsigned int offset);
void sg_set_buf(struct scatterlist *sg, const void *buf, unsigned int len);
void sg_init_one(struct scatterlist *sg, const void *buf, unsigned int len);
/* Non-standard.  Only since we aren't playing tricks with the buffers addrs */
void sg_destroy(struct scatterlist *sg);

#endif  /* VERITY_INCLUDE_LINUX_SCATTERLIST_ */
