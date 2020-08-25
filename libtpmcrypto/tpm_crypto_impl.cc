// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libtpmcrypto/tpm_crypto_impl.h"

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#if USE_TPM2
#include "libtpmcrypto/tpm2_impl.h"
#else
#include "libtpmcrypto/tpm1_impl.h"
#endif

#include "libtpmcrypto/tpm_proto_utils.h"

namespace tpmcrypto {

using brillo::SecureBlob;

namespace {

// Checks that |key| and |iv| are the correct length for AES256-GCM.
bool ValidateGcmKeyAndIvLength(const SecureBlob& key, const SecureBlob& iv) {
  if (key.size() != kDefaultAesKeySize) {
    LOG(ERROR) << "Key size is " << key.size() << " expected "
               << kDefaultAesKeySize;
    return false;
  }

  if (iv.size() != kGcmDefaultIVSize) {
    LOG(ERROR) << "IV size is " << iv.size() << " expected "
               << kGcmDefaultIVSize;
    return false;
  }

  return true;
}

// This function just calls ValidateGcmKeyAndIvLength(). It only exists for
// symmetry.
bool ValidateGcmEncryptionInputs(const SecureBlob& key, const SecureBlob& iv) {
  return ValidateGcmKeyAndIvLength(key, iv);
}

// Validates the length of |key|, |iv| and |tag| for AES256-GCM.
bool ValidateGcmDecryptionInputs(const SecureBlob& key,
                                 const SecureBlob& iv,
                                 const SecureBlob& tag) {
  if (!ValidateGcmKeyAndIvLength(key, iv)) {
    return false;
  }

  if (tag.size() != kGcmDefaultTagSize) {
    LOG(ERROR) << "Decryption tag size is " << iv.size() << " expected "
               << kGcmDefaultTagSize;
    return false;
  }

  return true;
}

bool AesEncryptGcmMode(const SecureBlob& plain_text,
                       const SecureBlob& key,
                       const SecureBlob& iv,
                       SecureBlob* cipher_text,
                       SecureBlob* tag) {
  DCHECK(cipher_text);
  DCHECK(tag);

  if (!ValidateGcmEncryptionInputs(key, iv)) {
    return false;
  }

  crypto::ScopedEVP_CIPHER_CTX context(EVP_CIPHER_CTX_new());
  if (!context) {
    LOG(ERROR) << "Failed to allocate EVP_CIPHER_CTX";
    return false;
  }
  if (!EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, key.data(),
                          iv.data())) {
    LOG(ERROR) << "Failed to initialize gcm encryption context";
    return false;
  }

  // Encrypt all of |plain_text|. There is no padding in GCM mode so
  // |cipher_text| will be the same length as |plain_text|.
  SecureBlob local_cipher_text(plain_text.size());
  int encrypted_len;
  if (!EVP_EncryptUpdate(context.get(), local_cipher_text.data(),
                         &encrypted_len, plain_text.data(),
                         plain_text.size())) {
    LOG(ERROR) << "EncryptUpdate failed";
    return false;
  }
  CHECK_EQ(plain_text.size(), encrypted_len);

  // In GCM Mode the result of |bytes_written| will be zero and no additional
  // bytes need to be written during EncryptFinal so a nullptr can be passed.
  if (!EVP_EncryptFinal_ex(context.get(), nullptr, &encrypted_len)) {
    LOG(ERROR) << "EncryptFinal failed";
    return false;
  }
  CHECK_EQ(0, encrypted_len);

  // Now that the encryption is finalized get the tag value.
  tag->resize(kGcmDefaultTagSize);
  if (!EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG,
                           kGcmDefaultTagSize, tag->data())) {
    LOG(ERROR) << "Could not get GCM encryption tag";
    return false;
  }

  *cipher_text = std::move(local_cipher_text);
  return true;
}

