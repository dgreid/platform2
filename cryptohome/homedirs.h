// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Homedirs - manages the collection of user home directories on disk. When a
// homedir is actually mounted, it becomes a Mount.

#ifndef CRYPTOHOME_HOMEDIRS_H_
#define CRYPTOHOME_HOMEDIRS_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/time/time.h>
#include <brillo/secure_blob.h>
#include <chaps/token_manager_client.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <gtest/gtest_prod.h>
#include <policy/device_policy.h>
#include <policy/libpolicy.h>

#include "cryptohome/mount_factory.h"
#include "cryptohome/rpc.pb.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"
#include "cryptohome/vault_keyset_factory.h"

namespace cryptohome {

// The uid shift of ARC++ container.
const uid_t kArcContainerShiftUid = 655360;
// The gid shift of ARC++ container.
const gid_t kArcContainerShiftGid = 655360;
const int64_t kFreeSpaceThresholdToTriggerCleanup = 1LL << 30;
const int64_t kFreeSpaceThresholdToTriggerAggressiveCleanup = 768 * 1024 * 1024;
const int64_t kTargetFreeSpaceAfterCleanup = 2LL << 30;
extern const char kAndroidCacheInodeAttribute[];
extern const char kAndroidCodeCacheInodeAttribute[];
extern const char kTrackedDirectoryNameAttribute[];
extern const char kRemovableFileAttribute[];
extern const char kEcryptfsVaultDir[];
extern const char kMountDir[];

class Credentials;
class Crypto;
class DiskCleanup;
class Platform;
class UserOldestActivityTimestampCache;
class VaultKeyset;

class HomeDirs {
 public:
  // HomeDir contains lists the current user profiles.
  struct HomeDir {
    std::string obfuscated;
    bool is_mounted = false;
  };

  HomeDirs();
  virtual ~HomeDirs();

  // Gets the user's eCryptfs vault directory for the given shadow root path and
  // obfuscated username.
  static base::FilePath GetEcryptfsUserVaultPath(
      const base::FilePath& shadow_root,
      const std::string& obfuscated_username);

  // Gets the directory to mount the user's cryptohome at given the shadow root
  // path and obfuscated username.
  static base::FilePath GetUserMountDirectory(
      const base::FilePath& shadow_root,
      const std::string& obfuscated_username);

  // Initializes this HomeDirs object. Returns true for success.
  virtual bool Init(Platform* platform,
                    Crypto* crypto,
                    UserOldestActivityTimestampCache* cache);

  // Removes all cryptohomes owned by anyone other than the owner user (if set),
  // regardless of free disk space.
  virtual void RemoveNonOwnerCryptohomes();

  // Returns the system salt, creating a new one if necessary. If loading the
  // system salt fails, returns false, and blob is unchanged.
  virtual bool GetSystemSalt(brillo::SecureBlob* blob);

  // Returns the owner's obfuscated username.
  virtual bool GetOwner(std::string* owner);
  virtual bool GetPlainOwner(std::string* owner);

  // Returns whether the given user is a non-enterprise owner, or if it will
  // become such in case it signs in now.
  bool IsOrWillBeOwner(const std::string& account_id);

  // Returns whether the ephemeral users policy is enabled.
  virtual bool AreEphemeralUsersEnabled();

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
  virtual VaultKeyset* GetVaultKeyset(const std::string& obfuscated_username,
                                      const std::string& key_label) const;

  // Removes the cryptohome for the named user.
  virtual bool Remove(const std::string& username);

  // Renames account identified by |account_id_from| to |account_id_to|.
  // This is called when user e-mail is replaced with GaiaId as account
  // identifier.
  virtual bool Rename(const std::string& account_id_from,
                      const std::string& account_id_to);

  // Computes the size of cryptohome for the named user.
  // Return 0 if the given user is invalid of non-existent.
  // Negative values are reserved for future cases whereby we need to do some
  // form of error reporting.
  // Note that this method calculates the disk usage instead of apparent size.
  virtual int64_t ComputeDiskUsage(const std::string& account_id);

  // Returns true if the supplied Credentials are a valid (username, passkey)
  // pair.
  virtual bool AreCredentialsValid(const Credentials& credentials);

  // Returns true if a path exists for the given obfuscated username.
  virtual bool Exists(const std::string& obfuscated_username) const;

  // Checks if a cryptohome vault exists for the given obfuscated username.
  virtual bool CryptohomeExists(const std::string& obfuscated_username) const;

  // Checks if a eCryptfs cryptohome vault exists for the given obfuscated
  // username.
  virtual bool EcryptfsCryptohomeExists(
      const std::string& obfuscated_username) const;

