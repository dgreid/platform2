// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_session.h"

#include <memory>

#include <base/memory/ref_counted.h>

#include "cryptohome/credentials.h"
#include "cryptohome/mount.h"

namespace cryptohome {

UserSession::UserSession() {}
UserSession::~UserSession() {}
UserSession::UserSession(const scoped_refptr<Mount> mount) : mount_(mount) {}

MountError UserSession::MountVault(const Credentials& credentials,
                                   const Mount::MountArgs& mount_args) {
  MountError code = MOUNT_ERROR_NONE;
  if (!mount_->MountCryptohome(credentials, mount_args, true, &code)) {
    // In the weird case where MountCryptohome returns false with ERROR_NONE
    // code report it as FATAL.
    return code == MOUNT_ERROR_NONE ? MOUNT_ERROR_FATAL : code;
  }
  UpdateActivityTimestamp(0);
  return code;
}

MountError UserSession::MountEphemeral(const Credentials& credentials) {
  return mount_->MountEphemeralCryptohome(credentials);
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
  return mount_->UpdateCurrentUserActivityTimestamp(time_shift_sec);
}

std::unique_ptr<base::Value> UserSession::GetStatus() const {
  return mount_->GetStatus();
}

bool UserSession::SetCredentials(const Credentials& credentials,
                                 int key_index) {
  return mount_->SetUserCreds(credentials, key_index);
}

const KeyData& UserSession::key_data() const {
  return mount_->GetCurrentLegacyUserSession()->key_data();
}

}  // namespace cryptohome
