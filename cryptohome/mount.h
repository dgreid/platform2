// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Mount - class for managing cryptohome user keys and mounts.  In Chrome OS,
// users are managed on top of a shared unix user, chronos.  When a user logs
// in, cryptohome mounts their encrypted home directory to /home/chronos/user,
// and Chrome does a profile switch to that directory.  All user data in their
// home directory is transparently encrypted, providing protection against
// offline theft.  On logout, the mount point is removed.

#ifndef CRYPTOHOME_MOUNT_H_
#define CRYPTOHOME_MOUNT_H_

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/synchronization/condition_variable.h>
#include <base/synchronization/lock.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <base/values.h>
#include <brillo/secure_blob.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest_prod.h>
#include <policy/device_policy.h>
#include <policy/libpolicy.h>

#include "cryptohome/crypto.h"
#include "cryptohome/dircrypto_data_migrator/migration_helper.h"
#include "cryptohome/file_system_keys.h"
#include "cryptohome/homedirs.h"
#include "cryptohome/migration_type.h"
#include "cryptohome/mount_constants.h"
#include "cryptohome/mount_helper.h"
#include "cryptohome/out_of_process_mount_helper.h"
#include "cryptohome/platform.h"
#include "cryptohome/timestamp.pb.h"
#include "cryptohome/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

// Name of the key file.
extern const char kKeyFile[];
// Automatic label prefix of a legacy key ("%s%d")
extern const char kKeyLegacyPrefix[];
// Maximum number of key files.
extern const int kKeyFileMax;

class ChapsClientFactory;
class UserOldestActivityTimestampCache;

// The Mount class handles mounting/unmounting of the user's cryptohome
// directory.
class Mount : public base::RefCountedThreadSafe<Mount> {
 public:
  // Called before mount cryptohome.
  using PreMountCallback = base::RepeatingCallback<void()>;

  struct MountArgs {
    bool create_if_missing = false;
    // Whether the mount has to be ephemeral.
    bool is_ephemeral = false;
    // When creating a new cryptohome from scratch, use ecryptfs.
    bool create_as_ecryptfs = false;
    // Forces dircrypto, i.e., makes it an error to mount ecryptfs.
    bool force_dircrypto = false;
    // Mount the existing ecryptfs vault to a temporary location while setting
    // up a new dircrypto directory.
    bool to_migrate_from_ecryptfs = false;
    // Only mount in shadow tree, don't expose the usual /home/(user)
    // directories.
    bool shadow_only = false;
  };

  // Sets up Mount with the default locations, username, etc., as defined above.
  Mount();

  virtual ~Mount();

  // Gets the uid/gid of the default user and loads the system salt
  virtual bool Init(Platform* platform,
                    Crypto* crypto,
                    UserOldestActivityTimestampCache* cache);

  // Makes mount type-specific preparation.
  //
  // Parameters
  //   obfuscated_username - salted hash of the username
  //   force_ecryptfs - force ECRYPTFS
  virtual bool PrepareCryptohome(const std::string& obfuscated_username,
                                 bool force_ecryptfs);

  // Attempts to mount the cryptohome for the given username
  //
  // Parameters
  //   username - name of the user to mount
  //   file_system_keys - file system encryption keys of the user
  //   mount_args - The options for the call to mount:
  //                * Whether to create the cryptohome if it doesn't exist.
  //                * Whether to ensure that the mount is ephemeral.
  //   is_pristine - Whether it is the first mount of the vault.
  //   error - The specific error condition on failure
  virtual bool MountCryptohome(const std::string& username,
                               const FileSystemKeys& file_system_keys,
                               const MountArgs& mount_args,
                               bool is_pristine,
                               MountError* error);

  // Attempts to mount an ephemeral cryptohome for the given username.
  //
  // Parameters
  //   username - name of the user to mount
  virtual MountError MountEphemeralCryptohome(const std::string& username);

  // Mounts a guest home directory to the cryptohome mount point.
  virtual bool MountGuestCryptohome();

  // Unmounts any mount at the cryptohome mount point
  virtual bool UnmountCryptohome();

  // Checks whether the mount point currently has a cryptohome mounted for the
  // current user.
  virtual bool IsMounted() const;

  // Checks whether the mount point currently has a cryptohome mounted for the
  // current user that is not ephemeral.
  //
  virtual bool IsNonEphemeralMounted() const;

  // Updates current user activity timestamp. This is called daily.
  // So we may not consider current user as old (and delete it soon after they
  // log off). Returns true if current user is logged in and timestamp was
  // updated.
  // If no user is logged or the mount is ephemeral, nothing is done and false
  // is returned.
  //
  // Parameters
  //   time_shift_sec - normally must be 0. Shifts the updated time backwards
  //                    by specified number of seconds. Used in manual tests.
  //   active_key_index - index of the active keyset
  virtual bool UpdateCurrentUserActivityTimestamp(int time_shift_sec,
                                                  int active_key_index);

