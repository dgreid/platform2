// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_KEYSET_MANAGEMENT_H_
#define CRYPTOHOME_KEYSET_MANAGEMENT_H_

#include <memory>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <dbus/cryptohome/dbus-constants.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/platform.h"
#include "cryptohome/rpc.pb.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset_factory.h"

namespace cryptohome {

class KeysetManagement {
 public:
  KeysetManagement() = default;
  KeysetManagement(Platform* platform,
                   Crypto* crypto,
                   const brillo::SecureBlob& system_salt,
                   std::unique_ptr<VaultKeysetFactory> vault_keyset_factory);
  virtual ~KeysetManagement() = default;
  KeysetManagement(const KeysetManagement&) = delete;
  KeysetManagement& operator=(const KeysetManagement&) = delete;

  // Returns a list of present keyset indices for an obfuscated username.
  // There is no guarantee the keysets are valid.
  virtual bool GetVaultKeysets(const std::string& obfuscated,
                               std::vector<int>* keysets) const;

  // Outputs a list of present keysets by label for a given obfuscated username.
  // There is no guarantee the keysets are valid nor is the ordering guaranteed.
  // Returns true on success, false if no keysets are found.
  virtual bool GetVaultKeysetLabels(const std::string& obfuscated_username,
                                    std::vector<std::string>* labels) const;

  // Returns a VaultKeyset that matches the given obfuscated username and the
  // key label. If the label is empty or if no matching keyset is found, NULL
  // will be returned.
  //
  // The caller DOES take ownership of the returned VaultKeyset pointer.
  // There is no guarantee the keyset is valid.
  virtual std::unique_ptr<VaultKeyset> GetVaultKeyset(
      const std::string& obfuscated_username,
      const std::string& key_label) const;

  // Returns true if the supplied Credentials are a valid (username, passkey)
  // pair.
  virtual bool AreCredentialsValid(const Credentials& credentials);

  // Returns decrypted with |creds| keyset, or nullptr if none decryptable
  // with the provided |creds| found and |error| will be populated with the
  // partucular failure reason.
  // NOTE: The LE Credential Keysets are only considered when the key label
  // provided via |creds| is non-empty.
  std::unique_ptr<VaultKeyset> GetValidKeyset(const Credentials& creds,
                                              MountError* error);

  // Loads the vault keyset for the supplied obfuscated username and index.
  // Returns true for success, false for failure.
  std::unique_ptr<VaultKeyset> LoadVaultKeysetForUser(
      const std::string& obfuscated_user, int index) const;

  // Looks for a keyset which matches the credentals and returns it decrypted.
  // TODO(dlunev): replace MountError with CryptohomeErrorCode.
  virtual std::unique_ptr<VaultKeyset> LoadUnwrappedKeyset(
      const Credentials& credentials, MountError* error);

  // Adds initial keyset for the credentials.
  virtual bool AddInitialKeyset(const Credentials& credentials);

  // Adds a new vault keyset for the user using the |existing_credentials| to
  // unwrap the homedir key and the |new_credentials| to rewrap and persist to
  // disk.  The key index is return in the |index| pointer if the function
  // returns true.  |index| is not modified if the function returns false.
  // |new_data|, when provided, is copied to the key_data of the new keyset.
  // If |new_data| is provided, a best-effort attempt will be made at ensuring
  // key_data().label() is unique.
  // If |clobber| is true and there are no matching, labeled keys, then it does
  // nothing.  If there is an identically labeled key, it will overwrite it.
  virtual CryptohomeErrorCode AddKeyset(const Credentials& existing_credentials,
                                        const brillo::SecureBlob& new_passkey,
                                        const KeyData* new_data,
                                        bool clobber,
                                        int* index);

  // Removes the keyset identified by |key_data| if |credentials|
  // has the remove() KeyPrivilege.  The VaultKeyset backing
  // |credentials| may be the same that |key_data| identifies.
  virtual CryptohomeErrorCode RemoveKeyset(const Credentials& credentials,
                                           const KeyData& key_data);

  // Removes the keyset specified by |index| from the list for the user
  // vault identified by its |obfuscated| username.
  // The caller should check credentials if the call is user-sourced.
  // TODO(wad,ellyjones) Determine a better keyset priotization and management
  //                     scheme than just integer indices, like fingerprints.
  virtual bool ForceRemoveKeyset(const std::string& obfuscated, int index);

  // Allows a keyset to be moved to a different index assuming the index can be
  // claimed for a given |obfuscated| username.
  virtual bool MoveKeyset(const std::string& obfuscated, int src, int dst);

  // Migrates the cryptohome for the supplied obfuscated username from the
  // supplied old key to the supplied new key.
  virtual bool Migrate(const Credentials& newcreds,
                       const brillo::SecureBlob& oldkey,
                       int* migrated_key_index);

  // Attempts to reset all LE credentials associated with a username, given
  // a credential |cred|.
  void ResetLECredentials(const Credentials& creds);

  // Removes all LE credentials for a user with |obfuscated_username|.
  virtual void RemoveLECredentials(const std::string& obfuscated_username);

 private:
  // Check if the vault keyset needs re-encryption.
  bool ShouldReSaveKeyset(VaultKeyset* vault_keyset) const;

  // Resaves the vault keyset, restoring on failure.
  bool ReSaveKeyset(const Credentials& credentials, VaultKeyset* keyset) const;

  // Checks whether the keyset is up to date (e.g. has correct encryption
  // parameters, has all required fields populated etc.) and if not, updates
  // and resaves the keyset.
  bool ReSaveKeysetIfNeeded(const Credentials& credentials,
                            VaultKeyset* keyset) const;

  Platform* platform_;
  Crypto* crypto_;
  brillo::SecureBlob system_salt_;
  std::unique_ptr<VaultKeysetFactory> vault_keyset_factory_;

  FRIEND_TEST(KeysetManagementTest, ReSaveOnLoadNoReSave);
  FRIEND_TEST(KeysetManagementTest, ReSaveOnLoadTestRegularCreds);
  FRIEND_TEST(KeysetManagementTest, ReSaveOnLoadTestLeCreds);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_KEYSET_MANAGEMENT_H_
