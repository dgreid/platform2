// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRC32_H_
#define CRYPTOHOME_CRC32_H_

#include <limits>

#include <stdint.h>

#include <base/metrics/crc32.h>

namespace cryptohome {

static inline uint32_t Crc32(const void* buffer, uint32_t len) {
  // The base::Crc32 method should really be called "UpdateCRC". Initial
  // checksum needs to be initialized to all 1's; final value is 1's
  // complement.
  // See https://www.w3.org/TR/PNG/#D-CRCAppendix.
  // TODO(b/168049518): Move to libchrome.
  return ~base::Crc32(std::numeric_limits<uint32_t>::max(), buffer, len);
}

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRC32_H_
