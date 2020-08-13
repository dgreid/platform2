/* Copyright (C) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 *
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_CRYPTO_H_
#define VERITY_INCLUDE_LINUX_CRYPTO_H_

#include <linux/string.h>
#include <linux/kernel.h>

#define CRYPTO_ALG_TYPE_SHASH 0

#define CRYPTO_MINALIGN_ATTR __attribute__((__aligned__(32)))
#define CRYPTO_MAX_ALG_NAME 64

#include <linux/types.h>

struct crypto_alg {
  u32 cra_flags;
  unsigned int cra_blocksize;

  char cra_name[CRYPTO_MAX_ALG_NAME];
  char cra_driver_name[CRYPTO_MAX_ALG_NAME];

  void* cra_module;
};

struct hash_tfm;

struct hash_desc {
  struct hash_tfm* tfm;
};

struct hash_tfm* crypto_alloc_hash(const char* alg_name, int a, int b);
void crypto_free_hash(struct hash_tfm* tfm);
unsigned int crypto_hash_digestsize(struct hash_tfm* tfm);
int crypto_hash_init(struct hash_desc* h);
int crypto_hash_update(struct hash_desc* h,
                       const u8* buffer,
                       unsigned int size);
int crypto_hash_final(struct hash_desc* h, u8* dst);

#endif /* VERITY_INCLUDE_LINUX_CRYPTO_H_ */
