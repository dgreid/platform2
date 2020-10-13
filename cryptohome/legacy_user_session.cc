// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Mount

#include "cryptohome/legacy_user_session.h"

#include <string>

#include <openssl/evp.h>

#include <base/logging.h>
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"

using brillo::SecureBlob;

namespace cryptohome {

const int kLegacyUserSessionIdLength = 128;

LegacyUserSession::LegacyUserSession() {}

LegacyUserSession::~LegacyUserSession() {}

void LegacyUserSession::Init(const SecureBlob& salt) {
  username_salt_.assign(salt.begin(), salt.end());
}

bool LegacyUserSession::SetUser(const Credentials& credentials) {
  obfuscated_username_ = credentials.GetObfuscatedUsername(username_salt_);
  username_ = credentials.username();
  key_data_ = credentials.key_data();
  key_index_ = -1;  // Invalid key index.
  key_salt_ = CryptoLib::CreateSecureRandomBlob(PKCS5_SALT_LEN);
  const auto plaintext =
      CryptoLib::CreateSecureRandomBlob(kLegacyUserSessionIdLength);

  SecureBlob aes_key;
  SecureBlob aes_iv;
  if (!CryptoLib::PasskeyToAesKey(credentials.passkey(), key_salt_,
                                  cryptohome::kDefaultPasswordRounds, &aes_key,
                                  &aes_iv)) {
    return false;
  }

  return CryptoLib::AesEncryptDeprecated(plaintext, aes_key, aes_iv, &cipher_);
}

void LegacyUserSession::Reset() {
  username_ = "";
  obfuscated_username_ = "";
  key_salt_.resize(0);
  cipher_.resize(0);
  key_index_ = -1;
  key_data_.Clear();
}

bool LegacyUserSession::CheckUser(
    const std::string& obfuscated_username) const {
  return obfuscated_username_ == obfuscated_username;
}

bool LegacyUserSession::Verify(const Credentials& credentials) const {
  ReportTimerStart(kSessionUnlockTimer);

  if (!CheckUser(credentials.GetObfuscatedUsername(username_salt_))) {
    return false;
  }
  // If the incoming credentials have no label, then just
  // test the secret.  If it is labeled, then the label must
  // match.
  if (!credentials.key_data().label().empty() &&
      credentials.key_data().label() != key_data_.label()) {
    return false;
  }

  SecureBlob aes_key;
  SecureBlob aes_iv;
  if (!CryptoLib::PasskeyToAesKey(credentials.passkey(), key_salt_,
                                  cryptohome::kDefaultPasswordRounds, &aes_key,
                                  &aes_iv)) {
    return false;
  }

  SecureBlob plaintext;
  bool status =
      CryptoLib::AesDecryptDeprecated(cipher_, aes_key, aes_iv, &plaintext);
  ReportTimerStop(kSessionUnlockTimer);
  return status;
}

void LegacyUserSession::GetObfuscatedUsername(std::string* username) const {
  username->assign(obfuscated_username_);
}

int LegacyUserSession::key_index() const {
  LOG_IF(WARNING, key_index_ < 0)
      << "Attempt to access an uninitialized key_index."
      << "Guest mount? Ephemeral mount?";
  return key_index_;
}

}  // namespace cryptohome
