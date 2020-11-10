// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_KEYSTORE_H_
#define CRYPTOHOME_MOCK_KEYSTORE_H_

#include "cryptohome/keystore.h"

#include <string>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

namespace cryptohome {

class MockKeyStore : public KeyStore {
 public:
  MockKeyStore();
  MockKeyStore(const MockKeyStore&) = delete;
  MockKeyStore& operator=(const MockKeyStore&) = delete;

  virtual ~MockKeyStore();

  MOCK_METHOD(
      bool,
      Read,
      (bool, const std::string&, const std::string&, brillo::SecureBlob*),
      (override));
  MOCK_METHOD(
      bool,
      Write,
      (bool, const std::string&, const std::string&, const brillo::SecureBlob&),
      (override));
  MOCK_METHOD(bool,
              Delete,
              (bool, const std::string&, const std::string&),
              (override));
  MOCK_METHOD(bool,
              DeleteByPrefix,
              (bool, const std::string&, const std::string&),
              (override));
  MOCK_METHOD(bool,
              Register,
              (bool,
               const std::string&,
               const std::string&,
               const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               const brillo::SecureBlob&),
              (override));
  MOCK_METHOD(bool,
              RegisterCertificate,
              (bool, const std::string&, const brillo::SecureBlob&),
              (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_KEYSTORE_H_
