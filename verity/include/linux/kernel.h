/* Copyright (C) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 *
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_KERNEL_H_
#define VERITY_INCLUDE_LINUX_KERNEL_H_

#include_next <linux/kernel.h>

#define ALIGN(x, a) __ALIGN_MASK(x, (typeof(x))(a)-1)
#define __ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))
#define IS_ALIGNED(x, a) (((x) & ((typeof(x))(a)-1)) == 0)

#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))

#define MIN(x, y)                  \
  ({                               \
    typeof(x) _min1 = (x);         \
    typeof(y) _min2 = (y);         \
    (void)(&_min1 == &_min2);      \
    _min1 < _min2 ? _min1 : _min2; \
  })

#define MAX(x, y)                  \
  ({                               \
    typeof(x) _max1 = (x);         \
    typeof(y) _max2 = (y);         \
    (void)(&_max1 == &_max2);      \
    _max1 > _max2 ? _max1 : _max2; \
  })

#endif /* VERITY_INCLUDE_LINUX_KERNEL_H_ */
