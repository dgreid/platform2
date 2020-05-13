// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/crypto_provider.h"

#include <memory>
#include <utility>

#include <base/strings/string_util.h>

#include "shill/crypto_rot47.h"
#include "shill/logging.h"

using std::string;

namespace shill {

CryptoProvider::CryptoProvider() {
  cryptos_.push_back(std::make_unique<CryptoRot47>());
}

base::Optional<string> CryptoProvider::Encrypt(const string& plaintext) const {
  for (auto& crypto : cryptos_) {
    string ciphertext;
    if (crypto->Encrypt(plaintext, &ciphertext)) {
      const string prefix = crypto->GetId() + ":";
      return prefix + ciphertext;
    }
  }
  LOG(ERROR) << "Failed to encrypt string";
  return base::nullopt;
}

base::Optional<string> CryptoProvider::Decrypt(const string& ciphertext) const {
  for (auto& crypto : cryptos_) {
    const string prefix = crypto->GetId() + ":";
    if (base::StartsWith(ciphertext, prefix, base::CompareCase::SENSITIVE)) {
      string to_decrypt = ciphertext;
      to_decrypt.erase(0, prefix.size());
      string plaintext;
      if (!crypto->Decrypt(to_decrypt, &plaintext)) {
        LOG(WARNING) << "Crypto module " << crypto->GetId()
                     << " failed to decrypt.";
      }
      return plaintext;
    }
  }
  LOG(ERROR) << "Failed to decrypt string";
  return base::nullopt;
}

}  // namespace shill
