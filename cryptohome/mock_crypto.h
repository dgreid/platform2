// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_CRYPTO_H_
#define CRYPTOHOME_MOCK_CRYPTO_H_

#include "cryptohome/crypto.h"

#include <string>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/attestation.pb.h"

namespace cryptohome {

class Platform;

class MockCrypto : public Crypto {
 public:
  MockCrypto() : Crypto(NULL) { set_use_tpm(true); }
  virtual ~MockCrypto() {}

  MOCK_METHOD(bool,
              GetOrCreateSalt,
              (const base::FilePath&, size_t, bool, brillo::SecureBlob*),
              (const, override));
  MOCK_METHOD(bool,
              EncryptWithTpm,
              (const brillo::SecureBlob&, std::string*),
              (const, override));
  MOCK_METHOD(bool,
              DecryptWithTpm,
              (const std::string&, brillo::SecureBlob*),
              (const, override));

  MOCK_METHOD(CryptoError, EnsureTpm, (bool), (const, override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_CRYPTO_H_
