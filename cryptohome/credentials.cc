// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/credentials.h"

#include <cstring>

#include "cryptohome/obfuscated_username.h"

using brillo::SecureBlob;

namespace cryptohome {

Credentials::Credentials() = default;

Credentials::Credentials(const char* username, const SecureBlob& passkey)
    : username_(username, strlen(username)), passkey_(passkey) {}

Credentials::Credentials(const Credentials& rhs) = default;

Credentials::~Credentials() = default;

Credentials& Credentials::operator=(const Credentials& rhs) = default;

std::string Credentials::GetObfuscatedUsername(
    const SecureBlob& system_salt) const {
  return BuildObfuscatedUsername(username_, system_salt);
}

}  // namespace cryptohome
