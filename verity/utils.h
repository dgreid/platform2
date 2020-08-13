// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Extra utility functions
#ifndef VERITY_UTILS_H__
#define VERITY_UTILS_H__ 1

#include <stdint.h>
#include <sys/types.h>

namespace verity_utils {
// A shabby function to convert an arbitrarily long digest to hex.
// This needs to be replace FOR SPEED.
// Note: hexdigest must be 2*digest_length+i long.
void to_hex(char* hexdigest, const uint8_t* digest, unsigned int digest_length);

}  // namespace verity_utils

#endif  // VERITY_UTILS_H__
