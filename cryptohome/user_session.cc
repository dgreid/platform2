// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_session.h"

#include <memory>
#include <string>
#include <utility>

#include <base/memory/ref_counted.h>

#include <cryptohome/aes_deprecated_password_verifier.h>
#include "cryptohome/credentials.h"
#include "cryptohome/mount.h"

namespace cryptohome {

// Message to use when generating a secret for WebAuthn.
constexpr char kWebAuthnSecretHmacMessage[] = "AuthTimeWebAuthnSecret";

UserSession::UserSession() {}
UserSession::~UserSession() {}
UserSession::UserSession(HomeDirs* homedirs,
                         const brillo::SecureBlob& salt,
                         const scoped_refptr<Mount> mount)
    : homedirs_(homedirs), system_salt_(salt), mount_(mount) {}

MountError UserSession::MountVault(const Credentials& credentials,
                                   const Mount::MountArgs& mount_args) {
  const std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);
  bool created = false;

  // TODO(chromium:1140868, dlunev): once re-recreation logic is removed, this
  // can be moved to the service level.
  if (!homedirs_->CryptohomeExists(obfuscated_username)) {
    if (!mount_args.create_if_missing) {
      LOG(ERROR) << "Asked to mount nonexistent user";
      return MOUNT_ERROR_USER_DOES_NOT_EXIST;
    }

    if (!homedirs_->Create(credentials.username()) ||
        !mount_->PrepareCryptohome(obfuscated_username,
                                   mount_args.create_as_ecryptfs) ||
        !homedirs_->AddInitialKeyset(credentials)) {
      LOG(ERROR) << "Error creating cryptohome.";
      return MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED;
    }
    created = true;
  }

  // Verifies user's credentials and retrieves the user's file system encryption
  // keys.
  MountError code = MOUNT_ERROR_NONE;
  std::unique_ptr<VaultKeyset> vk =
      homedirs_->LoadUnwrappedKeyset(credentials, &code);
  if (code != MOUNT_ERROR_NONE) {
    return code;
  }
  FileSystemKeys fs_keys(*vk);

  if (!mount_->MountCryptohome(credentials.username(), fs_keys, mount_args,
                               created, &code)) {
    // In the weird case where MountCryptohome returns false with ERROR_NONE
    // code report it as FATAL.
    return code == MOUNT_ERROR_NONE ? MOUNT_ERROR_FATAL : code;
  }
  SetCredentials(credentials, vk->legacy_index());
  UpdateActivityTimestamp(0);

  PrepareWebAuthnSecret(obfuscated_username_, fs_keys.fek(), fs_keys.fnek());

  return code;
}

MountError UserSession::MountEphemeral(const Credentials& credentials) {
  MountError code = mount_->MountEphemeralCryptohome(credentials.username());
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

base::Value UserSession::GetStatus() const {
  return mount_->GetStatus(key_index_);
}

void UserSession::PrepareWebAuthnSecret(const std::string& obfuscated_username,
                                        const brillo::SecureBlob& fek,
                                        const brillo::SecureBlob& fnek) {
  // This WebAuthn secret can be rederived upon in-session user auth success
  // since they will unlock the vault keyset.
  const std::string message(kWebAuthnSecretHmacMessage);
  webauthn_secret_ = std::make_unique<brillo::SecureBlob>(
      CryptoLib::HmacSha256(brillo::SecureBlob::Combine(fnek, fek),
                            brillo::Blob(message.cbegin(), message.cend())));
  clear_webauthn_secret_timer_.Start(
      FROM_HERE, base::TimeDelta::FromSeconds(5),
      base::BindOnce(&UserSession::ClearWebAuthnSecret,
                     base::Unretained(this)));
}

void UserSession::ClearWebAuthnSecret() {
  webauthn_secret_.reset();
}

std::unique_ptr<brillo::SecureBlob> UserSession::GetWebAuthnSecret() {
  return std::move(webauthn_secret_);
}

bool UserSession::SetCredentials(const Credentials& credentials,
                                 int key_index) {
  obfuscated_username_ = credentials.GetObfuscatedUsername(system_salt_);
  username_ = credentials.username();
  key_data_ = credentials.key_data();
  key_index_ = key_index;

  password_verifier_.reset(new AesDeprecatedPasswordVerifier());
  return password_verifier_->Set(credentials.passkey());
}

bool UserSession::VerifyUser(const std::string& obfuscated_username) const {
  return obfuscated_username_ == obfuscated_username;
}

bool UserSession::VerifyCredentials(const Credentials& credentials) const {
  ReportTimerStart(kSessionUnlockTimer);

  if (!password_verifier_) {
    LOG(ERROR) << "Attempt to verify credentials with no verifier set";
    return false;
  }
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

  bool status = password_verifier_->Verify(credentials.passkey());

  ReportTimerStop(kSessionUnlockTimer);

  return status;
}

}  // namespace cryptohome
