// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_WEBAUTHN_STORAGE_H_
#define U2FD_WEBAUTHN_STORAGE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>

namespace u2f {

constexpr int kCredentialSecretSize = 32;

// Used to persist credentials as JSON in the user's cryptohome.
struct WebAuthnRecord {
  // Credential id in bytes. Will be hex-encoded.
  std::string credential_id;
  // Secret to use for this credential in bytes. Will be base64-encoded.
  // We do not use SecureBlob here because: 1. Loading many SecureBlobs in
  // memory will hit RLIMIT_MEMLOCK. 2. With physical presence and auth-time
  // secret, this per-credential secret is more like a salt.
  brillo::Blob secret;
  // The relying party id.
  std::string rp_id;
  // The PublicKeyCredentialUserEntity.id property in bytes. Will be
  // hex-encoded.
  std::string user_id;
  // The PublicKeyCredentialUserEntity.display_name property.
  std::string user_display_name;
  // Timestamp of record creation.
  double timestamp;
};

// WebAuthnStorage manages the WebAuthn credential id records for the current
// user. It supports CRUD operations on WebAuthn records.
// TODO(yichengli): Add support for deleting records.
class WebAuthnStorage {
 public:
  WebAuthnStorage();
  virtual ~WebAuthnStorage();

  // Adds |record| to in-memory records and persist it on disk.
  virtual bool WriteRecord(const WebAuthnRecord& record);
  // Loads records for |sanitized_user| to memory.
  virtual bool LoadRecords();

  // Clears in-memory records.
  virtual void Reset();

  virtual base::Optional<brillo::Blob> GetSecretByCredentialId(
      const std::string& credential_id);

  virtual base::Optional<WebAuthnRecord> GetRecordByCredentialId(
      const std::string& credential_id);

  // Writes auth-time secret hash to disk.
  bool PersistAuthTimeSecretHash(const brillo::Blob& hash);
  // Loads auth-time secret hash from disk.
  std::unique_ptr<brillo::Blob> LoadAuthTimeSecretHash();

  // Sets the |allow_access_| which determines whether the backing storage
  // location can be accessed or not.
  void set_allow_access(bool allow_access) { allow_access_ = allow_access; }

  void set_sanitized_user(const std::string& sanitized_user) {
    sanitized_user_ = sanitized_user;
  }

  void SetRootPathForTesting(const base::FilePath& root_path);

 private:
  base::FilePath root_path_;
  // Whether access to storage is allowed.
  bool allow_access_ = false;
  // The current user that we are reading/writing records for.
  std::string sanitized_user_;
  // All WebAuthn credential records for |sanitized_user_|.
  std::vector<WebAuthnRecord> records_;
};

}  // namespace u2f

#endif  // U2FD_WEBAUTHN_STORAGE_H_