  // Used to override the default shadow root
  void set_shadow_root(const base::FilePath& value) { shadow_root_ = value; }

  // Used to override the default skeleton directory
  void set_skel_source(const base::FilePath& value) { skel_source_ = value; }

  // Used to override the default Crypto handler (does not take ownership)
  void set_crypto(Crypto* value) { crypto_ = value; }

  // Get the Crypto instance
  virtual Crypto* crypto() { return crypto_; }

  // Used to override the default HomeDirs handler (does not take ownership)
  void set_homedirs(HomeDirs* value) { homedirs_ = value; }

  // Get the HomeDirs instance
  virtual HomeDirs* homedirs() { return homedirs_; }

  virtual Platform* platform() { return platform_; }

  virtual const base::FilePath& mount_point() const { return mount_point_; }

  // Used to override the default Platform handler (does not take ownership)
  void set_platform(Platform* value) { platform_ = value; }

  // Override whether to use the TPM for added security
  void set_use_tpm(bool value) { use_tpm_ = value; }

  // Set/get a flag, that this machine is enterprise owned.
  void set_enterprise_owned(bool value) {
    enterprise_owned_ = value;
    homedirs_->set_enterprise_owned(value);
  }

  // Flag indicating if PKCS#11 is ready.
  typedef enum {
    kUninitialized = 0,   // PKCS#11 initialization hasn't been attempted.
    kIsWaitingOnTPM,      // PKCS#11 initialization is waiting on TPM ownership,
    kIsBeingInitialized,  // PKCS#11 is being attempted asynchronously.
    kIsInitialized,       // PKCS#11 was attempted and succeeded.
    kIsFailed,            // PKCS#11 was attempted and failed.
    kInvalidState,        // We should never be in this state.
  } Pkcs11State;

  virtual void set_pkcs11_state(Pkcs11State value) { pkcs11_state_ = value; }

  virtual Pkcs11State pkcs11_state() { return pkcs11_state_; }

  // Returns the status of this mount as a Value
  //
  // The returned object is a dictionary whose keys describe the mount. Current
  // keys are: "keysets", "mounted", "owner", "enterprise", and "type".
  virtual base::Value GetStatus(int active_key_index);

  // Inserts the current user's PKCS #11 token.
  virtual bool InsertPkcs11Token();

  // Removes the current user's PKCS #11 token.
  virtual void RemovePkcs11Token();

  // Returns true if this Mount instances owns the mount path.
  virtual bool OwnsMountPoint(const base::FilePath& path) const;

  // Migrates the data from eCryptfs to dircrypto.
  // Call MountCryptohome with to_migrate_from_ecryptfs beforehand.
  // If |migration_type| is MINIMAL, no progress reporting will be done and only
  // allowlisted paths will be migrated.
  virtual bool MigrateToDircrypto(
      const dircrypto_data_migrator::MigrationHelper::ProgressCallback&
          callback,
      MigrationType migration_type);

  // Cancels the active dircrypto migration if there is, and wait for it to
  // stop.
  void MaybeCancelActiveDircryptoMigrationAndWait();

  // Returns true if this Mount was mounted with |shadow_only|=true. This is
  // only valid when IsMounted() is true.
  bool IsShadowOnly() const;

  // Returns the WebAuthn secret and clears it from memory.
  std::unique_ptr<brillo::SecureBlob> GetWebAuthnSecret();

  // Used to override the policy provider for testing (takes ownership)
  // TODO(wad) move this in line with other testing accessors
  void set_policy_provider(policy::PolicyProvider* provider) {
    policy_provider_.reset(provider);
    homedirs_->set_policy_provider(provider);
  }

  void set_legacy_mount(bool legacy) { legacy_mount_ = legacy; }

  // Does not take ownership.
  void set_chaps_client_factory(ChapsClientFactory* factory) {
    chaps_client_factory_ = factory;
  }

 protected:
  // Only used in tests.
  void set_mount_guest_session_out_of_process(bool oop) {
    mount_guest_session_out_of_process_ = oop;
  }
  void set_mount_non_ephemeral_session_out_of_process(bool oop) {
    mount_non_ephemeral_session_out_of_process_ = oop;
  }
  void set_mount_guest_session_non_root_namespace(bool non_root_ns) {
    mount_guest_session_non_root_namespace_ = non_root_ns;
  }

  FRIEND_TEST(ServiceInterfaceTest, CheckAsyncTestCredentials);
  friend class MakeTests;
  friend class MountTest;
  friend class ChapsDirectoryTest;
  friend class EphemeralNoUserSystemTest;

