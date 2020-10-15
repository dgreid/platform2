// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Mount.

#include "cryptohome/mount.h"

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
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/dircrypto_data_migrator/migration_helper.h"
#include "cryptohome/dircrypto_util.h"
#include "cryptohome/homedirs.h"
#include "cryptohome/mount_utils.h"
#include "cryptohome/pkcs11_init.h"
#include "cryptohome/platform.h"
#include "cryptohome/timestamp.pb.h"
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

const char kKeyFile[] = "master";
const int kKeyFileMax = 100;  // master.0 ... master.99
const mode_t kKeyFilePermissions = 0600;
const char kKeyLegacyPrefix[] = "legacy-";

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

Mount::Mount()
    : default_user_(-1),
      chaps_user_(-1),
      default_group_(-1),
      default_access_group_(-1),
      shadow_root_(kDefaultShadowRoot),
      skel_source_(kDefaultSkeletonSource),
      system_salt_(),
      default_platform_(new Platform()),
      platform_(default_platform_.get()),
      crypto_(NULL),
      default_homedirs_(new HomeDirs()),
      homedirs_(default_homedirs_.get()),
      use_tpm_(true),
      user_timestamp_cache_(NULL),
      enterprise_owned_(false),
      mount_key_index_(-1),
      pkcs11_state_(kUninitialized),
      dircrypto_key_reference_(),
      legacy_mount_(true),
      mount_type_(MountType::NONE),
      shadow_only_(false),
      default_chaps_client_factory_(new ChapsClientFactory()),
      chaps_client_factory_(default_chaps_client_factory_.get()),
      dircrypto_migration_stopped_condition_(&active_dircrypto_migrator_lock_),
      mount_guest_session_out_of_process_(true),
      mount_non_ephemeral_session_out_of_process_(MountUserSessionOOP()),
      mount_guest_session_non_root_namespace_(true) {}

Mount::~Mount() {
  if (IsMounted())
    UnmountCryptohome();
}

bool Mount::Init(Platform* platform,
                 Crypto* crypto,
                 UserOldestActivityTimestampCache* cache) {
  platform_ = platform;
  crypto_ = crypto;
  user_timestamp_cache_ = cache;

  bool result = true;

  homedirs_->set_platform(platform_);
  homedirs_->set_shadow_root(FilePath(shadow_root_));
  homedirs_->set_enterprise_owned(enterprise_owned_);
  homedirs_->set_use_tpm(use_tpm_);

  // Make sure |homedirs_| uses the same PolicyProvider instance as we in case
  // it was set by a test.
  if (policy_provider_)
    homedirs_->set_policy_provider(policy_provider_.get());

  if (!homedirs_->Init(platform, crypto, user_timestamp_cache_))
    result = false;

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

  {
    brillo::ScopedUmask scoped_umask(kDefaultUmask);
    // Create the shadow root if it doesn't exist
    if (!platform_->DirectoryExists(shadow_root_)) {
      platform_->CreateDirectory(shadow_root_);
    }

    // One-time load of the global system salt (used in generating username
    // hashes)
    FilePath system_salt_file = shadow_root_.Append(kSystemSaltFile);
    if (!crypto_->GetOrCreateSalt(system_salt_file,
                                  CRYPTOHOME_DEFAULT_SALT_LENGTH, false,
                                  &system_salt_)) {
      LOG(ERROR) << "Failed to load or create the system salt";
      result = false;
    }
  }

  mounter_.reset(new MountHelper(
      default_user_, default_group_, default_access_group_, shadow_root_,
      skel_source_, system_salt_, legacy_mount_, platform_));

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
        system_salt_, std::move(chrome_mnt_ns), legacy_mount_, platform_));
  }

  return result;
}

