// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_VAULT_KEYSET_H_
#define CRYPTOHOME_VAULT_KEYSET_H_

#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/timestamp.pb.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class Crypto;
class Platform;

// VaultKeyset holds the File Encryption Key (FEK) and File Name Encryption Key
// (FNEK) and their corresponding signatures.
class VaultKeyset {
 public:
  VaultKeyset();
  VaultKeyset(VaultKeyset&&);
  virtual ~VaultKeyset();

  // Does not take ownership of platform and crypto. The objects pointed to by
  // them must outlive this object.
  virtual void Initialize(Platform* platform, Crypto* crypto);

  virtual void FromVaultKeyset(const VaultKeyset& vault_keyset);
  virtual void FromKeys(const VaultKeysetKeys& keys);
  virtual bool FromKeysBlob(const brillo::SecureBlob& keys_blob);
  virtual bool ToKeys(VaultKeysetKeys* keys) const;
  virtual bool ToKeysBlob(brillo::SecureBlob* keys_blob) const;
  virtual void CreateRandomChapsKey();
  virtual void CreateRandomResetSeed();
  virtual void CreateRandom();

  virtual const brillo::SecureBlob& fek() const;
  virtual const brillo::SecureBlob& fek_sig() const;
  virtual const brillo::SecureBlob& fek_salt() const;
  virtual const brillo::SecureBlob& fnek() const;
  virtual const brillo::SecureBlob& fnek_sig() const;
  virtual const brillo::SecureBlob& fnek_salt() const;

  virtual bool Load(const base::FilePath& filename);
  // Load must be called first. |crypto_error| may be null.
  virtual bool Decrypt(const brillo::SecureBlob& key,
                       bool is_pcr_extended,
                       CryptoError* crypto_error);
  // Encrypt must be called first.
  virtual bool Save(const base::FilePath& filename);
  virtual bool Encrypt(const brillo::SecureBlob& key,
                       const std::string& obfuscated_username);
  virtual const SerializedVaultKeyset& serialized() const {
    return serialized_;
  }
  virtual SerializedVaultKeyset* mutable_serialized() {
    return &serialized_;
  }
  virtual const base::FilePath& source_file() const {
    return source_file_;
  }
  virtual void set_legacy_index(int index) {
    legacy_index_ = index;
  }
  virtual const int legacy_index() const {
    return legacy_index_;
  }
  virtual const brillo::SecureBlob& chaps_key() const {
    return chaps_key_;
  }
  virtual const brillo::SecureBlob& reset_seed() const {
    return reset_seed_;
  }
  virtual const brillo::SecureBlob& reset_secret() const {
    return reset_secret_;
  }
  virtual void set_chaps_key(const brillo::SecureBlob& chaps_key);
  virtual void clear_chaps_key();
  virtual void set_reset_seed(const brillo::SecureBlob& reset_seed);
  virtual void set_reset_secret(const brillo::SecureBlob& reset_secret);
  virtual bool IsLECredential() const;
  virtual bool IsSignatureChallengeProtected() const;
  virtual int GetFscryptPolicyVersion();
  virtual void SetFscryptPolicyVersion(int policy_version);

 private:
  brillo::SecureBlob fek_;
  brillo::SecureBlob fek_sig_;
  brillo::SecureBlob fek_salt_;
  brillo::SecureBlob fnek_;
  brillo::SecureBlob fnek_sig_;
  brillo::SecureBlob fnek_salt_;
  brillo::SecureBlob chaps_key_;
  brillo::SecureBlob reset_seed_;
  // Used by LECredentials only.
  brillo::SecureBlob reset_secret_;

  Platform* platform_;
  Crypto* crypto_;

  SerializedVaultKeyset serialized_;
  bool loaded_;
  bool encrypted_;
  base::FilePath source_file_;
  int legacy_index_;

  DISALLOW_COPY_AND_ASSIGN(VaultKeyset);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_VAULT_KEYSET_H_
