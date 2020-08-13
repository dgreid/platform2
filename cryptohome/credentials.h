// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CREDENTIALS_H_
#define CRYPTOHOME_CREDENTIALS_H_

#include <string>

#include <brillo/secure_blob.h>

#include "cryptohome/key.pb.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

// This class wraps a username/passkey pair that can be used to authenticate the
// user.
class Credentials final {
 public:
  Credentials();
  Credentials(const std::string& username, const brillo::SecureBlob& passkey);
  Credentials(const Credentials& rhs);
  ~Credentials();

  Credentials& operator=(const Credentials& rhs);

  // Returns the full user name as a std::string
  std::string username() const { return username_; }

  // Returns the obfuscated username, used as the name of the directory
  // containing the user's stateful data (and maybe used for other reasons
  // at some point.)
  std::string GetObfuscatedUsername(
      const brillo::SecureBlob& system_salt) const;

  const brillo::SecureBlob& passkey() const { return passkey_; }

  // Getter and setter for the associated KeyData.
  void set_key_data(const KeyData& data) { key_data_ = data; }
  const KeyData& key_data() const { return key_data_; }

  // Getter and setter for the associated
  // SerializedVaultKeyset::SignatureChallengeInfo.
  // Used only for freshly generated challenge-protected credentials (see
  // ChallengeCredentialsHelper::GenerateNew()).
  void set_challenge_credentials_keyset_info(
      const SerializedVaultKeyset_SignatureChallengeInfo&
          challenge_credentials_keyset_info) {
    challenge_credentials_keyset_info_ = challenge_credentials_keyset_info;
  }
  const SerializedVaultKeyset_SignatureChallengeInfo&
  challenge_credentials_keyset_info() const {
    return challenge_credentials_keyset_info_;
  }

 private:
  std::string username_;
  KeyData key_data_;
  SerializedVaultKeyset_SignatureChallengeInfo
      challenge_credentials_keyset_info_;
  brillo::SecureBlob passkey_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CREDENTIALS_H_