 private:
  // Creates the tracked subdirectories in a user's cryptohome
  // If the cryptohome did not have tracked directories, but had them untracked,
  // migrates their contents.
  //
  // Parameters
  //   username - name of the user to create directories for
  virtual bool CreateTrackedSubdirectories(const std::string& username) const;

  // Determine the mount type of the existing vault.
  MountType DeriveVaultMountType(const std::string& obfuscated_username,
                                 bool shall_migrate) const;

  // Choose the mount type for the new vault.
  MountType ChooseVaultMountType(bool force_ecryptfs) const;

  virtual bool AddEcryptfsAuthToken(const FileSystemKeys& file_system_keys,
                                    std::string* key_signature,
                                    std::string* filename_key_signature) const;

  virtual bool StoreVaultKeysetForUser(const std::string& obfuscated_username,
                                       VaultKeyset* vault_keyset) const;

  virtual bool StoreTimestampForUser(const std::string& obfuscated_username,
                                     VaultKeyset* vault_keyset) const;

  base::FilePath GetUserTimestampFileForUser(
      const std::string& obfuscated_username, int index) const;

  // Gets the user's key file name by index
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the Credentials
  //   index - which key file to load
  base::FilePath GetUserLegacyKeyFileForUser(
      const std::string& obfuscated_username, int index) const;

  // Gets the directory in the shadow root where the user's salt, key, and vault
  // are stored.
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the Credentials
  base::FilePath GetUserDirectoryForUser(
      const std::string& obfuscated_username) const;

  // Gets the directory to mount the user's ephemeral cryptohome at.
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the credentials.
  base::FilePath GetUserEphemeralMountDirectory(
      const std::string& obfuscated_username) const;

  // Gets the directory to temporarily mount the user's cryptohome at.
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the credentials.
  base::FilePath GetUserTemporaryMountDirectory(
      const std::string& obfuscated_username) const;

  // Returns the path of a user passthrough inside a vault
  //
  // Parameters
  //   vault - vault path
  base::FilePath VaultPathToUserPath(const base::FilePath& vault) const;

  // Returns the path of a root passthrough inside a vault
  //
  // Parameters
  //   vault - vault path
  base::FilePath VaultPathToRootPath(const base::FilePath& vault) const;

  // Returns the mounted userhome path for ephemeral user
  // (e.g. /home/.shadow/.../ephemeral-mount/user)
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the credentials.
  base::FilePath GetMountedEphemeralUserHomePath(
      const std::string& obfuscated_username) const;

  // Returns the mounted roothome path for ephemeral user (
  // e.g. /home/.shadow/.../ephemeral-mount/root)
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the credentials.
  base::FilePath GetMountedEphemeralRootHomePath(
      const std::string& obfuscated_username) const;

  // Checks Chaps Directory and makes sure that it has the correct
  // permissions, owner uid and gid. If any of these values are
  // incorrect, the correct values are set. If the directory
  // does not exist, it is created and initialzed with the correct
  // values. If the directory or its attributes cannot be checked,
  // set or created, a fatal error has occured and the function
  // returns false.
  //
  // Parameters
  //   dir - directory to check
  bool CheckChapsDirectory(const base::FilePath& dir);

  // Mounts and populates an ephemeral cryptohome backed by tmpfs for the given
  // user.
  //
  // Parameters
  //   username - Username for the user
  //   ephemeral_mounter - Mounter class to use. Allows mounting Guest sessions
  //                       out of process
  //   cleanup - Closure to use in UnmountCryptohome(), and to clean up in case
  //             of failure
  bool MountEphemeralCryptohomeInternal(const std::string& username,
                                        MountHelperInterface* ephemeral_mounter,
                                        base::Closure cleanup);

  // Tears down an ephemeral cryptohome mount, and deletes the underlying loop
  // device and sparse file.
  void TearDownEphemeralMount();

  // Unmounts all mount points, and invalidates the dircrypto encryption key.
  // Relies on ForceUnmount() internally; see the caveat listed for it
  void UnmountAndDropKeys(base::OnceClosure unmounter);

  // Derives PKCS #11 token authorization data from a passkey. This may take up
  // to ~100ms (dependant on CPU / memory performance). Returns true on success.
  bool DeriveTokenAuthData(const brillo::SecureBlob& passkey,
                           std::string* auth_data);

  // Computes a public derivative from |fek| and |fnek| for u2fd to fetch.
  void PrepareWebAuthnSecret(const std::string& obfuscated_username,
                             const brillo::SecureBlob& fek,
                             const brillo::SecureBlob& fnek);
  // Clears the WebAuthn secret if it's not read yet.
  void ClearWebAuthnSecret();

  // The uid of the shared user.  Ownership of the user's vault is set to this
  // uid.
  uid_t default_user_;

