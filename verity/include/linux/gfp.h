/* Copyright (C) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 *
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_GFP_H_
#define VERITY_INCLUDE_LINUX_GFP_H_

#include <stdlib.h>

#define GFP_KERNEL 0
#define GFP_NOIO 1

static inline struct page *alloc_page(int flags)
{
	struct page *memptr;

	if (posix_memalign((void **)&memptr,
			   sizeof(struct page),
			   sizeof(struct page)))
	    return NULL;
	return memptr;
}

static inline void __free_page(struct page *page)
{
	free(page);
}

#endif  /* VERITY_INCLUDE_LINUX_GFP_H_ */
