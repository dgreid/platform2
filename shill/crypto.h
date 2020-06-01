// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CRYPTO_H_
#define SHILL_CRYPTO_H_

#include <string>

#include <base/optional.h>

namespace shill {

// Used to migrate Profile storage from the old ROT47 format to plaintext.
// TODO(crbug.com/1084279) Remove this and friends after migration to plaintext
// is complete.
namespace Crypto {

// Returns |ciphertext| decrypted by the highest priority available crypto
// module capable of performing the operation. If no module succeeds, returns
// base::nullopt.
base::Optional<std::string> Decrypt(const std::string& ciphertext);

}  // namespace Crypto

}  // namespace shill

#endif  // SHILL_CRYPTO_H_