  // The uid of the chaps user. Ownership of the user's PKCS #11 token directory
  // is set to this uid.
  uid_t chaps_user_;

  // The gid of the shared user.  Ownership of the user's vault is set to this
  // gid.
  gid_t default_group_;

  // The gid of the shared access group.  Ownership of the user's home and
  // Downloads directory to this gid.
  gid_t default_access_group_;

  // The file path to mount cryptohome at.  Defaults to /home/chronos/user
  base::FilePath mount_point_;

  // Where to store the system salt and user salt/key/vault.  Defaults to
  // /home/.shadow
  base::FilePath shadow_root_;

  // Where the skeleton for the user's cryptohome is copied from
  base::FilePath skel_source_;

  // Stores the global system salt
  brillo::SecureBlob system_salt_;

  // The platform-specific calls
  std::unique_ptr<Platform> default_platform_;
  Platform* platform_;

  // The crypto implementation
  Crypto* crypto_;

  // TODO(wad,ellyjones) Require HomeDirs at Init().
  // HomeDirs encapsulates operations on Cryptohomes at rest.
  std::unique_ptr<HomeDirs> default_homedirs_;
  HomeDirs* homedirs_;

  // Whether to use the TPM for added security
  bool use_tpm_;

  // Cache of last access timestamp for existing users.
  UserOldestActivityTimestampCache* user_timestamp_cache_;

  // Used to retrieve the owner user.  Currently used only for tests.
  std::unique_ptr<policy::PolicyProvider> policy_provider_;

  // True if the machine is enterprise owned, false if not or we have
  // not discovered it in this session.
  bool enterprise_owned_;

  // Name of the user the mount belongs to.
  std::string username_;

  Pkcs11State pkcs11_state_;

  // Used to track the user's passkey. PKCS #11 initialization consumes and
  // clears this value.
  brillo::SecureBlob pkcs11_token_auth_data_;

  // Dircrypto key reference.
  dircrypto::KeyReference dircrypto_key_reference_;

  // Whether to mount the legacy homedir or not (see MountLegacyHome)
  bool legacy_mount_;

  // Indicates the type of the current mount.
  // This is only valid when IsMounted() is true.
  MountType mount_type_;

  // true if mounted with |shadow_only|=true. This is only valid when
  // IsMounted() is true.
  bool shadow_only_;

  std::unique_ptr<ChapsClientFactory> default_chaps_client_factory_;
  ChapsClientFactory* chaps_client_factory_;

  dircrypto_data_migrator::MigrationHelper* active_dircrypto_migrator_ =
      nullptr;
  bool is_dircrypto_migration_cancelled_ = false;
  base::Lock active_dircrypto_migrator_lock_;
  base::ConditionVariable dircrypto_migration_stopped_condition_;

  // |mounter_| encapsulates mount(2)/umount(2) operations required to perform
  // and tear down cryptohome mounts. It performs these operations in-process.
  std::unique_ptr<MountHelper> mounter_;

  // |out_of_process_mounter_| also encapsulates mount(2) and umount(2)
  // operations, but will perform these operations out-of-process.
  // This is currently only used for Guest sessions.
  bool mount_guest_session_out_of_process_;
  // Use the |out-of-process mounter_| for non-ephemeral sessions.
  bool mount_non_ephemeral_session_out_of_process_;
  bool mount_guest_session_non_root_namespace_;
  std::unique_ptr<OutOfProcessMountHelper> out_of_process_mounter_;

  // Secret for WebAuthn credentials.
  std::unique_ptr<brillo::SecureBlob> webauthn_secret_;
  // Timer for clearing the |webauthn_secret_| if not read.
  base::OneShotTimer clear_webauthn_secret_timer_;

  // This closure will be run in UnmountCryptohome().
  base::OnceClosure mount_cleanup_;

  FRIEND_TEST(MountTest, NamespaceCreationPass);
  FRIEND_TEST(MountTest, NamespaceCreationFail);
  FRIEND_TEST(MountTest, RememberMountOrderingTest);
  FRIEND_TEST(MountTest, UserActivityTimestampUpdated);
  FRIEND_TEST(MountTest, CreateCryptohomeTest);
  FRIEND_TEST(MountTest, CreateTrackedSubdirectories);
  FRIEND_TEST(MountTest, CreateTrackedSubdirectoriesReplaceExistingDir);
  FRIEND_TEST(EphemeralNoUserSystemTest, CreateMyFilesDownloads);
  FRIEND_TEST(EphemeralNoUserSystemTest, CreateMyFilesDownloadsAlreadyExists);
  FRIEND_TEST(EphemeralNoUserSystemTest, MountGuestUserDir);

  DISALLOW_COPY_AND_ASSIGN(Mount);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOUNT_H_
