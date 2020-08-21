/* Copyright (C) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 *
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_CRYPTO_INTERNAL_HASH_H_
#define VERITY_INCLUDE_CRYPTO_INTERNAL_HASH_H_

#include <crypto/hash.h>

int crypto_register_shash(struct shash_alg* alg);
static inline int crypto_unregister_shash(struct shash_alg* alg) {
  return 0;
}

#endif  // VERITY_INCLUDE_CRYPTO_INTERNAL_HASH_H_
