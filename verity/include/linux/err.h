/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 * 
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_ERR_H_
#define VERITY_INCLUDE_LINUX_ERR_H_

/* We don't replicated pointer error the same way in these
 * wrappers.  We can just use NULL for now. */
#define IS_ERR(ptr) (ptr == NULL)
#endif  /* VERITY_INCLUDE_LINUX_ERR_H_ */
