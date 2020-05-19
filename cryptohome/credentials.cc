// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/credentials.h"

#include <brillo/cryptohome.h>

using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace cryptohome {

Credentials::Credentials() = default;

Credentials::Credentials(const std::string& username, const SecureBlob& passkey)
    : username_(username), passkey_(passkey) {}

Credentials::Credentials(const Credentials& rhs) = default;

Credentials::~Credentials() = default;

Credentials& Credentials::operator=(const Credentials& rhs) = default;

std::string Credentials::GetObfuscatedUsername(
    const SecureBlob& system_salt) const {
  return SanitizeUserNameWithSalt(username_, system_salt);
}

}  // namespace cryptohome
