// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Mount.

#include "cryptohome/storage/mount.h"

#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <map>
#include <memory>
#include <set>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/hash/sha1.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>
#include <base/values.h>
#include <chaps/isolate.h>
#include <chaps/token_manager_client.h>
#include <brillo/cryptohome.h>
#include <brillo/process/process.h>
#include <brillo/scoped_umask.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>
#include <google/protobuf/util/message_differencer.h>

#include "cryptohome/chaps_client_factory.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/dircrypto_data_migrator/migration_helper.h"
#include "cryptohome/dircrypto_util.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/pkcs11_init.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount_utils.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

using base::FilePath;
using base::StringPrintf;
using brillo::BlobToString;
using brillo::SecureBlob;
using brillo::cryptohome::home::GetRootPath;
using brillo::cryptohome::home::GetUserPath;
using brillo::cryptohome::home::IsSanitizedUserName;
using brillo::cryptohome::home::kGuestUserName;
using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;
using chaps::IsolateCredentialManager;
using google::protobuf::util::MessageDifferencer;

namespace {
constexpr bool __attribute__((unused)) MountUserSessionOOP() {
  return USE_MOUNT_OOP;
}

}  // namespace

namespace cryptohome {

const char kChapsUserName[] = "chaps";
const char kDefaultSharedAccessGroup[] = "chronos-access";

// Message to use when generating a secret for WebAuthn.
const char kWebAuthnSecretHmacMessage[] = "AuthTimeWebAuthnSecret";

void StartUserFileAttrsCleanerService(cryptohome::Platform* platform,
                                      const std::string& username) {
  std::unique_ptr<brillo::Process> file_attrs =
      platform->CreateProcessInstance();

  file_attrs->AddArg("/sbin/initctl");
  file_attrs->AddArg("start");
  file_attrs->AddArg("--no-wait");
  file_attrs->AddArg("file_attrs_cleaner_tool");
  file_attrs->AddArg(
      base::StringPrintf("OBFUSCATED_USERNAME=%s", username.c_str()));

  if (file_attrs->Run() != 0)
    PLOG(WARNING) << "Error while running file_attrs_cleaner_tool";
}

Mount::Mount(Platform* platform, HomeDirs* homedirs)
    : default_user_(-1),
      chaps_user_(-1),
      default_group_(-1),
      default_access_group_(-1),
      system_salt_(),
      platform_(platform),
      homedirs_(homedirs),
      pkcs11_state_(kUninitialized),
      dircrypto_key_reference_(),
      legacy_mount_(true),
      bind_mount_downloads_(true),
      mount_type_(MountType::NONE),
      shadow_only_(false),
      default_chaps_client_factory_(new ChapsClientFactory()),
      chaps_client_factory_(default_chaps_client_factory_.get()),
      dircrypto_migration_stopped_condition_(&active_dircrypto_migrator_lock_),
      mount_guest_session_out_of_process_(true),
      mount_non_ephemeral_session_out_of_process_(true),
      mount_guest_session_non_root_namespace_(true) {}

Mount::Mount() : Mount(nullptr, nullptr) {}

Mount::~Mount() {
  if (IsMounted())
    UnmountCryptohome();
}

bool Mount::Init() {
  bool result = true;

  // Get the user id and group id of the default user
  if (!platform_->GetUserId(kDefaultSharedUser, &default_user_,
                            &default_group_)) {
    result = false;
  }

  // Get the user id of the chaps user.
  gid_t not_used;
  if (!platform_->GetUserId(kChapsUserName, &chaps_user_, &not_used)) {
    result = false;
  }

  // Get the group id of the default shared access group.
  if (!platform_->GetGroupId(kDefaultSharedAccessGroup,
                             &default_access_group_)) {
    result = false;
  }

  // One-time load of the global system salt (used in generating username
  // hashes)
  if (!homedirs_->GetSystemSalt(&system_salt_)) {
    LOG(ERROR) << "Failed to load or create the system salt";
    result = false;
  }

  mounter_.reset(new MountHelper(
      default_user_, default_group_, default_access_group_, system_salt_,
      legacy_mount_, bind_mount_downloads_, platform_));

  std::unique_ptr<MountNamespace> chrome_mnt_ns;
  if (mount_guest_session_non_root_namespace_ || IsolateUserSession()) {
    chrome_mnt_ns = std::make_unique<MountNamespace>(
        base::FilePath(kUserSessionMountNamespacePath), platform_);
  }

  // When the |user_session_isolation| USE flag is set, the mount namespace for
  // both Guest and regular sessions will be created by session_manager.
  if (mount_guest_session_non_root_namespace_ && !IsolateUserSession()) {
    if (!chrome_mnt_ns->Create()) {
      std::string message =
          base::StringPrintf("Failed to create mount namespace at %s",
                             kUserSessionMountNamespacePath);
      cryptohome::ForkAndCrash(message);
      result = false;
    }
  }

  if (mount_guest_session_out_of_process_ ||
      mount_non_ephemeral_session_out_of_process_) {
    out_of_process_mounter_.reset(new OutOfProcessMountHelper(
        system_salt_, std::move(chrome_mnt_ns), legacy_mount_,
        bind_mount_downloads_, platform_));
  }

  return result;
}

MountType Mount::DeriveVaultMountType(const std::string& obfuscated_username,
                                      bool shall_migrate) const {
  FilePath ecryptfs_vault_path = GetEcryptfsUserVaultPath(obfuscated_username);
  bool ecryptfs_vault_exists = platform_->DirectoryExists(ecryptfs_vault_path);

  if (ecryptfs_vault_exists) {
    // Keep legacy ecryptfs of migrate to dir_crypto.
    return shall_migrate ? MountType::DIR_CRYPTO : MountType::ECRYPTFS;
  } else {
    // no ecrypfs vault means we have dir_crypto setup.
    if (shall_migrate) {
      LOG(ERROR) << "No eCryptfs vault to migrate.";
      return MountType::NONE;
    }
    return MountType::DIR_CRYPTO;
  }
}

MountType Mount::ChooseVaultMountType(bool force_ecryptfs) const {
  if (force_ecryptfs) {
    return MountType::ECRYPTFS;
  }

  dircrypto::KeyState state = platform_->GetDirCryptoKeyState(ShadowRoot());
  switch (state) {
    case dircrypto::KeyState::NOT_SUPPORTED:
      return MountType::ECRYPTFS;
    case dircrypto::KeyState::NO_KEY:
      return MountType::DIR_CRYPTO;
    case dircrypto::KeyState::UNKNOWN:
    case dircrypto::KeyState::ENCRYPTED:
      LOG(ERROR) << "Unexpected state " << static_cast<int>(state);
      return MountType::NONE;
  }
}

bool Mount::AddEcryptfsAuthToken(const FileSystemKeyset& file_system_keyset,
                                 std::string* key_signature,
                                 std::string* filename_key_signature) const {
  // Add the File Encryption key (FEK) from the vault keyset.  This is the key
  // that is used to encrypt the file contents when the file is persisted to the
  // lower filesystem by eCryptfs.
  *key_signature =
      CryptoLib::SecureBlobToHex(file_system_keyset.KeyReference().fek_sig);
  if (!platform_->AddEcryptfsAuthToken(file_system_keyset.Key().fek,
                                       *key_signature,
                                       file_system_keyset.Key().fek_salt)) {
    LOG(ERROR) << "Couldn't add eCryptfs file encryption key to keyring.";
    return false;
  }

  // Add the File Name Encryption Key (FNEK) from the vault keyset.  This is the
  // key that is used to encrypt the file name when the file is persisted to the
  // lower filesystem by eCryptfs.
  *filename_key_signature =
      CryptoLib::SecureBlobToHex(file_system_keyset.KeyReference().fnek_sig);
  if (!platform_->AddEcryptfsAuthToken(file_system_keyset.Key().fnek,
                                       *filename_key_signature,
                                       file_system_keyset.Key().fnek_salt)) {
    LOG(ERROR) << "Couldn't add eCryptfs filename encryption key to keyring.";
    return false;
  }

  return true;
}

MountError Mount::MountEphemeralCryptohome(const std::string& username) {
  username_ = username;

  if (homedirs_->IsOrWillBeOwner(username_)) {
    return MOUNT_ERROR_EPHEMERAL_MOUNT_BY_OWNER;
  }

  // Ephemeral mounts don't require dropping keys since they're not dircrypto
  // mounts. This callback will be executed in the destructor at the latest so
  // |this| will always be valid.
  base::Closure cleanup =
      base::Bind(&Mount::TearDownEphemeralMount, base::Unretained(this));

  // Ephemeral cryptohomes for regular users are mounted in-process.
  if (!MountEphemeralCryptohomeInternal(username_, mounter_.get(),
                                        std::move(cleanup))) {
    homedirs_->Remove(username_);
    return MOUNT_ERROR_FATAL;
  }

  return MOUNT_ERROR_NONE;
}

bool Mount::PrepareCryptohome(const std::string& obfuscated_username,
                              bool force_ecryptfs) {
  MountType mount_type = ChooseVaultMountType(force_ecryptfs);
  if (mount_type == MountType::ECRYPTFS) {
    // Create the user's vault.
    FilePath vault_path = GetEcryptfsUserVaultPath(obfuscated_username);
    if (!platform_->CreateDirectory(vault_path)) {
      LOG(ERROR) << "Couldn't create vault path: " << vault_path.value();
      return false;
    }
  }
  return true;
}

bool Mount::MountCryptohome(const std::string& username,
                            const FileSystemKeyset& file_system_keyset,
                            const Mount::MountArgs& mount_args,
                            bool is_pristine,
                            MountError* mount_error) {
  username_ = username;
  std::string obfuscated_username =
      SanitizeUserNameWithSalt(username_, system_salt_);

  if (!mount_args.shadow_only) {
    if (!mounter_->EnsureUserMountPoints(username_)) {
      LOG(ERROR) << "Error creating mountpoint.";
      *mount_error = MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED;
      return false;
    }
  }

  mount_type_ = DeriveVaultMountType(obfuscated_username,
                                     mount_args.to_migrate_from_ecryptfs);
  if (mount_type_ == MountType::NONE) {
    // TODO(dlunev): there should be a more proper error code set. CREATE_FAILED
    // is a temporary returned error to keep the behaviour unchanged while
    // refactoring.
    *mount_error = MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED;
    return false;
  }

  pkcs11_token_auth_data_ = file_system_keyset.chaps_key();
  if (!platform_->ClearUserKeyring()) {
    LOG(ERROR) << "Failed to clear user keyring";
  }

  // Checks whether migration from ecryptfs to dircrypto is needed, and returns
  // an error when necessary.
  if (homedirs_->EcryptfsCryptohomeExists(obfuscated_username) &&
      homedirs_->DircryptoCryptohomeExists(obfuscated_username) &&
      !mount_args.to_migrate_from_ecryptfs) {
    // If both types of home directory existed, it implies that the migration
    // attempt was aborted in the middle before doing clean up.
    LOG(ERROR) << "Mount failed because both eCryptfs and dircrypto home"
               << " directories were found. Need to resume and finish"
               << " migration first.";
    *mount_error = MOUNT_ERROR_PREVIOUS_MIGRATION_INCOMPLETE;
    return false;
  }

  if (mount_type_ == MountType::ECRYPTFS && mount_args.force_dircrypto) {
    // If dircrypto is forced, it's an error to mount ecryptfs home.
    LOG(ERROR) << "Mount attempt with force_dircrypto on eCryptfs.";
    *mount_error = MOUNT_ERROR_OLD_ENCRYPTION;
    return false;
  }

  if (!platform_->SetupProcessKeyring()) {
    LOG(ERROR) << "Failed to set up a process keyring.";
    *mount_error = MOUNT_ERROR_SETUP_PROCESS_KEYRING_FAILED;
    return false;
  }
  // When migrating, mount both eCryptfs and dircrypto.
  const bool should_mount_ecryptfs =
      mount_type_ == MountType::ECRYPTFS || mount_args.to_migrate_from_ecryptfs;
  const bool should_mount_dircrypto = mount_type_ == MountType::DIR_CRYPTO;
  if (!should_mount_ecryptfs && !should_mount_dircrypto) {
    NOTREACHED() << "Unexpected mount type " << static_cast<int>(mount_type_);
    *mount_error = MOUNT_ERROR_UNEXPECTED_MOUNT_TYPE;
    return false;
  }

  MountHelperInterface* helper;
  if (mount_non_ephemeral_session_out_of_process_) {
    helper = out_of_process_mounter_.get();
  } else {
    helper = mounter_.get();
  }
  // Ensure we don't leave any mounts hanging on intermediate errors.
  // The closure won't outlive the class so |this| will always be valid.
  // |out_of_process_mounter_|/|mounter_| will always be valid since this
  // callback runs in the destructor at the latest.
  base::ScopedClosureRunner unmount_and_drop_keys_runner(base::BindOnce(
      &Mount::UnmountAndDropKeys, base::Unretained(this),
      base::BindOnce(&MountHelperInterface::TearDownNonEphemeralMount,
                     base::Unretained(helper))));

  std::string key_signature, fnek_signature;
  if (should_mount_ecryptfs) {
    // Add the decrypted key to the keyring so that ecryptfs can use it.
    if (!AddEcryptfsAuthToken(file_system_keyset, &key_signature,
                              &fnek_signature)) {
      LOG(ERROR) << "Error adding eCryptfs keys.";
      *mount_error = MOUNT_ERROR_KEYRING_FAILED;
      return false;
    }
  }
  if (should_mount_dircrypto) {
    dircrypto_key_reference_.policy_version =
        dircrypto::GetDirectoryPolicyVersion(
            GetUserMountDirectory(obfuscated_username));
    if (dircrypto_key_reference_.policy_version < 0) {
      dircrypto_key_reference_.policy_version =
          dircrypto::CheckFscryptKeyIoctlSupport() ? FSCRYPT_POLICY_V2
                                                   : FSCRYPT_POLICY_V1;
    }
    dircrypto_key_reference_.reference =
        file_system_keyset.KeyReference().fek_sig;
    if (!platform_->AddDirCryptoKeyToKeyring(file_system_keyset.Key().fek,
                                             &dircrypto_key_reference_)) {
      LOG(ERROR) << "Error adding dircrypto key.";
      *mount_error = MOUNT_ERROR_KEYRING_FAILED;
      return false;
    }
  }

  // Mount cryptohome
  // /home/.shadow: owned by root
  // /home/.shadow/$hash: owned by root
  // /home/.shadow/$hash/vault: owned by root
  // /home/.shadow/$hash/mount: owned by root
  // /home/.shadow/$hash/mount/root: owned by root
  // /home/.shadow/$hash/mount/user: owned by chronos
  // /home/chronos: owned by chronos
  // /home/chronos/user: owned by chronos
  // /home/user/$hash: owned by chronos
  // /home/root/$hash: owned by root

  mount_point_ = GetUserMountDirectory(obfuscated_username);
  if (!platform_->CreateDirectory(mount_point_)) {
    PLOG(ERROR) << "User mount directory creation failed for "
                << mount_point_.value();
    *mount_error = MOUNT_ERROR_DIR_CREATION_FAILED;
    return false;
  }
  if (mount_args.to_migrate_from_ecryptfs) {
    FilePath temporary_mount_point =
        GetUserTemporaryMountDirectory(obfuscated_username);
    if (!platform_->CreateDirectory(temporary_mount_point)) {
      PLOG(ERROR) << "User temporary mount directory creation failed for "
                  << temporary_mount_point.value();
      *mount_error = MOUNT_ERROR_DIR_CREATION_FAILED;
      return false;
    }
  }

  // Since Service::Mount cleans up stale mounts, we should only reach
  // this point if someone attempts to re-mount an in-use mount point.
  if (platform_->IsDirectoryMounted(mount_point_)) {
    LOG(ERROR) << "Mount point is busy: " << mount_point_.value();
    *mount_error = MOUNT_ERROR_FATAL;
    return false;
  }

  if (should_mount_dircrypto) {
    if (!platform_->SetDirCryptoKey(mount_point_, dircrypto_key_reference_)) {
      LOG(ERROR) << "Failed to set directory encryption policy for "
                 << mount_point_.value();
      *mount_error = MOUNT_ERROR_SET_DIR_CRYPTO_KEY_FAILED;
      return false;
    }
  }

  MountHelper::Options mount_opts = {
      mount_type_, mount_args.to_migrate_from_ecryptfs, mount_args.shadow_only};

  cryptohome::ReportTimerStart(cryptohome::kPerformMountTimer);
  if (!helper->PerformMount(mount_opts, username_, key_signature,
                            fnek_signature, is_pristine, mount_error)) {
    LOG(ERROR) << "MountHelper::PerformMount failed, error = " << *mount_error;
    return false;
  }

  cryptohome::ReportTimerStop(cryptohome::kPerformMountTimer);

  // TODO(chromium:1147601): Move this into user_session.cc and add unittests
  // when service.cc is gone.
  PrepareWebAuthnSecret(obfuscated_username, file_system_keyset.Key().fek,
                        file_system_keyset.Key().fnek);

  // At this point we're done mounting so move the clean-up closure to the
  // instance variable.
  mount_cleanup_ = unmount_and_drop_keys_runner.Release();

  *mount_error = MOUNT_ERROR_NONE;

  switch (mount_type_) {
    case MountType::ECRYPTFS:
      ReportHomedirEncryptionType(HomedirEncryptionType::kEcryptfs);
      break;
    case MountType::DIR_CRYPTO:
      ReportHomedirEncryptionType(HomedirEncryptionType::kDircrypto);
      break;
    default:
      // We're only interested in encrypted home directories.
      NOTREACHED() << "Unknown homedir encryption type: "
                   << static_cast<int>(mount_type_);
      break;
  }

  // Start file attribute cleaner service.
  StartUserFileAttrsCleanerService(platform_, obfuscated_username);

  // TODO(fqj,b/116072767) Ignore errors since unlabeled files are currently
  // still okay during current development progress.
  platform_->RestoreSELinuxContexts(GetUserMountDirectory(obfuscated_username),
                                    true);

  return true;
}

bool Mount::MountEphemeralCryptohomeInternal(
    const std::string& username,
    MountHelperInterface* ephemeral_mounter,
    base::Closure cleanup) {
  // Ephemeral cryptohome can't be mounted twice.
  CHECK(ephemeral_mounter->CanPerformEphemeralMount());

  base::ScopedClosureRunner cleanup_runner(cleanup);

  if (!ephemeral_mounter->PerformEphemeralMount(username)) {
    LOG(ERROR) << "PerformEphemeralMount() failed, aborting ephemeral mount";
    return false;
  }

  // Mount succeeded, move the clean-up closure to the instance variable.
  mount_cleanup_ = cleanup_runner.Release();

  mount_type_ = MountType::EPHEMERAL;
  return true;
}

void Mount::PrepareWebAuthnSecret(const std::string& obfuscated_username,
                                  const brillo::SecureBlob& fek,
                                  const brillo::SecureBlob& fnek) {
  // This WebAuthn secret can be rederived upon in-session user auth success
  // since they will unlock the vault keyset.
  const std::string message(kWebAuthnSecretHmacMessage);
  webauthn_secret_ = std::make_unique<brillo::SecureBlob>(
      CryptoLib::HmacSha256(brillo::SecureBlob::Combine(fnek, fek),
                            brillo::Blob(message.cbegin(), message.cend())));
  clear_webauthn_secret_timer_.Start(
      FROM_HERE, base::TimeDelta::FromSeconds(5),
      base::BindOnce(&Mount::ClearWebAuthnSecret, base::Unretained(this)));
}

void Mount::ClearWebAuthnSecret() {
  webauthn_secret_.reset();
}

std::unique_ptr<brillo::SecureBlob> Mount::GetWebAuthnSecret() {
  return std::move(webauthn_secret_);
}

void Mount::TearDownEphemeralMount() {
  if (!mounter_->TearDownEphemeralMount()) {
    ReportCryptohomeError(kEphemeralCleanUpFailed);
  }
}

void Mount::UnmountAndDropKeys(base::OnceClosure unmounter) {
  std::move(unmounter).Run();

  // Invalidate dircrypto key to make directory contents inaccessible.
  if (!dircrypto_key_reference_.reference.empty()) {
    bool result = platform_->InvalidateDirCryptoKey(dircrypto_key_reference_,
                                                    ShadowRoot());
    if (!result) {
      // TODO(crbug.com/1116109): We should think about what to do after this
      // operation failed.
      LOG(ERROR) << "Failed to invalidate dircrypto key";
    }
    ReportInvalidateDirCryptoKeyResult(result);
    dircrypto_key_reference_.policy_version = FSCRYPT_POLICY_V1;
    dircrypto_key_reference_.reference.clear();
  }
}

bool Mount::UnmountCryptohome() {
  // There should be no file access when unmounting.
  // Stop dircrypto migration if in progress.
  MaybeCancelActiveDircryptoMigrationAndWait();

  if (!mount_cleanup_.is_null()) {
    std::move(mount_cleanup_).Run();
  }

  if (homedirs_->AreEphemeralUsersEnabled())
    homedirs_->RemoveNonOwnerCryptohomes();

  RemovePkcs11Token();
  mount_type_ = MountType::NONE;

  platform_->ClearUserKeyring();

  return true;
}

bool Mount::IsMounted() const {
  return (mounter_ && mounter_->MountPerformed()) ||
         (out_of_process_mounter_ && out_of_process_mounter_->MountPerformed());
}

bool Mount::IsNonEphemeralMounted() const {
  return IsMounted() && mount_type_ != MountType::EPHEMERAL;
}

bool Mount::OwnsMountPoint(const FilePath& path) const {
  return (mounter_ && mounter_->IsPathMounted(path)) ||
         (out_of_process_mounter_ &&
          out_of_process_mounter_->IsPathMounted(path));
}

bool Mount::CreateTrackedSubdirectories(const std::string& username) const {
  std::string obfuscated_username =
      SanitizeUserNameWithSalt(username, system_salt_);
  return mounter_->CreateTrackedSubdirectories(obfuscated_username,
                                               mount_type_);
}

bool Mount::MountGuestCryptohome() {
  username_ = "";
  MountHelperInterface* ephemeral_mounter = nullptr;
  base::Closure cleanup;

  if (mount_guest_session_out_of_process_) {
    // Ephemeral cryptohomes for Guest sessions are mounted out-of-process.
    ephemeral_mounter = out_of_process_mounter_.get();
    // This callback will be executed in the destructor at the latest so
    // |out_of_process_mounter_| will always be valid. Error reporting is done
    // in the helper process in cryptohome_namespace_mounter.cc.
    cleanup = base::Bind(
        base::IgnoreResult(&OutOfProcessMountHelper::TearDownEphemeralMount),
        base::Unretained(out_of_process_mounter_.get()));
  } else {
    ephemeral_mounter = mounter_.get();
    // This callback will be executed in the destructor at the latest so
    // |this| will always be valid.
    cleanup =
        base::Bind(&Mount::TearDownEphemeralMount, base::Unretained(this));
  }

  return MountEphemeralCryptohomeInternal(kGuestUserName, ephemeral_mounter,
                                          std::move(cleanup));
}

FilePath Mount::GetUserDirectoryForUser(
    const std::string& obfuscated_username) const {
  return ShadowRoot().Append(obfuscated_username);
}

FilePath Mount::GetUserTemporaryMountDirectory(
    const std::string& obfuscated_username) const {
  return mounter_->GetUserTemporaryMountDirectory(obfuscated_username);
}

bool Mount::CheckChapsDirectory(const FilePath& dir) {
  const Platform::Permissions kChapsDirPermissions = {
      chaps_user_,                 // chaps
      default_access_group_,       // chronos-access
      S_IRWXU | S_IRGRP | S_IXGRP  // 0750
  };
  const Platform::Permissions kChapsFilePermissions = {
      chaps_user_,                 // chaps
      default_access_group_,       // chronos-access
      S_IRUSR | S_IWUSR | S_IRGRP  // 0640
  };
  const Platform::Permissions kChapsSaltPermissions = {
      0,                 // root
      0,                 // root
      S_IRUSR | S_IWUSR  // 0600
  };

  // If the Chaps database directory does not exist, create it.
  if (!platform_->DirectoryExists(dir)) {
    if (!platform_->CreateDirectory(dir)) {
      LOG(ERROR) << "Failed to create " << dir.value();
      return false;
    }
    if (!platform_->SetOwnership(dir, kChapsDirPermissions.user,
                                 kChapsDirPermissions.group, true)) {
      LOG(ERROR) << "Couldn't set file ownership for " << dir.value();
      return false;
    }
    if (!platform_->SetPermissions(dir, kChapsDirPermissions.mode)) {
      LOG(ERROR) << "Couldn't set permissions for " << dir.value();
      return false;
    }
    return true;
  }
  // Directory already exists so check permissions and log a warning
  // if not as expected then attempt to apply correct permissions.
  std::map<FilePath, Platform::Permissions> special_cases;
  special_cases[dir.Append("auth_data_salt")] = kChapsSaltPermissions;
  if (!platform_->ApplyPermissionsRecursive(
          dir, kChapsFilePermissions, kChapsDirPermissions, special_cases)) {
    LOG(ERROR) << "Chaps permissions failure.";
    return false;
  }
  return true;
}

bool Mount::InsertPkcs11Token() {
  FilePath token_dir = homedirs_->GetChapsTokenDir(username_);
  if (!CheckChapsDirectory(token_dir))
    return false;
  // We may create a salt file and, if so, we want to restrict access to it.
  brillo::ScopedUmask scoped_umask(kDefaultUmask);

  // Derive authorization data for the token from the passkey.
  FilePath salt_file = homedirs_->GetChapsTokenSaltPath(username_);

  std::unique_ptr<chaps::TokenManagerClient> chaps_client(
      chaps_client_factory_->New());

  Pkcs11Init pkcs11init;
  int slot_id = 0;
  if (!chaps_client->LoadToken(
          IsolateCredentialManager::GetDefaultIsolateCredential(), token_dir,
          pkcs11_token_auth_data_,
          pkcs11init.GetTpmTokenLabelForUser(username_), &slot_id)) {
    LOG(ERROR) << "Failed to load PKCS #11 token.";
    ReportCryptohomeError(kLoadPkcs11TokenFailed);
  }
  pkcs11_token_auth_data_.clear();
  ReportTimerStop(kPkcs11InitTimer);
  return true;
}

void Mount::RemovePkcs11Token() {
  FilePath token_dir = homedirs_->GetChapsTokenDir(username_);
  std::unique_ptr<chaps::TokenManagerClient> chaps_client(
      chaps_client_factory_->New());
  chaps_client->UnloadToken(
      IsolateCredentialManager::GetDefaultIsolateCredential(), token_dir);
}

base::Value Mount::GetStatus(int active_key_index) {
  base::Value dv(base::Value::Type::DICTIONARY);
  std::string user = SanitizeUserNameWithSalt(username_, system_salt_);
  base::Value keysets(base::Value::Type::LIST);
  std::vector<int> key_indices;
  if (user.length() &&
      homedirs_->keyset_management()->GetVaultKeysets(user, &key_indices)) {
    for (auto key_index : key_indices) {
      base::Value keyset_dict(base::Value::Type::DICTIONARY);
      std::unique_ptr<VaultKeyset> keyset(
          homedirs_->keyset_management()->LoadVaultKeysetForUser(user,
                                                                 key_index));
      if (keyset.get()) {
        bool tpm =
            keyset->serialized().flags() & SerializedVaultKeyset::TPM_WRAPPED;
        bool scrypt = keyset->serialized().flags() &
                      SerializedVaultKeyset::SCRYPT_WRAPPED;
        keyset_dict.SetBoolKey("tpm", tpm);
        keyset_dict.SetBoolKey("scrypt", scrypt);
        keyset_dict.SetBoolKey("ok", true);
        keyset_dict.SetIntKey("last_activity",
                              keyset->serialized().last_activity_timestamp());
        if (keyset->serialized().has_key_data()) {
          // TODO(wad) Add additional KeyData
          keyset_dict.SetStringKey("label",
                                   keyset->serialized().key_data().label());
        }
      } else {
        keyset_dict.SetBoolKey("ok", false);
      }
      // TODO(wad) Replace key_index use with key_label() use once
      //           legacy keydata is populated.
      if (mount_type_ != MountType::EPHEMERAL && key_index == active_key_index)
        keyset_dict.SetBoolKey("current", true);
      keyset_dict.SetIntKey("index", key_index);
      keysets.Append(std::move(keyset_dict));
    }
  }
  dv.SetKey("keysets", std::move(keysets));
  dv.SetBoolKey("mounted", IsMounted());
  std::string obfuscated_owner;
  homedirs_->GetOwner(&obfuscated_owner);
  dv.SetStringKey("owner", obfuscated_owner);
  dv.SetBoolKey("enterprise", homedirs_->enterprise_owned());

  std::string mount_type_string;
  switch (mount_type_) {
    case MountType::NONE:
      mount_type_string = "none";
      break;
    case MountType::ECRYPTFS:
      mount_type_string = "ecryptfs";
      break;
    case MountType::DIR_CRYPTO:
      mount_type_string = "dircrypto";
      break;
    case MountType::EPHEMERAL:
      mount_type_string = "ephemeral";
      break;
  }
  dv.SetStringKey("type", mount_type_string);

  return dv;
}

bool Mount::MigrateToDircrypto(
    const dircrypto_data_migrator::MigrationHelper::ProgressCallback& callback,
    MigrationType migration_type) {
  std::string obfuscated_username =
      SanitizeUserNameWithSalt(username_, system_salt_);
  FilePath temporary_mount =
      GetUserTemporaryMountDirectory(obfuscated_username);
  if (!IsMounted() || mount_type_ != MountType::DIR_CRYPTO ||
      !platform_->DirectoryExists(temporary_mount) ||
      !mounter_->IsPathMounted(temporary_mount)) {
    LOG(ERROR) << "Not mounted for eCryptfs->dircrypto migration.";
    return false;
  }
  // Do migration.
  constexpr uint64_t kMaxChunkSize = 128 * 1024 * 1024;
  dircrypto_data_migrator::MigrationHelper migrator(
      platform_, temporary_mount, mount_point_,
      GetUserDirectoryForUser(obfuscated_username), kMaxChunkSize,
      migration_type);
  {  // Abort if already cancelled.
    base::AutoLock lock(active_dircrypto_migrator_lock_);
    if (is_dircrypto_migration_cancelled_)
      return false;
    CHECK(!active_dircrypto_migrator_);
    active_dircrypto_migrator_ = &migrator;
  }
  bool success = migrator.Migrate(callback);
  // This closure will be run immediately so |mounter_| will be valid.
  UnmountAndDropKeys(base::BindOnce(&MountHelper::TearDownNonEphemeralMount,
                                    base::Unretained(mounter_.get())));
  {  // Signal the waiting thread.
    base::AutoLock lock(active_dircrypto_migrator_lock_);
    active_dircrypto_migrator_ = nullptr;
    dircrypto_migration_stopped_condition_.Signal();
  }
  if (!success) {
    LOG(ERROR) << "Failed to migrate.";
    return false;
  }
  // Clean up.
  FilePath vault_path = GetEcryptfsUserVaultPath(obfuscated_username);
  if (!platform_->DeletePathRecursively(temporary_mount) ||
      !platform_->DeletePathRecursively(vault_path)) {
    LOG(ERROR) << "Failed to delete the old vault.";
    return false;
  }
  return true;
}

void Mount::MaybeCancelActiveDircryptoMigrationAndWait() {
  base::AutoLock lock(active_dircrypto_migrator_lock_);
  is_dircrypto_migration_cancelled_ = true;
  while (active_dircrypto_migrator_) {
    active_dircrypto_migrator_->Cancel();
    LOG(INFO) << "Waiting for dircrypto migration to stop.";
    dircrypto_migration_stopped_condition_.Wait();
    LOG(INFO) << "Dircrypto migration stopped.";
  }
}

bool Mount::IsShadowOnly() const {
  return shadow_only_;
}
}  // namespace cryptohome
