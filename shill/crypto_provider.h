// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CRYPTO_PROVIDER_H_
#define SHILL_CRYPTO_PROVIDER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/optional.h>

#include "shill/crypto_interface.h"

namespace shill {

// Top-level class for encryption and decryption. Provides backwards
// compatibility for ease of creating new crypto modules and gracefully
// migrating data from older to newer modules.
class CryptoProvider {
 public:
  CryptoProvider();

  // CryptoProvider is not copyable or movable.
  CryptoProvider(const CryptoProvider&) = delete;
  CryptoProvider& operator=(const CryptoProvider&) = delete;

  // Returns |plaintext| encrypted by the highest priority available crypto
  // module capable of performing the operation. If no module succeeds, returns
  // base::nullopt.
  base::Optional<std::string> Encrypt(const std::string& plaintext) const;

  // Returns |ciphertext| decrypted by the highest priority available crypto
  // module capable of performing the operation. If no module succeeds, returns
  // base::nullopt.
  base::Optional<std::string> Decrypt(const std::string& ciphertext) const;

 private:
  // Registered crypto modules in high to low priority order.
  std::vector<std::unique_ptr<CryptoInterface>> cryptos_;
};

}  // namespace shill

#endif  // SHILL_CRYPTO_PROVIDER_H_
