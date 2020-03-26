// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/libscrypt_compat.h"

#include <openssl/aes.h>
#include <openssl/evp.h>

#include <base/bits.h>
#include <base/sys_byteorder.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>

#include "cryptohome/cryptolib.h"

namespace cryptohome {

// Callers of this library need to allocate the salt and key, so the sizes are
// exposed.
constexpr size_t kLibScryptSaltSize = 32;

constexpr size_t kLibScryptDerivedKeySize = 64;

namespace {

constexpr size_t kLibScryptHeaderSize = 96;

constexpr size_t kLibScryptSubHeaderSize = 48;

constexpr size_t kLibScryptHeaderBytesToHMAC = 64;

// Bytes 33-64 of the derived key are used for the HMAC key.
constexpr size_t kLibScryptHMACOffset = 32;

constexpr size_t kLibScryptHMACSize = 32;

constexpr size_t kLibScryptIVSize = 16;

// libscrypt places data into a uint8_t[96] array in C style. This lays it out
// as a more readable struct, but it must be tightly packed to be compatible
// with the array.
#pragma pack(push, 1)
struct LibScryptHeader {
  // This is always "scrypt".
  char magic[6];
  // This is set to 0.
  uint8_t header_reserved_byte;
  // The log base 2 of the N-factor (i.e. 10 for 1024).
  uint8_t log_n;
  // The r and p params used to generate this key.
  uint32_t r_factor;
  uint32_t p_factor;
  // A salt which is unique to each encryption. Note that this is a bit odd and
  // in new scrypt code it's better to use a unique *nonce* in the AES
  // encryption.
  uint8_t salt[32];
  // This is a checksum of the first 48 bytes of the header (all fields up to
  // and including the salt).
  uint8_t check_sum[16];
  // This is an HMAC over the first 64 bytes of the header (all fields up to and
  // including the check_sum). Why there is a check_sum and an HMAC is
  // confusing, since they cover the same data. But the key given to the HMAC is
  // the last 32 bytes of the |derived_key|, and so it verifies that the
  // password is the proper passsord for this encrypted blob.
  uint8_t signature[kLibScryptHMACSize];
};
#pragma pack(pop)

static_assert(sizeof(LibScryptHeader) == kLibScryptHeaderSize,
              "LibScryptHeader struct is packed wrong and will not be byte "
              "compatible with existing data");

// This generates the header which is specific to libscrypt. It's inserted at
// the beginning |output|.
void GenerateHeader(const brillo::SecureBlob& salt,
                    const brillo::SecureBlob& derived_key,
                    const ScryptParameters& params,
                    LibScryptHeader* header_struct) {
  DCHECK_EQ(kLibScryptSaltSize, salt.size());

  *header_struct = {
      {'s', 'c', 'r', 'y', 'p', 't'},
      0,
      static_cast<uint8_t>(base::bits::Log2Ceiling(params.n_factor)),
      base::ByteSwap(params.r_factor),
      base::ByteSwap(params.p_factor)};

  memcpy(&header_struct->salt, salt.data(), sizeof(header_struct->salt));

  // Add the header check sum.
  uint8_t* header_ptr = reinterpret_cast<uint8_t*>(header_struct);
  brillo::Blob header_blob_to_hash(header_ptr,
                                   header_ptr + kLibScryptSubHeaderSize);
  brillo::Blob sha = CryptoLib::Sha256(header_blob_to_hash);
  memcpy(&header_struct->check_sum[0], sha.data(),
         sizeof(header_struct->check_sum));

  // Add the header signature (used for verifying the passsword).
  brillo::SecureBlob key_hmac(derived_key.begin() + kLibScryptHMACOffset,
                              derived_key.end());
  brillo::Blob data_to_hmac(header_ptr,
                            header_ptr + kLibScryptHeaderBytesToHMAC);
  brillo::SecureBlob hmac = CryptoLib::HmacSha256(key_hmac, data_to_hmac);
  memcpy(&header_struct->signature[0], hmac.data(),
         sizeof(header_struct->signature));
}

}  // namespace

// static
bool LibScryptCompat::Encrypt(const brillo::SecureBlob& derived_key,
                              const brillo::SecureBlob& salt,
                              const brillo::SecureBlob& data_to_encrypt,
                              const ScryptParameters& params,
                              brillo::SecureBlob* encrypted_data) {
  encrypted_data->resize(data_to_encrypt.size() + kLibScryptHeaderSize +
                         kLibScryptHMACSize);

  LibScryptHeader header_struct;
  GenerateHeader(salt, derived_key, params, &header_struct);
  memcpy(encrypted_data->data(), &header_struct, sizeof(header_struct));

  brillo::SecureBlob aes_key(derived_key.begin(),
                             derived_key.end() - kLibScryptHMACOffset);
  brillo::SecureBlob iv(kLibScryptIVSize, 0);
  brillo::SecureBlob aes_ciphertext;

  if (!CryptoLib::AesEncryptSpecifyBlockMode(
          data_to_encrypt, 0, data_to_encrypt.size(), aes_key, iv,
          CryptoLib::kPaddingStandard, CryptoLib::kCtr, &aes_ciphertext)) {
    LOG(ERROR) << "AesEncryptSpecifyBlockMode failed.";
    return false;
  }
  memcpy(encrypted_data->data() + sizeof(header_struct), aes_ciphertext.data(),
         aes_ciphertext.size());

  brillo::SecureBlob key_hmac(derived_key.begin() + kLibScryptHMACOffset,
                              derived_key.end());
  brillo::Blob data_to_hmac(
      encrypted_data->begin(),
      encrypted_data->begin() + aes_ciphertext.size() + kLibScryptHeaderSize);
  brillo::SecureBlob hmac = CryptoLib::HmacSha256(key_hmac, data_to_hmac);

  memcpy(encrypted_data->data() + sizeof(header_struct) + aes_ciphertext.size(),
         hmac.data(), kLibScryptHMACSize);

  return true;
}

}  // namespace cryptohome
