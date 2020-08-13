/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 *
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_BUG_H_
#define VERITY_INCLUDE_LINUX_BUG_H_
#include <stdio.h>
#include <stdlib.h>

#define BUG() abort()
#define BUG_ON(val)                                                      \
  {                                                                      \
    if (val) {                                                           \
      fprintf(stderr, "!! %s:%s:%i: BUG_ON: %s\n\n", __FILE__, __func__, \
              __LINE__, #val);                                           \
      abort();                                                           \
    }                                                                    \
  }

#endif  // VERITY_INCLUDE_LINUX_BUG_H_