  // Checks if a dircrypto cryptohome vault exists for the given obfuscated
  // username.
  virtual bool DircryptoCryptohomeExists(
      const std::string& obfuscated_username) const;

  // Gets the user's eCryptfs vault directory for the given obfuscated username.
  base::FilePath GetEcryptfsUserVaultPath(
      const std::string& obfuscated_username) const;

  // Gets the directory to mount the user's cryptohome at. The user is specified
  // by its obfuscated username.
  base::FilePath GetUserMountDirectory(
      const std::string& obfuscated_username) const;

  // Returns true if a valid keyset can be decrypted with |creds|.  If true,
  // |vk| will contain the decrypted value, and |key_index|, if non-null, will
  // contain the key index. If false, |vk| will contain the last failed keyset
  // attempt, and |error|, if non-null, will contain the error details.
  // NOTE: The LE Credential Keysets are only considered when the key label
  // provided via |creds| is non-empty.
  virtual bool GetValidKeyset(const Credentials& creds,
                              VaultKeyset* vk,
                              int* key_index,
                              MountError* error);

  // Returns the vault keyset path for the supplied obfuscated username.
  virtual base::FilePath GetVaultKeysetPath(const std::string& obfuscated,
                                            int index) const;

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

  // Finds and updates the keyset authenticated by |credentials| and
  // applies |changed_data| to the keyset conditionally on if
  // |authorization_signature| is needed and is valid.
  virtual CryptohomeErrorCode UpdateKeyset(
      const Credentials& credentials,
      const Key* changed_data,
      const std::string& authorization_signature);

  // Returns true if the |signature| is valid over the |new_key| given
  // the AuthorizationData specification from |existing_key_data|.
  virtual bool CheckAuthorizationSignature(const KeyData& existing_key_data,
                                           const Key& new_key,
                                           const std::string& signature);

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
                       scoped_refptr<Mount> user_mount);

  // Returns the path to the user's chaps token directory.
  virtual base::FilePath GetChapsTokenDir(const std::string& username) const;

  // Returns the path to the user's legacy chaps token directory.
  virtual base::FilePath GetLegacyChapsTokenDir(
      const std::string& username) const;

  // Returns the path to the user's token salt.
  virtual base::FilePath GetChapsTokenSaltPath(
      const std::string& username) const;

  // Returns true if the cryptohome for the given obfuscated username should
  // migrate to dircrypto.
  virtual bool NeedsDircryptoMigration(
      const std::string& obfuscated_username) const;

  // Attempts to reset all LE credentials associated with a username, given
  // a credential |cred|.
  virtual void ResetLECredentials(const Credentials& creds);

  // Removes all LE credentials for a user with |obfuscated_username|.
  virtual void RemoveLECredentials(const std::string& obfuscated_username);

  // Get the number of unmounted android-data directory. Each android users
  // that is not currently logged in should have exactly one android-data
  // directory.
  virtual int32_t GetUnmountedAndroidDataCount();

  // Marks that the device got locked to be able to use only data of a single
  // user until reboot. Internally touches a file in temporary storage marking
  // that PCR was extended.
  virtual bool SetLockedToSingleUser() const;

  // Get the list of cryptohomes on the system.
  virtual std::vector<HomeDir> GetHomeDirs();

  // Called during disk cleanup if the timestamp cache is not yet
  // initialized. Loads the last activity timestamp from the vault keyset.
  virtual void AddUserTimestampToCache(const std::string& obfuscated);

  // Accessors. Mostly used for unit testing. These do not take ownership of
  // passed-in pointers.
  // TODO(wad) Should this update default_crypto_.set_platform()?
  void set_platform(Platform* value) { platform_ = value; }
  Platform* platform() { return platform_; }
  virtual void set_shadow_root(const base::FilePath& value) {
    shadow_root_ = value;
  }
  virtual const base::FilePath& shadow_root() const { return shadow_root_; }
  virtual void set_enterprise_owned(bool value) { enterprise_owned_ = value; }
  virtual bool enterprise_owned() const { return enterprise_owned_; }
  void set_policy_provider(policy::PolicyProvider* value) {
    policy_provider_ = value;
  }
  policy::PolicyProvider* policy_provider() { return policy_provider_; }
  void set_crypto(Crypto* value) { crypto_ = value; }
  Crypto* crypto() const { return crypto_; }
  virtual DiskCleanup* disk_cleanup() const { return cleanup_; }
  virtual void set_disk_cleanup(DiskCleanup* cleanup) { cleanup_ = cleanup; }
  void set_vault_keyset_factory(VaultKeysetFactory* value) {
    vault_keyset_factory_ = value;
  }
  VaultKeysetFactory* vault_keyset_factory() const {
    return vault_keyset_factory_;
  }
  void set_use_tpm(bool use_tpm) { use_tpm_ = use_tpm; }

