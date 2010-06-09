/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 * 
 * These files provide equivalent implementations for kernel calls for
 * compatibility with files under SRC/include.
 */

#include <linux/bug.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <openssl/evp.h>

/* Simple static global for counting crypto references.  This let's us clean up
 * global OpenSSL state when the last one is freed. */
static unsigned int crypto_refs = 0;

struct hash_tfm *crypto_alloc_hash(const char *alg_name, int a, int b) 
{
	struct hash_tfm *tfm =
	  (struct hash_tfm *) calloc(sizeof(struct hash_tfm), 1);
        if (!crypto_refs++)
		OpenSSL_add_all_digests();
	tfm->impl = EVP_get_digestbyname(alg_name);
	EVP_MD_CTX_init(&tfm->ctx);
	return tfm;
}

void crypto_free_hash(struct hash_tfm *tfm) 
{
	if (tfm) {
		free(tfm);
		if (--crypto_refs == 0)
			EVP_cleanup();
	}
}

unsigned int crypto_hash_digestsize(struct hash_tfm *tfm) 
{
	unsigned int len;
	BUG_ON(!tfm);
	/* Must be initialized before we can get a size */
	len = EVP_MD_size(tfm->impl);
	return len;
}

int crypto_hash_init(struct hash_desc *h)
{
	EVP_MD_CTX_init(&h->tfm->ctx);
	EVP_DigestInit_ex(&h->tfm->ctx, h->tfm->impl, NULL);
	return 0;
}

int crypto_hash_digest(struct hash_desc *h, struct scatterlist *sg,
		       unsigned int sz, u8 *dst)
{
	unsigned int available = 0;
	char *buffer = (char *)(sg->buffer) + sg->offset;
	EVP_DigestUpdate(&h->tfm->ctx, buffer, sg->length - sg->offset);
	EVP_DigestFinal_ex(&h->tfm->ctx, dst, &available);
	EVP_MD_CTX_cleanup(&h->tfm->ctx);
	BUG_ON(available > sz);
	return 0;
}

int crypto_hash_update(struct hash_desc *h, struct scatterlist *sg,
		       unsigned int size)
{
	char *buffer = (char *)(sg->buffer) + sg->offset;
	EVP_DigestUpdate(&h->tfm->ctx, buffer, sg->length - sg->offset);
	return 0;
}

int crypto_hash_final(struct hash_desc *h, u8 *dst) {
	unsigned int available = 0;
	EVP_DigestFinal_ex(&h->tfm->ctx, dst, &available);
	EVP_MD_CTX_cleanup(&h->tfm->ctx);
	return 0;
}
