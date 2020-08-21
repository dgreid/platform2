/* Copyright (C) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 *
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_INIT_H_
#define VERITY_INCLUDE_LINUX_INIT_H_

#define __init __attribute__((constructor))
#define __exit __attribute__((unused))

#define module_init(x) \
  int mod_init_##x(void) { return x(); }
#define module_exit(x)

#define CALL_MODULE_INIT(x)        \
  do {                             \
    extern int mod_init_##x(void); \
    mod_init_##x();                \
  } while (0)

#endif /* VERITY_INCLUDE_LINUX_INIT_H_ */