bool AesDecryptGcmMode(const SecureBlob& cipher_text,
                       const SecureBlob& key,
                       const SecureBlob& iv,
                       const SecureBlob& tag,
                       SecureBlob* plain_text) {
  DCHECK(plain_text);

  if (!ValidateGcmDecryptionInputs(key, iv, tag)) {
    return false;
  }

  crypto::ScopedEVP_CIPHER_CTX context(EVP_CIPHER_CTX_new());
  if (!context) {
    LOG(ERROR) << "Failed to allocate EVP_CIPHER_CTX";
    return false;
  }
  if (!EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, key.data(),
                          iv.data())) {
    LOG(ERROR) << "Failed to initialize GCM decryption context";
    return false;
  }

  // Note that |tag| is not modified here, but because the API takes void*
  // |tag| cannot be const.
  if (!EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, tag.size(),
                           const_cast<uint8_t*>(tag.data()))) {
    LOG(ERROR) << "Could not set GCM encryption tag";
    return false;
  }

  // Decrypt all of |cipher_text|. There is no padding in GCM mode so
  // |plain_text| will be the same length as |cipher_text|.
  SecureBlob local_plain_text(cipher_text.size());
  int decrypted_len;
  if (!EVP_DecryptUpdate(context.get(), local_plain_text.data(), &decrypted_len,
                         cipher_text.data(), cipher_text.size())) {
    LOG(ERROR) << "DecryptUpdate failed";
    return false;
  }
  CHECK_EQ(cipher_text.size(), decrypted_len);

  // In GCM mode all the data was decrypted already, so no more data needs to
  // be written so a nullptr can be passed. This call just validates the tag
  // that was set during initialization.
  if (!EVP_DecryptFinal_ex(context.get(), nullptr, &decrypted_len)) {
    LOG(ERROR) << "DecryptFinal failed ";
    return false;
  }
  CHECK_EQ(0, decrypted_len);

  *plain_text = std::move(local_plain_text);
  return true;
}

}  // namespace

TpmCryptoImpl::TpmCryptoImpl() : rand_bytes_fn_(RAND_bytes) {
#if USE_TPM2
  tpm_ = std::make_unique<Tpm2Impl>();
#else
  tpm_ = std::make_unique<Tpm1Impl>();
#endif
}

TpmCryptoImpl::TpmCryptoImpl(std::unique_ptr<Tpm> tpm)
    : TpmCryptoImpl(std::move(tpm), RAND_bytes) {}

TpmCryptoImpl::TpmCryptoImpl(std::unique_ptr<Tpm> tpm,
                             RandBytesFn rand_bytes_fn) {
  CHECK(tpm);
  CHECK(rand_bytes_fn);
  tpm_ = std::move(tpm);
  rand_bytes_fn_ = rand_bytes_fn;
}

TpmCryptoImpl::~TpmCryptoImpl() = default;

bool TpmCryptoImpl::Encrypt(const SecureBlob& data,
                            std::string* encrypted_data) {
  if (data.empty()) {
    // Do not allow empty plaintext.
    return false;
  }

  SecureBlob aes_key;
  SecureBlob sealed_key;
  return CreateSealedKey(&aes_key, &sealed_key) &&
         EncryptData(data, aes_key, sealed_key, encrypted_data);
}

bool TpmCryptoImpl::Decrypt(const std::string& encrypted_data,
                            SecureBlob* data) {
  SecureBlob sealed_key;
  SecureBlob iv;
  SecureBlob tag;
  SecureBlob encrypted_data_blob;
  if (!ParseTpmCryptoProto(encrypted_data, &sealed_key, &iv, &tag,
                           &encrypted_data_blob)) {
    return false;
  }

  SecureBlob aes_key;
  if (!tpm_->Unseal(sealed_key, &aes_key)) {
    LOG(ERROR) << "Cannot unseal aes key.";
    return false;
  }

  if (!AesDecryptGcmMode(encrypted_data_blob, aes_key, iv, tag, data)) {
    LOG(ERROR) << "Failed to decrypt encrypted data.";
    return false;
  }
  return true;
}

bool TpmCryptoImpl::CreateSealedKey(SecureBlob* aes_key,
                                    SecureBlob* sealed_key) const {
  if (!GetRandomDataSecureBlob(kDefaultAesKeySize, aes_key)) {
    LOG(ERROR) << "GetRandomDataSecureBlob failed.";
    return false;
  }

  if (!tpm_->SealToPCR0(*aes_key, sealed_key)) {
    LOG(ERROR) << "Failed to seal cipher key.";
    return false;
  }
  return true;
}

bool TpmCryptoImpl::EncryptData(const SecureBlob& data,
                                const SecureBlob& aes_key,
                                const SecureBlob& sealed_key,
                                std::string* encrypted_data) const {
  SecureBlob iv;
  if (!GetRandomDataSecureBlob(kGcmDefaultIVSize, &iv)) {
    LOG(ERROR) << "GetRandomDataSecureBlob failed.";
    return false;
  }

  SecureBlob tag;
  SecureBlob encrypted_data_blob;
  if (!AesEncryptGcmMode(data, aes_key, iv, &encrypted_data_blob, &tag)) {
    LOG(ERROR) << "Failed to encrypt serial data.";
    return false;
  }

  return CreateSerializedTpmCryptoProto(sealed_key, iv, tag,
                                        encrypted_data_blob, encrypted_data);
}

bool TpmCryptoImpl::GetRandomDataSecureBlob(size_t length,
                                            brillo::SecureBlob* data) const {
  data->resize(length);
  return rand_bytes_fn_(data->data(), data->size()) == 1;
}

}  // namespace tpmcrypto