MountType Mount::DeriveVaultMountType(const std::string& obfuscated_username,
                                      bool shall_migrate) const {
  FilePath ecryptfs_vault_path =
      homedirs_->GetEcryptfsUserVaultPath(obfuscated_username);
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

  dircrypto::KeyState state = platform_->GetDirCryptoKeyState(shadow_root_);
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

bool Mount::AddEcryptfsAuthToken(const VaultKeyset& vault_keyset,
                                 std::string* key_signature,
                                 std::string* filename_key_signature) const {
  // Add the File Encryption key (FEK) from the vault keyset.  This is the key
  // that is used to encrypt the file contents when the file is persisted to the
  // lower filesystem by eCryptfs.
  *key_signature = CryptoLib::SecureBlobToHex(vault_keyset.fek_sig());
  if (!platform_->AddEcryptfsAuthToken(vault_keyset.fek(), *key_signature,
                                       vault_keyset.fek_salt())) {
    LOG(ERROR) << "Couldn't add eCryptfs file encryption key to keyring.";
    return false;
  }

  // Add the File Name Encryption Key (FNEK) from the vault keyset.  This is the
  // key that is used to encrypt the file name when the file is persisted to the
  // lower filesystem by eCryptfs.
  *filename_key_signature = CryptoLib::SecureBlobToHex(vault_keyset.fnek_sig());
  if (!platform_->AddEcryptfsAuthToken(vault_keyset.fnek(),
                                       *filename_key_signature,
                                       vault_keyset.fnek_salt())) {
    LOG(ERROR) << "Couldn't add eCryptfs filename encryption key to keyring.";
    return false;
  }

  return true;
}

MountError Mount::MountEphemeralCryptohome(const Credentials& credentials) {
  username_ = credentials.username();

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

bool Mount::MountCryptohome(const Credentials& credentials,
                            const Mount::MountArgs& mount_args,
                            bool recreate_on_decrypt_fatal,
                            MountError* mount_error) {
  username_ = credentials.username();
  const std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);
  const bool is_owner = homedirs_->IsOrWillBeOwner(username_);

  if (!mount_args.create_if_missing &&
      !homedirs_->CryptohomeExists(obfuscated_username)) {
    LOG(ERROR) << "Asked to mount nonexistent user";
    *mount_error = MOUNT_ERROR_USER_DOES_NOT_EXIST;
    return false;
  }

  if (!mount_args.shadow_only) {
    if (!mounter_->EnsureUserMountPoints(credentials.username())) {
      LOG(ERROR) << "Error creating mountpoint.";
      *mount_error = MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED;
      return false;
    }
  }

  bool created = false;
  if (!homedirs_->CryptohomeExists(obfuscated_username)) {
    if (!CreateCryptohome(credentials, mount_args.create_as_ecryptfs)) {
      LOG(ERROR) << "Error creating cryptohome.";
      *mount_error = MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED;
      return false;
    }
    created = true;
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

  // Attempt to decrypt the vault keyset with the specified credentials.
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(platform_, crypto_);
  MountError local_mount_error = MOUNT_ERROR_NONE;
  if (!DecryptVaultKeyset(credentials, &vault_keyset, &local_mount_error)) {
    *mount_error = local_mount_error;
    if (recreate_on_decrypt_fatal &&
        local_mount_error == MOUNT_ERROR_VAULT_UNRECOVERABLE) {
      LOG(ERROR) << "cryptohome must be re-created because of fatal error.";
      if (!homedirs_->Remove(credentials.username())) {
        LOG(ERROR) << "Fatal decryption error, but unable to remove "
                   << "cryptohome.";
        *mount_error = MOUNT_ERROR_REMOVE_INVALID_USER_FAILED;
        return false;
      }
      // Allow one recursion into MountCryptohome by blocking re-create on
      // fatal.
      bool local_result =
          MountCryptohome(credentials, mount_args,
                          /*recreate_on_decrypt_fatal=*/false, mount_error);
      // If the mount was successful, set the status to indicate that the
      // cryptohome was recreated.
      if (local_result) {
        *mount_error = MOUNT_ERROR_RECREATED;
      }
      return local_result;
    }

    // Return VAULT_UNRECOVERABLE as FATAL for the higher level code doesn't
    // know such an error.
    // TODO(chromium:1140868, dlunev): extract the recreation behaviour to the
    // higher layer and then return VAULT_UNRECOVERABLE directly.
    if (*mount_error == MOUNT_ERROR_VAULT_UNRECOVERABLE) {
      *mount_error = MOUNT_ERROR_FATAL;
    }

    LOG(ERROR) << "Failed to decrypt VK, error = " << local_mount_error;
    return false;
  }

  if (!vault_keyset.serialized().has_wrapped_chaps_key()) {
    vault_keyset.CreateRandomChapsKey();
    ReEncryptVaultKeyset(credentials, &vault_keyset);
  }

  SecureBlob local_chaps_key(vault_keyset.chaps_key().begin(),
                             vault_keyset.chaps_key().end());
  pkcs11_token_auth_data_.swap(local_chaps_key);
  if (!platform_->ClearUserKeyring()) {
    LOG(ERROR) << "Failed to clear user keyring";
  }

  // Checks whether migration from ecryptfs to dircrypto is needed, and returns
  // an error when necessary. Do this after the check by DecryptVaultKeyset,
  // because a correct credential is required before switching to migration UI.
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
    if (!AddEcryptfsAuthToken(vault_keyset, &key_signature, &fnek_signature)) {
      LOG(ERROR) << "Error adding eCryptfs keys.";
      *mount_error = MOUNT_ERROR_KEYRING_FAILED;
      return false;
    }
  }
  if (should_mount_dircrypto) {
    dircrypto_key_reference_.policy_version =
        dircrypto::GetDirectoryPolicyVersion(
            homedirs_->GetUserMountDirectory(obfuscated_username));
    if (dircrypto_key_reference_.policy_version < 0) {
      dircrypto_key_reference_.policy_version =
          dircrypto::CheckFscryptKeyIoctlSupport() ? FSCRYPT_POLICY_V2
                                                   : FSCRYPT_POLICY_V1;
    }
    dircrypto_key_reference_.reference = vault_keyset.fek_sig();
    if (!platform_->AddDirCryptoKeyToKeyring(vault_keyset.fek(),
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

  mount_point_ = homedirs_->GetUserMountDirectory(obfuscated_username);
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

  mount_key_index_ = vault_keyset.legacy_index();

  MountHelper::Options mount_opts = {
      mount_type_, mount_args.to_migrate_from_ecryptfs, mount_args.shadow_only};

  cryptohome::ReportTimerStart(cryptohome::kPerformMountTimer);
  if (!helper->PerformMount(mount_opts, credentials.username(), key_signature,
                            fnek_signature, created, mount_error)) {
    LOG(ERROR) << "MountHelper::PerformMount failed, error = " << *mount_error;
    return false;
  }

  cryptohome::ReportTimerStop(cryptohome::kPerformMountTimer);

  if (!UserSignInEffects(true /* is_mount */, is_owner)) {
    LOG(ERROR) << "Failed to set user type, aborting mount";
    *mount_error = MOUNT_ERROR_TPM_COMM_ERROR;
    return false;
  }

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
  platform_->RestoreSELinuxContexts(
      homedirs_->GetUserMountDirectory(obfuscated_username), true);

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

  if (!UserSignInEffects(true /* is_mount */, false /* is_owner */)) {
    LOG(ERROR) << "Failed to set user type, aborting ephemeral mount";
    return false;
  }

  // Mount succeeded, move the clean-up closure to the instance variable.
  mount_cleanup_ = cleanup_runner.Release();

  mount_type_ = MountType::EPHEMERAL;
  return true;
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
                                                    shadow_root_);
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
  if (!UserSignInEffects(false /* is_mount */, false /* is_owner */)) {
    LOG(WARNING) << "Failed to set user type, but continuing with unmount";
  }

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

bool Mount::CreateCryptohome(const Credentials& credentials,
                             bool force_ecryptfs) const {
  brillo::ScopedUmask scoped_umask(kDefaultUmask);
  std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);

  MountType mount_type = ChooseVaultMountType(force_ecryptfs);
  if (mount_type == MountType::NONE) {
    return false;
  }

  // Create the user's entry in the shadow root
  FilePath user_dir(GetUserDirectoryForUser(obfuscated_username));
  platform_->CreateDirectory(user_dir);

  // Generate a new keyset
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(platform_, crypto_);
  vault_keyset.CreateRandom();
  if (!AddVaultKeyset(credentials, &vault_keyset)) {
    LOG(ERROR) << "Failed to add vault keyset to new user";
    return false;
  }
  // Merge in the key data from credentials using the label() as
  // the existence test. (All new-format calls must populate the
  // label on creation.)
  if (!credentials.key_data().label().empty()) {
    *vault_keyset.mutable_serialized()->mutable_key_data() =
        credentials.key_data();
  }
  if (credentials.key_data().type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    *vault_keyset.mutable_serialized()->mutable_signature_challenge_info() =
        credentials.challenge_credentials_keyset_info();
  }

  vault_keyset.set_legacy_index(0);  // first key
  if (!StoreVaultKeysetForUser(obfuscated_username, &vault_keyset)) {
    LOG(ERROR) << "Failed to store vault keyset for new user";
    return false;
  }

  if (mount_type == MountType::ECRYPTFS) {
    // Create the user's vault.
    FilePath vault_path =
        homedirs_->GetEcryptfsUserVaultPath(obfuscated_username);
    if (!platform_->CreateDirectory(vault_path)) {
      LOG(ERROR) << "Couldn't create vault path: " << vault_path.value();
      return false;
    }
  }

  return true;
}

bool Mount::CreateTrackedSubdirectories(const Credentials& credentials) const {
  return mounter_->CreateTrackedSubdirectories(
      credentials.GetObfuscatedUsername(system_salt_), mount_type_);
}

bool Mount::UpdateCurrentUserActivityTimestamp(int time_shift_sec,
                                               int active_key_index) {
  std::string obfuscated_username =
      SanitizeUserNameWithSalt(username_, system_salt_);
  if (!obfuscated_username.empty() && mount_type_ != MountType::EPHEMERAL) {
    VaultKeyset keyset;
    keyset.Initialize(platform_, crypto_);
    if (!homedirs_->LoadVaultKeysetForUser(obfuscated_username,
                                           active_key_index, &keyset)) {
      return false;
    }
    base::Time timestamp = platform_->GetCurrentTime();
    if (time_shift_sec > 0)
      timestamp -= base::TimeDelta::FromSeconds(time_shift_sec);
    keyset.mutable_serialized()->set_last_activity_timestamp(
        timestamp.ToInternalValue());
    if (!StoreTimestampForUser(obfuscated_username, &keyset)) {
      return false;
    }
    if (user_timestamp_cache_->initialized()) {
      user_timestamp_cache_->UpdateExistingUser(obfuscated_username, timestamp);
    }
    return true;
  }
  return false;
}

bool Mount::StoreVaultKeysetForUser(const std::string& obfuscated_username,
                                    VaultKeyset* vault_keyset) const {
  int index = vault_keyset->legacy_index();
  if (index < 0 || index > kKeyFileMax) {
    LOG(ERROR) << "Attempted to store an invalid key index: " << index;
    return false;
  }
  if (platform_->FileExists(
          GetUserLegacyKeyFileForUser(obfuscated_username, index))) {
    VaultKeyset keyset;
    keyset.Initialize(platform_, crypto_);
    homedirs_->LoadVaultKeysetForUser(obfuscated_username, index, &keyset);
    if (vault_keyset->serialized().has_last_activity_timestamp()) {
      keyset.mutable_serialized()->set_last_activity_timestamp(
          vault_keyset->serialized().last_activity_timestamp());
      if (MessageDifferencer::Equals(vault_keyset->serialized(),
                                     keyset.serialized())) {
        LOG(INFO) << "Only the timestamp has changed, should not store keyset.";
        return StoreTimestampForUser(obfuscated_username, vault_keyset);
      }
    }
  }
  if (vault_keyset->serialized().has_last_activity_timestamp()) {
    if (!StoreTimestampForUser(obfuscated_username, vault_keyset)) {
      return false;
    }
  }
  brillo::Blob final_blob(vault_keyset->serialized().ByteSizeLong());
  vault_keyset->serialized().SerializeWithCachedSizesToArray(
      static_cast<google::protobuf::uint8*>(final_blob.data()));
  return platform_->WriteFileAtomicDurable(
      GetUserLegacyKeyFileForUser(obfuscated_username, index), final_blob,
      kKeyFilePermissions);
}

bool Mount::StoreTimestampForUser(const std::string& obfuscated_username,
                                  VaultKeyset* vault_keyset) const {
  int index = vault_keyset->legacy_index();
  Timestamp timestamp;
  timestamp.set_timestamp(vault_keyset->serialized().last_activity_timestamp());
  std::string timestamp_str;
  if (!timestamp.SerializeToString(&timestamp_str)) {
    return false;
  }
  if (!platform_->WriteStringToFileAtomicDurable(
          GetUserTimestampFileForUser(obfuscated_username, index),
          timestamp_str, kKeyFilePermissions)) {
    LOG(ERROR) << "Failed writing to timestamp file";
    return false;
  }
  if (!vault_keyset->serialized().timestamp_file_exists()) {
    // The first time we write to a timestamp file we need to update the
    // vault_keyset to indicate that the timestamp is stored separately.
    // The initial 0 timestamp is also written to the vault_keyset which
    // means a timestamp will exist and can be read in case of a rollback.
    vault_keyset->mutable_serialized()->set_timestamp_file_exists(true);
    brillo::Blob blob(vault_keyset->serialized().ByteSizeLong());
    vault_keyset->serialized().SerializeWithCachedSizesToArray(
        static_cast<google::protobuf::uint8*>(blob.data()));
    return platform_->WriteFileAtomicDurable(
        GetUserLegacyKeyFileForUser(obfuscated_username, index), blob,
        kKeyFilePermissions);
  }
  return true;
}

bool Mount::ShouldReSaveKeyset(VaultKeyset* vault_keyset) const {
  // If the vault keyset's TPM state is not the same as that configured for
  // the device, re-save the keyset (this will save in the device's default
  // method).
  // In the table below: X = true, - = false, * = any value
  //
  //                 1   2   3   4   5   6   7   8   9
  // should_tpm      X   X   X   X   -   -   -   *   X
  //
  // pcr_bound       -   X   *   -   -   *   -   *   -
  //
  // tpm_wrapped     -   X   X   -   -   X   -   X   *
  //
  // scrypt_wrapped  -   -   -   X   -   -   X   X   *
  //
  // scrypt_derived  *   X   -   *   *   *   *   *   *
  //
  // migrate         Y   N   Y   Y   Y   Y   N   Y   Y
  //
  // If the vault keyset is signature-challenge protected, we should not
  // re-encrypt it at all (that is unnecessary).
  const unsigned crypt_flags = vault_keyset->serialized().flags();
  bool pcr_bound = (crypt_flags & SerializedVaultKeyset::PCR_BOUND) != 0;
  bool tpm_wrapped = (crypt_flags & SerializedVaultKeyset::TPM_WRAPPED) != 0;
  bool scrypt_wrapped =
      (crypt_flags & SerializedVaultKeyset::SCRYPT_WRAPPED) != 0;
  bool scrypt_derived =
      (crypt_flags & SerializedVaultKeyset::SCRYPT_DERIVED) != 0;
  bool is_signature_challenge_protected =
      (crypt_flags & SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED) != 0;
  bool should_tpm =
      (crypto_->has_tpm() && use_tpm_ && crypto_->is_cryptohome_key_loaded() &&
       !is_signature_challenge_protected);
  bool can_unseal_with_user_auth = crypto_->CanUnsealWithUserAuth();
  bool has_tpm_public_key_hash =
      vault_keyset->serialized().has_tpm_public_key_hash();

  if (is_signature_challenge_protected)
    return false;

  bool is_le_credential =
      (crypt_flags & SerializedVaultKeyset::LE_CREDENTIAL) != 0;
  uint64_t le_label = vault_keyset->serialized().le_label();
  if (is_le_credential && !crypto_->NeedsPcrBinding(le_label))
    return false;

  // If the keyset was TPM-wrapped, but there was no public key hash,
  // always re-save.
  if (tpm_wrapped && !has_tpm_public_key_hash) {
    LOG(INFO) << "Migrating keyset " << vault_keyset->legacy_index()
              << " as there is no public hash";
    return true;
  }

  // Check the table.
  if (tpm_wrapped && should_tpm && scrypt_derived && !scrypt_wrapped) {
    if ((pcr_bound && can_unseal_with_user_auth) ||
        (!pcr_bound && !can_unseal_with_user_auth)) {
      return false;  // 2
    }
  }
  if (scrypt_wrapped && !should_tpm && !tpm_wrapped)
    return false;  // 7


  LOG(INFO) << "Migrating keyset " << vault_keyset->legacy_index()
            << ": should_tpm=" << should_tpm
            << ", has_hash=" << has_tpm_public_key_hash
            << ", flags=" << crypt_flags << ", pcr_bound=" << pcr_bound
            << ", can_unseal_with_user_auth=" << can_unseal_with_user_auth;

  return true;
}

bool Mount::DecryptVaultKeyset(const Credentials& credentials,
                               VaultKeyset* vault_keyset,
                               MountError* error) const {
  *error = MOUNT_ERROR_NONE;

  if (!homedirs_->GetValidKeyset(credentials, vault_keyset, error))
    return false;

  // Calling EnsureTpm here handles the case where a user logged in while
  // cryptohome was taking TPM ownership.  In that case, their vault keyset
  // would be scrypt-wrapped and the TPM would not be connected.  If we're
  // configured to use the TPM, calling EnsureTpm will try to connect, and
  // if successful, the call to has_tpm() below will succeed, allowing
  // re-wrapping (migration) using the TPM.
  if (use_tpm_) {
    crypto_->EnsureTpm(false);
  }

  if (!ShouldReSaveKeyset(vault_keyset)) {
    return true;
  }

  // This is not considered a fatal error.  Re-saving with the desired
  // protection is ideal, but not required.
  ReEncryptVaultKeyset(credentials, vault_keyset);

  return true;
}

bool Mount::AddVaultKeyset(const Credentials& credentials,
                           VaultKeyset* vault_keyset) const {
  // We don't do passkey to wrapper conversion because it is salted during save
  const SecureBlob passkey = credentials.passkey();

  std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);

  if (credentials.key_data().type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    vault_keyset->mutable_serialized()->set_flags(
        vault_keyset->serialized().flags() |
        SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);
  }

  // Encrypt the vault keyset
  const auto salt =
      CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);
  if (!crypto_->EncryptVaultKeyset(*vault_keyset, passkey, salt,
                                   obfuscated_username,
                                   vault_keyset->mutable_serialized())) {
    LOG(ERROR) << "Encrypting vault keyset failed";
    return false;
  }

  return true;
}

bool Mount::ReEncryptVaultKeyset(const Credentials& credentials,
                                 VaultKeyset* vault_keyset) const {
  // Save the initial serialized proto so we can roll-back any changes if we
  // failed to re-save.
  SerializedVaultKeyset old_serialized;
  old_serialized.CopyFrom(vault_keyset->serialized());

  std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);
  uint64_t label = vault_keyset->serialized().le_label();
  if (!AddVaultKeyset(credentials, vault_keyset)) {
    LOG(ERROR) << "Couldn't add keyset.";
    vault_keyset->mutable_serialized()->CopyFrom(old_serialized);
    return false;
  }

  if ((vault_keyset->serialized().flags() &
       SerializedVaultKeyset::LE_CREDENTIAL) != 0) {
    if (!crypto_->RemoveLECredential(label)) {
      // This is non-fatal error.
      LOG(ERROR) << "Failed to remove label = " << label;
    }
  }

  // Note that existing legacy keysets are not automatically annotated.
  // All _new_ interfaces that support KeyData will implicitly translate
  // master.<index> to label=<kKeyLegacyFormat,index> for checking on
  // label uniqueness.  This means that we will still be able to use the
  // lack of KeyData in the future as input to migration.
  if (!StoreVaultKeysetForUser(credentials.GetObfuscatedUsername(system_salt_),
                               vault_keyset)) {
    LOG(ERROR) << "Write to keyset failed";
    vault_keyset->mutable_serialized()->CopyFrom(old_serialized);
    return false;
  }

  return true;
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
  return shadow_root_.Append(obfuscated_username);
}