 private:
  base::TimeDelta GetUserInactivityThresholdForRemoval();
  // Loads the device policy, either by initializing it or reloading the
  // existing one.
  void LoadDevicePolicy();
  // Returns the path of the specified tracked directory (i.e. a directory which
  // we can locate even when without the key).
  bool GetTrackedDirectory(const base::FilePath& user_dir,
                           const base::FilePath& tracked_dir_name,
                           base::FilePath* out);
  // GetTrackedDirectory() implementation for dircrypto.
  bool GetTrackedDirectoryForDirCrypto(const base::FilePath& mount_dir,
                                       const base::FilePath& tracked_dir_name,
                                       base::FilePath* out);

  // Removes all mounted homedirs from the vector
  void FilterMountedHomedirs(std::vector<HomeDir>* homedirs);
  // Used by RemoveNonOwnerCryptohomes and FreeDiskSpace to perform the actual
  // cleanup.
  void RemoveNonOwnerCryptohomesInternal(const std::vector<HomeDir>& homedirs);
  // Callback used during RemoveNonOwnerCryptohomes()
  void RemoveNonOwnerCryptohomesCallback(const std::string& obfuscated);
  // Deletes all directories under the supplied directory whose basename is not
  // the same as the obfuscated owner name.
  void RemoveNonOwnerDirectories(const base::FilePath& prefix);
  // Loads the serialized vault keyset for the supplied obfuscated username.
  // Returns true for success, false for failure.
  bool LoadVaultKeysetForUser(const std::string& obfuscated_user,
                              int index,
                              VaultKeyset* keyset) const;

  // Helper function to check if the directory contains subdirectory that looks
  // like encrypted android-data (see definition of looks-like-android-data in
  // the LooksLikeAndroidData function). Each file names under mounted_user_dir
  // filesystem tree has encrypted name, but unencrypted metadata.
  // False positive is possible, but practically should never happen. Even if
  // false positive happens, installd in ARC++ will use non-quota path and the
  // system will keep running properly (though a bit slower) so it is still
  // safe.
  bool MayContainAndroidData(const base::FilePath& mounted_user_dir) const;

  // Helper function to check if the directory looks like android-data. A
  // directory is said to look like android-data if it has subdirectory owned by
  // Android system. It is possible for a directory that looks like android-data
  // to not actually be android-data, but the other way around is not possible.
  // But practically in current home directory structure, directory that looks
  // like android-data is always android-data. So normally, this function
  // accurately predicts if the directory in the parameter is actually
  // android-data.
  bool LooksLikeAndroidData(const base::FilePath& directory) const;

  // Helper function to check if the directory is owned by android system
  // UID.
  bool IsOwnedByAndroidSystem(const base::FilePath& directory) const;

  // If the VaultKeyset is TPM backed (directly or via PinWeaver, etc.) this
  // method will tell the TPM firmware that a successful decryption occurred.
  void DeclareTpmStableIfAppropriate(VaultKeyset* vk);

  // Takes ownership of the supplied PolicyProvider. Used to avoid leaking mocks
  // in unit tests.
  void own_policy_provider(policy::PolicyProvider* value) {
    default_policy_provider_.reset(value);
    policy_provider_ = value;
  }

  std::unique_ptr<Platform> default_platform_;
  Platform* platform_;
  base::FilePath shadow_root_;
  UserOldestActivityTimestampCache* timestamp_cache_;
  bool enterprise_owned_;
  std::unique_ptr<policy::PolicyProvider> default_policy_provider_;
  policy::PolicyProvider* policy_provider_;
  Crypto* crypto_;
  std::unique_ptr<DiskCleanup> default_cleanup_;
  DiskCleanup* cleanup_;
  // TODO(wad) Collapse all factories into a single manufacturing plant to save
  //           some pointers.
  std::unique_ptr<VaultKeysetFactory> default_vault_keyset_factory_;
  VaultKeysetFactory* vault_keyset_factory_;
  brillo::SecureBlob system_salt_;
  chaps::TokenManagerClient chaps_client_;

  // The container a not-shifted system UID in ARC++ container (AID_SYSTEM).
  static constexpr uid_t kAndroidSystemUid = 1000;

  bool use_tpm_;

  friend class HomeDirsTest;
  FRIEND_TEST(HomeDirsTest, GetTrackedDirectoryForDirCrypto);

  DISALLOW_COPY_AND_ASSIGN(HomeDirs);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_HOMEDIRS_H_
