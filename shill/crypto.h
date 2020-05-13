// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CRYPTO_H_
#define SHILL_CRYPTO_H_

#include <string>

#include <base/optional.h>

namespace shill {

// Top-level interface for encryption and decryption. Currently only uses ROT47.
namespace Crypto {

// Returns |plaintext| encrypted by the highest priority available crypto
// module capable of performing the operation.
std::string Encrypt(const std::string& plaintext);

// Returns |ciphertext| decrypted by the highest priority available crypto
// module capable of performing the operation. If no module succeeds, returns
// base::nullopt.
base::Optional<std::string> Decrypt(const std::string& ciphertext);

}  // namespace Crypto

}  // namespace shill

#endif  // SHILL_CRYPTO_H_
