// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Extra utility functions
#include "verity/utils.h"

#include <stdio.h>

namespace verity_utils {

void to_hex(char* hexdigest,
            const uint8_t* digest,
            unsigned int digest_length) {
  for (unsigned int i = 0; i < digest_length; i++) {
    // NOLINTNEXTLINE(runtime/printf)
    sprintf(hexdigest + (2 * i), "%02x", static_cast<int>(digest[i]));
  }
}

}  // namespace verity_utils
