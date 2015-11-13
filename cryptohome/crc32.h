/* Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef CRYPTOHOME_CRC32_H_
#define CRYPTOHOME_CRC32_H_

#include <stdint.h>

uint32_t Crc32(const void *buffer, uint32_t len);

#endif  /* CRYPTOHOME_CRC32_H_ */
