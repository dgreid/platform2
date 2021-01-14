// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Homedirs - manages the collection of user home directories on disk. When a
// homedir is actually mounted, it becomes a Mount.

#ifndef CRYPTOHOME_STORAGE_HOMEDIRS_H_
#define CRYPTOHOME_STORAGE_HOMEDIRS_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/time/time.h>
#if USE_LVM_STATEFUL_PARTITION
#include <brillo/blkdev_utils/lvm.h>
#endif  // USE_LVM_STATEFUL_PARTITION
#include <brillo/secure_blob.h>
#include <chaps/token_manager_client.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <gtest/gtest_prod.h>
#include <policy/device_policy.h>
#include <policy/libpolicy.h>

#include "cryptohome/crypto.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/platform.h"
#include "cryptohome/rpc.pb.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/cryptohome_vault_factory.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/encrypted_container_factory.h"
#include "cryptohome/storage/user_oldest_activity_timestamp_cache.h"

namespace cryptohome {

// The uid shift of ARC++ container.
const uid_t kArcContainerShiftUid = 655360;
// The gid shift of ARC++ container.
const gid_t kArcContainerShiftGid = 655360;
extern const char kAndroidCacheInodeAttribute[];
extern const char kAndroidCodeCacheInodeAttribute[];
extern const char kTrackedDirectoryNameAttribute[];
extern const char kRemovableFileAttribute[];

class HomeDirs {
 public:
  // HomeDir contains lists the current user profiles.
  struct HomeDir {
    std::string obfuscated;
    bool is_mounted = false;
  };

  HomeDirs() = default;
  HomeDirs(Platform* platform,
           KeysetManagement* keyset_management,
           const brillo::SecureBlob& system_salt,
           UserOldestActivityTimestampCache* timestamp_cache,
           std::unique_ptr<policy::PolicyProvider> policy_provider);
  HomeDirs(Platform* platform,
           KeysetManagement* keyset_management,
           const brillo::SecureBlob& system_salt,
           UserOldestActivityTimestampCache* timestamp_cache,
           std::unique_ptr<policy::PolicyProvider> policy_provider,
           std::unique_ptr<CryptohomeVaultFactory> vault_factory);
  HomeDirs(const HomeDirs&) = delete;
  HomeDirs& operator=(const HomeDirs&) = delete;

  virtual ~HomeDirs();

  // Removes all cryptohomes owned by anyone other than the owner user (if set),
  // regardless of free disk space.
  virtual void RemoveNonOwnerCryptohomes();

  // Returns the system salt.
  virtual bool GetSystemSalt(brillo::SecureBlob* blob);

  // Returns the owner's obfuscated username.
  virtual bool GetOwner(std::string* owner);
  virtual bool GetPlainOwner(std::string* owner);

  // Returns whether the given user is a non-enterprise owner, or if it will
  // become such in case it signs in now.
  bool IsOrWillBeOwner(const std::string& account_id);

  // Returns whether the ephemeral users policy is enabled.
  virtual bool AreEphemeralUsersEnabled();
  // Creates the cryptohome for the named user.
  virtual bool Create(const std::string& username);

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

  // Check if a dm-crypt container exists for the given obfuscated username.
  virtual bool DmcryptContainerExists(
      const std::string& obfuscated_username,
      const std::string& container_suffix) const;

  // Checks if a dm-crypt cryptohome vault exists for the given obfuscated
  // username.
  virtual bool DmcryptCryptohomeExists(
      const std::string& obfuscated_username) const;

  // Checks if the dm-crypt cryptohome's cache container exists for the given
  // obfuscated username.
  virtual bool DmcryptCacheContainerExists(
      const std::string& obfuscated_username) const;

  virtual bool UpdateActivityTimestamp(const std::string& obfuscted,
                                       int index,
                                       int time_shift_sec);

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
  virtual void set_enterprise_owned(bool value) { enterprise_owned_ = value; }
  virtual bool enterprise_owned() const { return enterprise_owned_; }

  // Choose the vault type for new vaults.
  virtual EncryptedContainerType ChooseVaultType();

  // Generates the cryptohome vault for a newly created home directory.
  virtual std::unique_ptr<CryptohomeVault> CreatePristineVault(
      const std::string& obfuscated_username,
      const FileSystemKeyReference& key_reference,
      CryptohomeVault::Options options,
      MountError* mount_error);

  // Generates the cryptohome vault for an existing home directory that needs
  // to be migrated.
  virtual std::unique_ptr<CryptohomeVault> CreateMigratingVault(
      const std::string& obfuscated_username,
      const FileSystemKeyReference& key_reference,
      CryptohomeVault::Options options,
      MountError* mount_error);

  // Generates the cryptohome vault for an existing home directory that will
  // not be migrated in the current mount.
  virtual std::unique_ptr<CryptohomeVault> CreateNonMigratingVault(
      const std::string& obfuscated_username,
      const FileSystemKeyReference& key_reference,
      CryptohomeVault::Options options,
      MountError* mount_error);

  // Generate the cryptohome vault depending on the on-disk state.
  virtual std::unique_ptr<CryptohomeVault> GenerateCryptohomeVault(
      const std::string& obfuscated_username,
      const FileSystemKeyReference& key_reference,
      CryptohomeVault::Options options,
      bool is_pristine,
      MountError* mount_error);

  // TODO(dlunev, b/172344610): this is a temporary accessor to simplify the
  // split patch. Remove it once all clients using it are either get it
  // directly or not use it.
  virtual KeysetManagement* keyset_management() { return keyset_management_; }

#if USE_LVM_STATEFUL_PARTITION
  void SetLogicalVolumeManagerForTesting(
      std::unique_ptr<brillo::LogicalVolumeManager> lvm) {
    lvm_ = std::move(lvm);
  }
#endif  // USE_LVM_STATEFUL_PARTITION

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

  Platform* platform_;
  KeysetManagement* keyset_management_;
  brillo::SecureBlob system_salt_;
  UserOldestActivityTimestampCache* timestamp_cache_;
  std::unique_ptr<policy::PolicyProvider> policy_provider_;
  bool enterprise_owned_;
  chaps::TokenManagerClient chaps_client_;
  std::unique_ptr<CryptohomeVaultFactory> vault_factory_;

  // The container a not-shifted system UID in ARC++ container (AID_SYSTEM).
  static constexpr uid_t kAndroidSystemUid = 1000;

#if USE_LVM_STATEFUL_PARTITION
  std::unique_ptr<brillo::LogicalVolumeManager> lvm_;
#endif  // USE_LVM_STATEFUL_PARTITION

  friend class HomeDirsTest;
  FRIEND_TEST(HomeDirsTest, GetTrackedDirectoryForDirCrypto);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_HOMEDIRS_H_
