// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cryptohome/aes_deprecated_password_verifier.h>

#include <brillo/secure_blob.h>

#include "cryptohome/cryptolib.h"

namespace cryptohome {

namespace {

const int kUserSessionIdLength = 128;

}  // namespace

bool AesDeprecatedPasswordVerifier::Set(const brillo::SecureBlob& secret) {
  key_salt_ = CryptoLib::CreateSecureRandomBlob(PKCS5_SALT_LEN);
  const auto plaintext =
      CryptoLib::CreateSecureRandomBlob(kUserSessionIdLength);

  brillo::SecureBlob aes_key;
  brillo::SecureBlob aes_iv;
  if (!CryptoLib::PasskeyToAesKey(secret, key_salt_,
                                  cryptohome::kDefaultPasswordRounds, &aes_key,
                                  &aes_iv)) {
    return false;
  }

  return CryptoLib::AesEncryptDeprecated(plaintext, aes_key, aes_iv,
                                         &cipher_text_);
}

bool AesDeprecatedPasswordVerifier::Verify(const brillo::SecureBlob& secret) {
  brillo::SecureBlob aes_key;
  brillo::SecureBlob aes_iv;
  if (!CryptoLib::PasskeyToAesKey(secret, key_salt_,
                                  cryptohome::kDefaultPasswordRounds, &aes_key,
                                  &aes_iv)) {
    return false;
  }

  brillo::SecureBlob plaintext;
  return CryptoLib::AesDecryptDeprecated(cipher_text_, aes_key, aes_iv,
                                         &plaintext);
}

}  // namespace cryptohome
