/* Copyright (C) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 *
 * These files provide equivalent implementations for kernel calls for
 * compatibility with files under SRC/include.
 */

#include <linux/bug.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <crypto/hash.h>

static size_t num_hashes = 0;
static struct shash_alg** hashes = NULL;

int crypto_register_shash(struct shash_alg* alg) {
  hashes = realloc(hashes, sizeof(*hashes) * (num_hashes + 1));
  hashes[num_hashes++] = alg;
  return 0;
}

struct hash_tfm* crypto_alloc_hash(const char* alg_name, int a, int b) {
  size_t i;
  struct hash_tfm* tfm;

  if (!hashes) {
    /* need to initialize our world */
    CALL_MODULE_INIT(md5_mod_init);
    CALL_MODULE_INIT(sha1_generic_mod_init);
    CALL_MODULE_INIT(sha256_generic_mod_init);
  }
  BUG_ON(!hashes);

  for (i = 0; i < num_hashes; ++i)
    if (!strcasecmp(alg_name, hashes[i]->base.cra_name))
      break;
  BUG_ON(i == num_hashes);

  tfm = calloc(sizeof(*tfm) + hashes[i]->statesize, 1);
  BUG_ON(!tfm);
  tfm->alg = hashes[i];

  return tfm;
}

void crypto_free_hash(struct hash_tfm* tfm) {
  free(tfm);
}

unsigned int crypto_hash_digestsize(struct hash_tfm* tfm) {
  BUG_ON(!tfm);
  return tfm->alg->digestsize;
}

int crypto_hash_init(struct hash_desc* h) {
  const struct shash_alg* alg = h->tfm->alg;
  struct shash_desc* desc = &h->tfm->desc;

  return alg->init(desc);
}

int crypto_hash_update(struct hash_desc* h,
                       const u8* buffer,
                       unsigned int size) {
  const struct shash_alg* alg = h->tfm->alg;
  struct shash_desc* desc = &h->tfm->desc;

  return alg->update(desc, buffer, size);
}

int crypto_hash_final(struct hash_desc* h, u8* dst) {
  const struct shash_alg* alg = h->tfm->alg;
  struct shash_desc* desc = &h->tfm->desc;

  return alg->final(desc, dst);
}
