// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/crypto_provider.h"

#include <memory>
#include <utility>

#include <base/strings/string_util.h>

#include "shill/crypto_des_cbc.h"
#include "shill/crypto_rot47.h"
#include "shill/logging.h"

using std::string;

namespace shill {

const char CryptoProvider::kKeyMatterFile[] = "/var/lib/whitelist/owner.key";

CryptoProvider::CryptoProvider() : key_matter_file_(kKeyMatterFile) {}

void CryptoProvider::Init() {
  cryptos_.clear();

  // Register the crypto modules in priority order -- highest priority first.
  auto des_cbc = std::make_unique<CryptoDesCbc>();
  if (des_cbc->LoadKeyMatter(key_matter_file_)) {
    cryptos_.push_back(std::move(des_cbc));
  }
  cryptos_.push_back(std::make_unique<CryptoRot47>());
}

string CryptoProvider::Encrypt(const string& plaintext) const {
  for (auto& crypto : cryptos_) {
    string ciphertext;
    if (crypto->Encrypt(plaintext, &ciphertext)) {
      const string prefix = crypto->GetId() + ":";
      return prefix + ciphertext;
    }
  }
  LOG(WARNING) << "Unable to encrypt text, returning as is.";
  return plaintext;
}

string CryptoProvider::Decrypt(const string& ciphertext) const {
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
  LOG(WARNING) << "Unable to decrypt text, returning as is.";
  return ciphertext;
}

}  // namespace shill
