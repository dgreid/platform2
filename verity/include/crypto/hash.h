/*
 * Hash: Hash algorithms under the crypto API
 *
 * Copyright (c) 2008 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#ifndef VERITY_INCLUDE_CRYPTO_HASH_H_
#define VERITY_INCLUDE_CRYPTO_HASH_H_

#include <linux/crypto.h>

struct shash_desc {
  u32 flags;

  void* __ctx[] CRYPTO_MINALIGN_ATTR;
};

struct shash_alg {
  int (*init)(struct shash_desc* desc);
  int (*update)(struct shash_desc* desc, const u8* data, unsigned int len);
  int (*final)(struct shash_desc* desc, u8* out);
  int (*finup)(struct shash_desc* desc,
               const u8* data,
               unsigned int len,
               u8* out);
  int (*digest)(struct shash_desc* desc,
                const u8* data,
                unsigned int len,
                u8* out);
  int (*export)(struct shash_desc* desc, void* out);
  int (*import)(struct shash_desc* desc, const void* in);

  unsigned int descsize;

  unsigned int digestsize;
  unsigned int statesize;

  struct crypto_alg base;
};

struct hash_tfm {
  const struct shash_alg* alg;
  struct shash_desc desc;
};

static inline void* shash_desc_ctx(struct shash_desc* desc) {
  return desc->__ctx;
}

#endif  // VERITY_INCLUDE_CRYPTO_HASH_H_
