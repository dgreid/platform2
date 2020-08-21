// NOLINT(legal/copyright)

#ifndef VERITY_INCLUDE_CRYPTO_MD5_H_
#define VERITY_INCLUDE_CRYPTO_MD5_H_

#include <linux/types.h>

#define MD5_DIGEST_SIZE 16
#define MD5_HMAC_BLOCK_SIZE 64
#define MD5_BLOCK_WORDS 16
#define MD5_HASH_WORDS 4

struct md5_state {
  u32 hash[MD5_HASH_WORDS];
  u32 block[MD5_BLOCK_WORDS];
  u64 byte_count;
};

#endif  // VERITY_INCLUDE_CRYPTO_MD5_H_