FilePath Mount::GetUserTimestampFileForUser(
    const std::string& obfuscated_username, int index) const {
  return GetUserLegacyKeyFileForUser(obfuscated_username, index)
      .AddExtension("timestamp");
}

FilePath Mount::GetUserLegacyKeyFileForUser(
    const std::string& obfuscated_username, int index) const {
  DCHECK(index < kKeyFileMax && index >= 0);
  return shadow_root_.Append(obfuscated_username)
      .Append(kKeyFile)
      .AddExtension(base::NumberToString(index));
}

FilePath Mount::GetUserTemporaryMountDirectory(
    const std::string& obfuscated_username) const {
  return mounter_->GetUserTemporaryMountDirectory(obfuscated_username);
}

bool Mount::CheckChapsDirectory(const FilePath& dir,
                                const FilePath& legacy_dir) {
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
    if (platform_->DirectoryExists(legacy_dir)) {
      LOG(INFO) << "Moving chaps directory from " << legacy_dir.value()
                << " to " << dir.value();
      if (!platform_->CopyWithPermissions(legacy_dir, dir)) {
        return false;
      }
      if (!platform_->DeleteFile(legacy_dir, true)) {
        PLOG(WARNING) << "Failed to clean up " << legacy_dir.value();
        return false;
      }
    } else {
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
  FilePath legacy_token_dir = homedirs_->GetLegacyChapsTokenDir(username_);
  if (!CheckChapsDirectory(token_dir, legacy_token_dir))
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

std::unique_ptr<base::Value> Mount::GetStatus(int active_key_index) {
  VaultKeyset keyset;
  keyset.Initialize(platform_, crypto_);
  auto dv = std::make_unique<base::DictionaryValue>();
  std::string user = SanitizeUserNameWithSalt(username_, system_salt_);
  auto keysets = std::make_unique<base::ListValue>();
  std::vector<int> key_indices;
  if (user.length() && homedirs_->GetVaultKeysets(user, &key_indices)) {
    for (auto key_index : key_indices) {
      auto keyset_dict = std::make_unique<base::DictionaryValue>();
      if (homedirs_->LoadVaultKeysetForUser(user, key_index, &keyset)) {
        bool tpm =
            keyset.serialized().flags() & SerializedVaultKeyset::TPM_WRAPPED;
        bool scrypt =
            keyset.serialized().flags() & SerializedVaultKeyset::SCRYPT_WRAPPED;
        keyset_dict->SetBoolean("tpm", tpm);
        keyset_dict->SetBoolean("scrypt", scrypt);
        keyset_dict->SetBoolean("ok", true);
        keyset_dict->SetInteger("last_activity",
                                keyset.serialized().last_activity_timestamp());
        if (keyset.serialized().has_key_data()) {
          // TODO(wad) Add additional KeyData
          keyset_dict->SetString("label",
                                 keyset.serialized().key_data().label());
        }
      } else {
        keyset_dict->SetBoolean("ok", false);
      }
      // TODO(wad) Replace key_index use with key_label() use once
      //           legacy keydata is populated.
      if (mount_type_ != MountType::EPHEMERAL && key_index == active_key_index)
        keyset_dict->SetBoolean("current", true);
      keyset_dict->SetInteger("index", key_index);
      keysets->Append(std::move(keyset_dict));
    }
  }
  dv->Set("keysets", std::move(keysets));
  dv->SetBoolean("mounted", IsMounted());
  std::string obfuscated_owner;
  homedirs_->GetOwner(&obfuscated_owner);
  dv->SetString("owner", obfuscated_owner);
  dv->SetBoolean("enterprise", enterprise_owned_);

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
  dv->SetString("type", mount_type_string);

  return std::move(dv);
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
  FilePath vault_path =
      homedirs_->GetEcryptfsUserVaultPath(obfuscated_username);
  if (!platform_->DeleteFile(temporary_mount, true /* recursive */) ||
      !platform_->DeleteFile(vault_path, true /* recursive */)) {
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

// TODO(chromium:795310): include all side-effects and move out of mount.cc.
// Sign-in/sign-out effects hook.
// Performs actions that need to follow a mount/unmount operation as a part of
// user sign-in/sign-out.
// Parameters:
//   |mount| - the mount instance that was just mounted/unmounted.
//   |tpm| - the TPM instance.
//   |is_mount| - true for mount operation, false for unmount.
//   |is_owner| - true if mounted for an owner user, false otherwise.
// Returns true if successful, false otherwise.
bool Mount::UserSignInEffects(bool is_mount, bool is_owner) {
  Tpm* tpm = crypto_->get_tpm();
  if (!tpm) {
    return true;
  }

  Tpm::UserType user_type =
      (is_mount & is_owner) ? Tpm::UserType::Owner : Tpm::UserType::NonOwner;
  return tpm->SetUserType(user_type);
}

}  // namespace cryptohome
