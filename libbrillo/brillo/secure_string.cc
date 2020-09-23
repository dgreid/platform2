// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/secure_string.h"

#include <cstdint>

#include <openssl/crypto.h>

namespace brillo {

BRILLO_DISABLE_ASAN void SecureClear(void* v, size_t n) {
  OPENSSL_cleanse(v, n);
}

int SecureMemcmp(const void* s1, const void* s2, size_t n) {
  const uint8_t* us1 = reinterpret_cast<const uint8_t*>(s1);
  const uint8_t* us2 = reinterpret_cast<const uint8_t*>(s2);
  int result = 0;

  if (0 == n)
    return 0;

  /* Code snippet without data-dependent branch due to
   * Nate Lawson (nate@root.org) of Root Labs. */
  while (n--)
    result |= *us1++ ^ *us2++;

  return result != 0;
}

}  // namespace brillo
