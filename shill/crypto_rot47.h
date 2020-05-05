// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CRYPTO_ROT47_H_
#define SHILL_CRYPTO_ROT47_H_

#include <string>

#include <base/macros.h>

#include "shill/crypto_interface.h"

namespace shill {

// ROT47 crypto module implementation.
class CryptoRot47 : public CryptoInterface {
 public:
  CryptoRot47();

  // Inherited from CryptoInterface.
  std::string GetId() const override;
  bool Encrypt(const std::string& plaintext,
               std::string* ciphertext) const override;
  bool Decrypt(const std::string& ciphertext,
               std::string* plaintext) const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(CryptoRot47);
};

}  // namespace shill

#endif  // SHILL_CRYPTO_ROT47_H_
