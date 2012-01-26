/* Copyright (C) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 *
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_PERCPU_H_
#define VERITY_INCLUDE_LINUX_PERCPU_H_

#define DEFINE_PER_CPU(type, name) __typeof__(type) name

#define __get_cpu_var(name) name
#define get_cpu_var(name) *(&__get_cpu_var(name))

#define put_cpu_var(name) (void)(name)

#endif  /* VERITY_INCLUDE_LINUX_PERCPU_H_ */
