// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_VAULT_KEYSET_H_
#define CRYPTOHOME_MOCK_VAULT_KEYSET_H_

#include "cryptohome/vault_keyset.h"

#include <string>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"

namespace cryptohome {
class Platform;
class Crypto;

class MockVaultKeyset : public VaultKeyset {
 public:
  MockVaultKeyset();
  virtual ~MockVaultKeyset();

  MOCK_METHOD(void, Initialize, (Platform*, Crypto*), (override));
  MOCK_METHOD(void, FromVaultKeyset, (const VaultKeyset&), (override));
  MOCK_METHOD(void, FromKeys, (const VaultKeysetKeys&), (override));
  MOCK_METHOD(bool, FromKeysBlob, (const brillo::SecureBlob&), (override));
  MOCK_METHOD(bool, ToKeys, (VaultKeysetKeys*), (const, override));
  MOCK_METHOD(bool, ToKeysBlob, (brillo::SecureBlob*), (const, override));

  MOCK_METHOD(void, CreateRandom, (), (override));

  MOCK_METHOD(const brillo::SecureBlob&, fek, (), (const, override));
  MOCK_METHOD(const brillo::SecureBlob&, fek_sig, (), (const, override));
  MOCK_METHOD(const brillo::SecureBlob&, fek_salt, (), (const, override));
  MOCK_METHOD(const brillo::SecureBlob&, fnek, (), (const, override));
  MOCK_METHOD(const brillo::SecureBlob&, fnek_sig, (), (const, override));
  MOCK_METHOD(const brillo::SecureBlob&, fnek_salt, (), (const, override));

  MOCK_METHOD(bool, Load, (const base::FilePath&), (override));
  MOCK_METHOD(bool,
              Decrypt,
              (const brillo::SecureBlob&, bool, CryptoError*),
              (override));
  MOCK_METHOD(bool, Save, (const base::FilePath&), (override));
  MOCK_METHOD(bool,
              Encrypt,
              (const brillo::SecureBlob&, const std::string&),
              (override));
  MOCK_METHOD(std::string, label, (), (const, override));
  MOCK_METHOD(const SerializedVaultKeyset&, serialized, (), (const, override));
  MOCK_METHOD(SerializedVaultKeyset*, mutable_serialized, (), (override));
  MOCK_METHOD(const base::FilePath&, source_file, (), (const, override));
  MOCK_METHOD(void, set_legacy_index, (int), (override));
  MOCK_METHOD(const int, legacy_index, (), (const, override));

 private:
  SerializedVaultKeyset stub_serialized_;
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_VAULT_KEYSET_H_
