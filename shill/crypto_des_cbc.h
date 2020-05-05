// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CRYPTO_DES_CBC_H_
#define SHILL_CRYPTO_DES_CBC_H_

#include <string>
#include <vector>

#include <base/macros.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/crypto_interface.h"

namespace base {

class FilePath;

}  // namespace base

namespace shill {

// DES-CBC crypto module implementation.
class CryptoDesCbc : public CryptoInterface {
 public:
  CryptoDesCbc();

  // Sets the DES key to the last |kBlockSize| bytes of |key_matter_path| and
  // the DES initialization vector to the second to last |kBlockSize| bytes of
  // |key_matter_path|. Returns true on success.
  bool LoadKeyMatter(const base::FilePath& path);

  // Inherited from CryptoInterface.
  std::string GetId() const override;
  bool Encrypt(const std::string& plaintext,
               std::string* ciphertext) const override;
  bool Decrypt(const std::string& ciphertext,
               std::string* plaintext) const override;

  const std::vector<char>& key() const { return key_; }
  const std::vector<char>& iv() const { return iv_; }

 private:
  FRIEND_TEST(CryptoDesCbcTest, Decrypt);
  FRIEND_TEST(CryptoDesCbcTest, Encrypt);

  std::vector<char> key_;
  std::vector<char> iv_;

  DISALLOW_COPY_AND_ASSIGN(CryptoDesCbc);
};

}  // namespace shill

#endif  // SHILL_CRYPTO_DES_CBC_H_
