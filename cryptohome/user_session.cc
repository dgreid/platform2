// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_session.h"

#include <memory>
#include <string>

#include <base/memory/ref_counted.h>

#include "cryptohome/credentials.h"
#include "cryptohome/mount.h"

namespace cryptohome {

const int kUserSessionIdLength = 128;

UserSession::UserSession() {}
UserSession::~UserSession() {}
UserSession::UserSession(const brillo::SecureBlob& salt,
                         const scoped_refptr<Mount> mount)
    : system_salt_(salt), mount_(mount) {}

MountError UserSession::MountVault(const Credentials& credentials,
                                   const Mount::MountArgs& mount_args) {
  MountError code = MOUNT_ERROR_NONE;
  if (!mount_->MountCryptohome(credentials, mount_args, true, &code)) {
    // In the weird case where MountCryptohome returns false with ERROR_NONE
    // code report it as FATAL.
    return code == MOUNT_ERROR_NONE ? MOUNT_ERROR_FATAL : code;
  }
  SetCredentials(credentials, mount_->mount_key_index());
  UpdateActivityTimestamp(0);
  return code;
}

MountError UserSession::MountEphemeral(const Credentials& credentials) {
  MountError code = mount_->MountEphemeralCryptohome(credentials);
  if (code == MOUNT_ERROR_NONE) {
    SetCredentials(credentials, -1);
  }
  return code;
}

MountError UserSession::MountGuest() {
  bool status = mount_->MountGuestCryptohome();
  return status ? MOUNT_ERROR_NONE : MOUNT_ERROR_FATAL;
}

bool UserSession::Unmount() {
  if (mount_->IsNonEphemeralMounted()) {
    UpdateActivityTimestamp(0);
  }
  return mount_->UnmountCryptohome();
}

bool UserSession::UpdateActivityTimestamp(int time_shift_sec) {
  return mount_->UpdateCurrentUserActivityTimestamp(time_shift_sec, key_index_);
}

std::unique_ptr<base::Value> UserSession::GetStatus() const {
  return mount_->GetStatus(key_index_);
}

bool UserSession::SetCredentials(const Credentials& credentials,
                                 int key_index) {
  obfuscated_username_ = credentials.GetObfuscatedUsername(system_salt_);
  username_ = credentials.username();
  key_data_ = credentials.key_data();
  key_index_ = key_index;
  key_salt_ = CryptoLib::CreateSecureRandomBlob(PKCS5_SALT_LEN);
  const auto plaintext =
      CryptoLib::CreateSecureRandomBlob(kUserSessionIdLength);

  brillo::SecureBlob aes_key;
  brillo::SecureBlob aes_iv;
  if (!CryptoLib::PasskeyToAesKey(credentials.passkey(), key_salt_,
                                  cryptohome::kDefaultPasswordRounds, &aes_key,
                                  &aes_iv)) {
    return false;
  }

  return CryptoLib::AesEncryptDeprecated(plaintext, aes_key, aes_iv, &cipher_);
}

bool UserSession::VerifyUser(const std::string& obfuscated_username) const {
  return obfuscated_username_ == obfuscated_username;
}

bool UserSession::VerifyCredentials(const Credentials& credentials) const {
  ReportTimerStart(kSessionUnlockTimer);

  if (!VerifyUser(credentials.GetObfuscatedUsername(system_salt_))) {
    return false;
  }
  // If the incoming credentials have no label, then just
  // test the secret.  If it is labeled, then the label must
  // match.
  if (!credentials.key_data().label().empty() &&
      credentials.key_data().label() != key_data_.label()) {
    return false;
  }

  brillo::SecureBlob aes_key;
  brillo::SecureBlob aes_iv;
  if (!CryptoLib::PasskeyToAesKey(credentials.passkey(), key_salt_,
                                  cryptohome::kDefaultPasswordRounds, &aes_key,
                                  &aes_iv)) {
    return false;
  }

  brillo::SecureBlob plaintext;
  bool status =
      CryptoLib::AesDecryptDeprecated(cipher_, aes_key, aes_iv, &plaintext);
  ReportTimerStop(kSessionUnlockTimer);
  return status;
}

}  // namespace cryptohome
