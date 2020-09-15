// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/service.h"
#if USE_TPM2
#include "cryptohome/bootlockbox/boot_lockbox_client.h"
#endif  // USE_TPM2

#include <functional>

#include <base/bind.h>
#include <base/callback.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/message_loop/message_loop_current.h>
#include <base/message_loop/message_pump_type.h>
#include <base/optional.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/sys_string_conversions.h>
#include <base/system/sys_info.h>
#include <base/time/time.h>
#include <base/values.h>
#include <brillo/cryptohome.h>
#include <brillo/glib/dbus.h>
#include <brillo/secure_blob.h>
#include <chaps/isolate.h>
#include <chaps/token_manager_client.h>
#include <chromeos/constants/cryptohome.h>
#include <dbus/bus.h>
#include <dbus/dbus.h>
#include <glib-unix.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cryptohome/bootlockbox/boot_attributes.h"
#include "cryptohome/bootlockbox/boot_lockbox.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper_impl.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_event_source.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/dbus_transition.h"
#include "cryptohome/disk_cleanup.h"
#include "cryptohome/firmware_management_parameters.h"
#include "cryptohome/glib_transition.h"
#include "cryptohome/install_attributes.h"
#include "cryptohome/interface.h"
#include "cryptohome/key.pb.h"
#include "cryptohome/key_challenge_service.h"
#include "cryptohome/key_challenge_service_factory.h"
#include "cryptohome/key_challenge_service_factory_impl.h"
#include "cryptohome/mount.h"
#include "cryptohome/platform.h"
#include "cryptohome/rpc.pb.h"
#include "cryptohome/service_distributed.h"
#include "cryptohome/stateful_recovery.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.pb.h"

using base::FilePath;
using brillo::Blob;
using brillo::BlobFromString;
using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

// Forcibly namespace the dbus-bindings generated server bindings instead of
// modifying the files afterward.
namespace cryptohome {
namespace gobject {
#include "bindings/cryptohome.dbusserver.h"  // NOLINT(build/include_alpha)
}  // namespace gobject
}  // namespace cryptohome

namespace cryptohome {

namespace {

const std::string& GetAccountId(const AccountIdentifier& id) {
  if (id.has_account_id()) {
    return id.account_id();
  }

  return id.email();
}

bool KeyHasWrappedAuthorizationSecrets(const Key& k) {
  for (const KeyAuthorizationData& auth_data : k.data().authorization_data()) {
    for (const KeyAuthorizationSecret& secret : auth_data.secrets()) {
      // If wrapping becomes richer in the future, this may change.
      if (secret.wrapped())
        return true;
    }
  }
  return false;
}

void AddTaskObserverToThread(base::Thread* thread,
                             base::TaskObserver* task_observer) {
  // Since MessageLoopCurrent::AddTaskObserver need to be executed in the same
  // thread of that message loop. So we need to wrap it and post as a task.

  scoped_refptr<base::SingleThreadTaskRunner> task_runner =
      thread->task_runner();
  if (task_runner == nullptr) {
    LOG(ERROR) << __func__ << ": The thread doesn't have task runner.";
    return;
  }

  task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::TaskObserver* task_observer) {
            base::MessageLoopCurrent::Get().AddTaskObserver(task_observer);
          },
          base::Unretained(task_observer)));
}

// Returns whether the Chrome OS image is a test one.
bool IsOsTestImage() {
  std::string chromeos_release_track;
  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_TRACK",
                                         &chromeos_release_track)) {
    // Fall back to the safer assumption that we're not in a test image.
    return false;
  }
  return base::StartsWith(chromeos_release_track, "test",
                          base::CompareCase::SENSITIVE);
}

// Whether the key can be used for lightweight challenge-response authentication
// check against the given user session.
bool KeyMatchesForLightweightChallengeResponseCheck(
    const KeyData& key_data, const UserSession& session) {
  DCHECK_EQ(key_data.type(), KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  DCHECK_EQ(key_data.challenge_response_key_size(), 1);
  if (session.key_data().type() != KeyData::KEY_TYPE_CHALLENGE_RESPONSE ||
      session.key_data().label().empty() ||
      session.key_data().label() != key_data.label())
    return false;
  if (session.key_data().challenge_response_key_size() != 1) {
    // Using multiple challenge-response keys at once is currently unsupported.
    return false;
  }
  if (session.key_data().challenge_response_key(0).public_key_spki_der() !=
      key_data.challenge_response_key(0).public_key_spki_der()) {
    LOG(WARNING) << "Public key mismatch for lightweight challenge-response "
                    "authentication check";
    return false;
  }
  return true;
}

}  // anonymous namespace

const char kMountThreadName[] = "MountThread";
const char kTpmInitStatusEventType[] = "TpmInitStatus";
const char kDircryptoMigrationProgressEventType[] =
    "DircryptoMigrationProgress";

const char kAutoInitializeTpmSwitch[] = "auto_initialize_tpm";

class TpmInitStatus : public CryptohomeEventBase {
 public:
  TpmInitStatus(bool took_ownership, bool status)
      : took_ownership_(took_ownership), status_(status) {}
  ~TpmInitStatus() override = default;

  const char* GetEventName() const override { return kTpmInitStatusEventType; }

  bool get_took_ownership() { return took_ownership_; }

  bool get_status() { return status_; }

 private:
  bool took_ownership_;
  bool status_;
};

class DircryptoMigrationProgress : public CryptohomeEventBase {
 public:
  DircryptoMigrationProgress(DircryptoMigrationStatus status,
                             uint64_t current_bytes,
                             uint64_t total_bytes)
      : status_(status),
        current_bytes_(current_bytes),
        total_bytes_(total_bytes) {}
  ~DircryptoMigrationProgress() override = default;

  const char* GetEventName() const override {
    return kDircryptoMigrationProgressEventType;
  }

  DircryptoMigrationStatus status() const { return status_; }
  uint64_t current_bytes() const { return current_bytes_; }
  uint64_t total_bytes() const { return total_bytes_; }

 private:
  DircryptoMigrationStatus status_;
  uint64_t current_bytes_;
  uint64_t total_bytes_;

  DISALLOW_COPY_AND_ASSIGN(DircryptoMigrationProgress);
};

void MountThreadObserver::PostTask() {
  parallel_task_count_ += 1;
}

void MountThreadObserver::WillProcessTask(const base::PendingTask& pending_task
#if BASE_VER > 780000
                                          ,
                                          bool was_blocked_or_low_priority
#endif
) {
  // Task name will be equal to the task handler name
  std::string task_name = pending_task.posted_from.function_name();

  ReportAsyncDbusRequestInqueueTime(
      task_name, base::TimeTicks::Now() - pending_task.delayed_run_time);
}

void MountThreadObserver::DidProcessTask(
    const base::PendingTask& pending_task) {
  parallel_task_count_ -= 1;
}

int MountThreadObserver::GetParallelTaskCount() const {
  return parallel_task_count_;
}

Service::Service()
    : use_tpm_(true),
      loop_(NULL),
      cryptohome_(NULL),
      system_salt_(),
      default_platform_(new Platform()),
      platform_(default_platform_.get()),
      default_crypto_(new Crypto(platform_)),
      crypto_(default_crypto_.get()),
      tpm_(nullptr),
      tpm_init_(nullptr),
      fingerprint_manager_(nullptr),
      default_pkcs11_init_(new Pkcs11Init()),
      pkcs11_init_(default_pkcs11_init_.get()),
      initialize_tpm_(true),
      mount_thread_(kMountThreadName),
      async_complete_signal_(-1),
      async_data_complete_signal_(-1),
      tpm_init_signal_(-1),
      low_disk_space_signal_(-1),
      dircrypto_migration_progress_signal_(-1),
      low_disk_space_signal_was_emitted_(false),
      event_source_(),
      event_source_sink_(this),
      default_install_attrs_(new cryptohome::InstallAttributes(NULL)),
      install_attrs_(default_install_attrs_.get()),
      reported_pkcs11_init_fail_(false),
      enterprise_owned_(false),
      user_timestamp_cache_(new UserOldestActivityTimestampCache()),
      default_mount_factory_(new cryptohome::MountFactory()),
      mount_factory_(default_mount_factory_.get()),
      default_homedirs_(new cryptohome::HomeDirs()),
      homedirs_(default_homedirs_.get()),
      default_arc_disk_quota_(new cryptohome::ArcDiskQuota(
          homedirs_, platform_, base::FilePath(kArcDiskHome))),
      arc_disk_quota_(default_arc_disk_quota_.get()),
      guest_user_(brillo::cryptohome::home::kGuestUserName),
      force_ecryptfs_(true),
      legacy_mount_(true),
      public_mount_salt_(),
      default_chaps_client_(new chaps::TokenManagerClient()),
      chaps_client_(default_chaps_client_.get()),
      boot_lockbox_(nullptr),
      boot_attributes_(nullptr),
      firmware_management_parameters_(nullptr),
      low_disk_notification_period_ms_(kLowDiskNotificationPeriodMS),
      upload_alerts_period_ms_(kUploadAlertsPeriodMS),
      ownership_callback_has_run_(false) {}

Service::~Service() {
  mount_thread_.Stop();
  if (loop_) {
    g_main_loop_unref(loop_);
  }
  if (cryptohome_) {
    g_object_unref(cryptohome_);
  }
}

void Service::StopTasks() {
  LOG(INFO) << "Stopping cryptohome task processing.";
  if (loop_) {
    g_main_loop_quit(loop_);
  }
  // It is safe to call Stop() multiple times
  mount_thread_.Stop();
}

Service* Service::CreateDefault() {
  return new ServiceDistributed();
}

static bool PrefixPresent(const std::vector<FilePath>& prefixes,
                          const std::string path) {
  for (const auto& prefix : prefixes)
    if (base::StartsWith(path, prefix.value(),
                         base::CompareCase::INSENSITIVE_ASCII))
      return true;
  return false;
}

bool Service::UnloadPkcs11Tokens(const std::vector<FilePath>& exclude) {
  SecureBlob isolate =
      chaps::IsolateCredentialManager::GetDefaultIsolateCredential();
  std::vector<std::string> tokens;
  if (!chaps_client_->GetTokenList(isolate, &tokens))
    return false;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] != chaps::kSystemTokenPath &&
        !PrefixPresent(exclude, tokens[i])) {
      LOG(INFO) << "Cleaning up PKCS #11 token: " << tokens[i];
      chaps_client_->UnloadToken(isolate, FilePath(tokens[i]));
    }
  }
  return true;
}

CryptohomeErrorCode Service::MountErrorToCryptohomeError(
    const MountError code) const {
  switch (code) {
    case MOUNT_ERROR_NONE:
      return CRYPTOHOME_ERROR_NOT_SET;
    case MOUNT_ERROR_FATAL:
      return CRYPTOHOME_ERROR_MOUNT_FATAL;
    case MOUNT_ERROR_KEY_FAILURE:
      return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
    case MOUNT_ERROR_MOUNT_POINT_BUSY:
      return CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY;
    case MOUNT_ERROR_TPM_COMM_ERROR:
      return CRYPTOHOME_ERROR_TPM_COMM_ERROR;
    case MOUNT_ERROR_UNPRIVILEGED_KEY:
      return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED;
    case MOUNT_ERROR_TPM_DEFEND_LOCK:
      return CRYPTOHOME_ERROR_TPM_DEFEND_LOCK;
    case MOUNT_ERROR_TPM_UPDATE_REQUIRED:
      return CRYPTOHOME_ERROR_TPM_UPDATE_REQUIRED;
    case MOUNT_ERROR_USER_DOES_NOT_EXIST:
      return CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND;
    case MOUNT_ERROR_TPM_NEEDS_REBOOT:
      return CRYPTOHOME_ERROR_TPM_NEEDS_REBOOT;
    case MOUNT_ERROR_OLD_ENCRYPTION:
      return CRYPTOHOME_ERROR_MOUNT_OLD_ENCRYPTION;
    case MOUNT_ERROR_PREVIOUS_MIGRATION_INCOMPLETE:
      return CRYPTOHOME_ERROR_MOUNT_PREVIOUS_MIGRATION_INCOMPLETE;
    case MOUNT_ERROR_RECREATED:
      return CRYPTOHOME_ERROR_NOT_SET;
    default:
      return CRYPTOHOME_ERROR_MOUNT_FATAL;
  }
}

void Service::PostTask(const base::Location& from_here,
                       base::OnceClosure task) {
  int task_count = mount_thread_observer_.GetParallelTaskCount();
  if (task_count > 1) {
    ReportParallelTasks(task_count);
  }
  mount_thread_observer_.PostTask();
  mount_thread_.task_runner()->PostTask(from_here, std::move(task));
}

void Service::SendReply(DBusGMethodInvocation* context,
                        const BaseReply& reply) {
  // DBusBlobReply will take ownership of the |reply_str|.
  std::unique_ptr<std::string> reply_str(new std::string);
  reply.SerializeToString(reply_str.get());
  event_source_.AddEvent(
      std::make_unique<DBusBlobReply>(context, reply_str.release()));
}

void Service::SendDBusErrorReply(DBusGMethodInvocation* context,
                                 GQuark domain,
                                 gint code,
                                 const gchar* message) {
  if (message) {
    LOG(ERROR) << message;
  }
  GError* error = g_error_new_literal(domain, code, message);
  event_source_.AddEvent(std::make_unique<DBusErrorReply>(context, error));
}

bool Service::FilterActiveMounts(
    std::multimap<const FilePath, const FilePath>* mounts,
    std::multimap<const FilePath, const FilePath>* active_mounts,
    bool force) {
  bool skipped = false;
  std::set<const FilePath> children_to_preserve;

  for (auto match = mounts->begin(); match != mounts->end();) {
    auto curr = match;
    bool keep = false;
    // Walk each set of sources as one group since multimaps are key ordered.
    for (; match != mounts->end() && match->first == curr->first; ++match) {
      // Ignore known mounts.
      {
        base::AutoLock _lock(mounts_lock_);
        for (const auto& mount_pair : mounts_) {
          if (mount_pair.second->OwnsMountPoint(match->second)) {
            keep = true;
            // If !force, other mount points not owned scanned after should
            // be preserved as well.
            if (force)
              break;
          }
        }
      }
      // Ignore mounts pointing to children of used mounts.
      if (!force) {
        if (children_to_preserve.find(match->second) !=
            children_to_preserve.end()) {
          keep = true;
          skipped = true;
          LOG(WARNING) << "Stale mount " << match->second.value() << " from "
                       << match->first.value() << " is a just a child.";
        }
      }

      // Optionally, ignore mounts with open files.
      if (!keep && !force) {
        std::vector<ProcessInformation> processes;
        platform_->GetProcessesWithOpenFiles(match->second, &processes);
        if (processes.size()) {
          const std::vector<std::string> cmd_line = processes[0].get_cmd_line();
          const std::string first_cmd =
              (cmd_line.size() > 0 ? cmd_line[0] : "<empty>");
          LOG(WARNING) << "Stale mount " << match->second.value() << " from "
                       << match->first.value() << " has " << processes.size()
                       << " active holders. First one " << first_cmd;
          keep = true;
          skipped = true;
        }
      }
    }
    if (keep) {
      std::multimap<const FilePath, const FilePath> children;
      LOG(WARNING) << "Looking for children of " << curr->first;
      platform_->GetMountsBySourcePrefix(curr->first, &children);
      for (const auto& child : children) {
        children_to_preserve.insert(child.second);
      }

      active_mounts->insert(curr, match);
      mounts->erase(curr, match);
    }
  }
  return skipped;
}

void Service::GetEphemeralLoopDevicesMounts(
    std::multimap<const FilePath, const FilePath>* mounts) {
  std::multimap<const FilePath, const FilePath> loop_mounts;
  platform_->GetLoopDeviceMounts(&loop_mounts);

  const FilePath sparse_path =
      FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir);
  for (const auto& device : platform_->GetAttachedLoopDevices()) {
    // Ephemeral mounts are mounts from a loop device with ephemeral sparse
    // backing file.
    if (sparse_path.IsParent(device.backing_file)) {
      auto range = loop_mounts.equal_range(device.device);
      mounts->insert(range.first, range.second);
    }
  }
}

bool Service::CleanUpStaleMounts(bool force) {
  // This function is meant to aid in a clean recovery from a crashed or
  // manually restarted cryptohomed.  Cryptohomed may restart:
  // 1. Before any mounts occur
  // 2. While mounts are active
  // 3. During an unmount
  // In case #1, there should be no special work to be done.
  // The best way to disambiguate #2 and #3 is to determine if there are
  // any active open files on any stale mounts.  If there are open files,
  // then we've likely(*) resumed an active session. If there are not,
  // the last cryptohome should have been unmounted.
  // It's worth noting that a restart during active use doesn't impair
  // other user session behavior, like CheckKey, because it doesn't rely
  // exclusively on mount state.
  //
  // In the future, it may make sense to attempt to keep the MountMap
  // persisted to disk which would make resumption much easier.
  //
  // (*) Relies on the expectation that all processes have been killed off.
  std::multimap<const FilePath, const FilePath> shadow_mounts;
  std::multimap<const FilePath, const FilePath> ephemeral_mounts;
  platform_->GetMountsBySourcePrefix(homedirs_->shadow_root(), &shadow_mounts);
  GetEphemeralLoopDevicesMounts(&ephemeral_mounts);

  std::multimap<const FilePath, const FilePath> excluded;
  bool skipped = FilterActiveMounts(&shadow_mounts, &excluded, force);
  skipped |= FilterActiveMounts(&ephemeral_mounts, &excluded, force);

  std::vector<FilePath> excluded_mount_points;
  for (const auto& mount : excluded)
    excluded_mount_points.push_back(mount.second);
  UnloadPkcs11Tokens(excluded_mount_points);
  // Unmount anything left.
  for (const auto& match : shadow_mounts) {
    LOG(WARNING) << "Lazily unmounting stale shadow mount: "
                 << match.second.value() << " from " << match.first.value();
    platform_->Unmount(match.second, true, nullptr);
  }

  // Attempt to clear the encryption key for the shadow directories once
  // the mount has been unmounted. The encryption key needs to be cleared
  // after all the unmounts are done to ensure that none of the existing
  // submounts becomes inaccessible.
  if (force && !shadow_mounts.empty()) {
    // Attempt to clear fscrypt encryption keys for the shadow mounts.
    for (const auto& match : shadow_mounts) {
      if (!platform_->InvalidateDirCryptoKey(dircrypto::KeyReference(),
                                             match.first)) {
        LOG(WARNING) << "Failed to clear fscrypt keys for stale mount: "
                     << match.first;
      }
    }

    // Clear all keys in the user keyring for ecryptfs mounts.
    if (!platform_->ClearUserKeyring()) {
      LOG(WARNING) << "Failed to clear stale user keys.";
    }
  }

  for (const auto& match : ephemeral_mounts) {
    LOG(WARNING) << "Lazily unmounting stale ephemeral mount: "
                 << match.second.value() << " from " << match.first.value();
    platform_->Unmount(match.second, true, nullptr);
    // Clean up destination directory for ephemeral mounts under ephemeral
    // cryptohome dir.
    if (base::StartsWith(match.first.value(), kLoopPrefix,
                         base::CompareCase::SENSITIVE) &&
        FilePath(kEphemeralCryptohomeDir).IsParent(match.second)) {
      platform_->DeleteFile(match.second, true /* recursive */);
    }
  }

  // TODO(chromium:781821): Add autotests for this case.
  std::vector<Platform::LoopDevice> loop_devices =
      platform_->GetAttachedLoopDevices();
  const FilePath sparse_dir =
      FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir);
  std::vector<FilePath> stale_sparse_files;
  platform_->EnumerateDirectoryEntries(sparse_dir, false /* is_recursive */,
                                       &stale_sparse_files);
  for (const auto& device : loop_devices) {
    // Check whether it's created from an ephemeral sparse file.
    if (!sparse_dir.IsParent(device.backing_file))
      continue;
    if (excluded.count(device.device) == 0) {
      LOG(WARNING) << "Detaching stale loop device: " << device.device.value();
      if (!platform_->DetachLoop(device.device)) {
        ReportCryptohomeError(kEphemeralCleanUpFailed);
        PLOG(ERROR) << "Can't detach stale loop: " << device.device.value();
      }
    } else {
      // Remove if it's a non-stale loop device.
      stale_sparse_files.erase(
          std::remove(stale_sparse_files.begin(), stale_sparse_files.end(),
                      device.backing_file),
          stale_sparse_files.end());
    }
  }

  for (const auto& file : stale_sparse_files) {
    LOG(WARNING) << "Deleting stale ephemeral backing sparse file: "
                 << file.value();
    if (!platform_->DeleteFile(file, false /* recursive */)) {
      ReportCryptohomeError(kEphemeralCleanUpFailed);
      PLOG(ERROR) << "Failed to clean up ephemeral sparse file: "
                  << file.value();
    }
  }
  return skipped;
}

bool Service::CleanUpHiddenMounts() {
  bool ok = true;
  base::AutoLock _lock(mounts_lock_);
  for (auto it = mounts_.begin(); it != mounts_.end();) {
    scoped_refptr<cryptohome::Mount> mount = it->second;
    if (mount->IsMounted() && mount->IsShadowOnly()) {
      ok = ok && mount->UnmountCryptohome();
      it = mounts_.erase(it);
    } else {
      ++it;
    }
  }
  return ok;
}

bool Service::Initialize() {
  bool result = true;
  if (!tpm_ && use_tpm_) {
    tpm_ = Tpm::GetSingleton();
  }
  if (!tpm_init_ && initialize_tpm_) {
    default_tpm_init_.reset(new TpmInit(tpm_, platform_));
    tpm_init_ = default_tpm_init_.get();
  }
  if (!boot_lockbox_) {
    default_boot_lockbox_.reset(new BootLockbox(tpm_, platform_, crypto_));
    boot_lockbox_ = default_boot_lockbox_.get();
  }
  if (!boot_attributes_) {
    default_boot_attributes_.reset(
        new BootAttributes(boot_lockbox_, platform_));
    boot_attributes_ = default_boot_attributes_.get();
  }
  if (!firmware_management_parameters_) {
    default_firmware_management_params_.reset(
        new FirmwareManagementParameters(tpm_));
    firmware_management_parameters_ = default_firmware_management_params_.get();
  }
  crypto_->set_use_tpm(use_tpm_);
  if (!crypto_->Init(tpm_init_))
    return false;
  if (!homedirs_->Init(platform_, crypto_, user_timestamp_cache_.get()))
    return false;
  if (!homedirs_->GetSystemSalt(&system_salt_))
    return false;

  arc_disk_quota_->Initialize();

  // Install the type-info for the service with dbus.
  dbus_g_object_type_install_info(gobject::cryptohome_get_type(),
                                  &gobject::dbus_glib_cryptohome_object_info);
  if (!Reset()) {
    result = false;
  }

  // Registers the signal callbacks so the loop can exit gracefully.
  for (int sig : {SIGTERM, SIGINT, SIGHUP}) {
    g_unix_signal_add(sig, ShutdownService, this);
  }

  // This ownership taken signal registration should be done before any
  // Tpm::IsOwned() call so that Tpm can cache and update the ownership state
  // correctly without keeping requesting for the TPM status.
  ConnectOwnershipTakenSignal();

  // If the TPM is unowned or doesn't exist, it's safe for
  // this function to be called again. However, it shouldn't
  // be called across multiple threads in parallel.
  InitializeInstallAttributes();

  // Clean up any unreferenced mountpoints at startup.
  CleanUpStaleMounts(false);

  AttestationInitialize();

  async_complete_signal_ =
      g_signal_lookup("async_call_status", gobject::cryptohome_get_type());
  if (!async_complete_signal_) {
    async_complete_signal_ =
        g_signal_new("async_call_status", gobject::cryptohome_get_type(),
                     G_SIGNAL_RUN_LAST, 0, NULL, NULL, nullptr, G_TYPE_NONE, 3,
                     G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_INT);
  }

  async_data_complete_signal_ = g_signal_lookup("async_call_status_with_data",
                                                gobject::cryptohome_get_type());
  if (!async_data_complete_signal_) {
    async_data_complete_signal_ = g_signal_new(
        "async_call_status_with_data", gobject::cryptohome_get_type(),
        G_SIGNAL_RUN_LAST, 0, NULL, NULL, nullptr, G_TYPE_NONE, 3, G_TYPE_INT,
        G_TYPE_BOOLEAN, DBUS_TYPE_G_UCHAR_ARRAY);
  }

  tpm_init_signal_ =
      g_signal_lookup("tpm_init_status", gobject::cryptohome_get_type());
  if (!tpm_init_signal_) {
    tpm_init_signal_ =
        g_signal_new("tpm_init_status", gobject::cryptohome_get_type(),
                     G_SIGNAL_RUN_LAST, 0, NULL, NULL, nullptr, G_TYPE_NONE, 3,
                     G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
  }

  low_disk_space_signal_ =
      g_signal_lookup("low_disk_space", gobject::cryptohome_get_type());
  if (!low_disk_space_signal_) {
    low_disk_space_signal_ = g_signal_new(
        "low_disk_space", gobject::cryptohome_get_type(), G_SIGNAL_RUN_LAST, 0,
        NULL, NULL, nullptr, G_TYPE_NONE, 1, G_TYPE_UINT64);
  }

  dircrypto_migration_progress_signal_ = g_signal_lookup(
      "dircrypto_migration_progress", gobject::cryptohome_get_type());
  if (!dircrypto_migration_progress_signal_) {
    dircrypto_migration_progress_signal_ = g_signal_new(
        "dircrypto_migration_progress", gobject::cryptohome_get_type(),
        G_SIGNAL_RUN_LAST, 0, NULL, NULL, nullptr, G_TYPE_NONE, 3, G_TYPE_INT,
        G_TYPE_UINT64, G_TYPE_UINT64);
  }

  base::Thread::Options options;
#if BASE_VER < 780000
  options.message_loop_type = base::MessagePumpType::IO;
#else
  options.message_pump_type = base::MessagePumpType::IO;
#endif
  mount_thread_.StartWithOptions(options);

  // Add task observer, message_loop is only availible after the thread start.
  // We can only add observer inside the thread.
  AddTaskObserverToThread(&mount_thread_, &mount_thread_observer_);

  // TODO(wad) Determine if this should only be called if
  //           tpm->IsEnabled() is true.
  if (tpm_ && initialize_tpm_) {
    tpm_init_->Init(
        base::Bind(&Service::OwnershipCallback, base::Unretained(this)));
    if (!SeedUrandom()) {
      LOG(ERROR) << "FAILED TO SEED /dev/urandom AT START";
    }
    AttestationInitializeTpm();
    if (tpm_init_->ShallInitialize() ||
        base::CommandLine::ForCurrentProcess()->HasSwitch(
            kAutoInitializeTpmSwitch)) {
      tpm_init_->AsyncTakeOwnership();
    }
  }

  last_user_activity_timestamp_time_ = platform_->GetCurrentTime();

  // Clean up space on start (once).
  PostTask(FROM_HERE,
           base::Bind(&Service::DoAutoCleanup, base::Unretained(this)));

  // Start scheduling periodic check for low-disk space and cleanup events.
  // Subsequent events are scheduled by the callback itself.
  PostTask(FROM_HERE,
           base::Bind(&Service::LowDiskCallback, base::Unretained(this)));

  // Start scheduling periodic TPM alerts upload to UMA. Subsequent events are
  // scheduled by the callback itself.
  PostTask(FROM_HERE, base::Bind(&Service::UploadAlertsDataCallback,
                                 base::Unretained(this)));

  // Create a FingerprintManager for talking to biod over dbus..
  PostTask(FROM_HERE, base::Bind(&Service::CreateFingerprintManager,
                                 base::Unretained(this)));

  // TODO(keescook,ellyjones) Make this mock-able.
  auto mountfn =
      base::Bind(&Service::StatefulRecoveryMount, base::Unretained(this));
  auto unmountfn =
      base::Bind(&Service::StatefulRecoveryUnmount, base::Unretained(this));
  auto isownerfn = base::Bind(&Service::IsOwner, base::Unretained(this));
  StatefulRecovery recovery(platform_, mountfn, unmountfn, isownerfn);
  if (recovery.Requested()) {
    if (recovery.Recover())
      LOG(INFO) << "A stateful recovery was performed successfully.";
    recovery.PerformReboot();
  }

  boot_attributes_->Load();

  return result;
}

bool Service::StatefulRecoveryMount(const std::string& username,
                                    const std::string& passkey,
                                    FilePath* out_home_path) {
  gint error_code;
  gboolean result;
  GError* error = NULL;

  if (!Mount(username.c_str(), passkey.c_str(), false, false, &error_code,
             &result, &error) ||
      !result) {
    LOG(ERROR) << "Could not authenticate user '" << username
               << "' for stateful recovery: "
               << (error ? error->message : "[null]") << " (code:" << error_code
               << ")";
    return false;
  }

  if (!GetMountPointForUser(username.c_str(), out_home_path)) {
    LOG(ERROR) << "Mount point missing after successful mount call!?";
    return false;
  }
  return true;
}

bool Service::StatefulRecoveryUnmount() {
  gboolean result;
  GError* error = NULL;
  if (!Unmount(&result, &error) || !result) {
    LOG(ERROR) << "Failed to unmount after stateful recovery: "
               << (error ? error->message : "[null]");
    return false;
  }
  return true;
}

bool Service::IsOwner(const std::string& userid) {
  std::string owner;
  if (homedirs_->GetPlainOwner(&owner) && userid.length() && userid == owner)
    return true;
  return false;
}

void Service::InitializeInstallAttributes() {
  // The TPM owning instance may have changed since initialization.
  // InstallAttributes can handle a NULL or !IsEnabled Tpm object.
  install_attrs_->SetTpm(tpm_);
  install_attrs_->Init(tpm_init_);

  // Check if the machine is enterprise owned and report to mount_ then.
  DetectEnterpriseOwnership();
}

void Service::DoInitializePkcs11(cryptohome::Mount* mount) {
  bool still_mounted = false;
  {
    base::AutoLock _lock(mounts_lock_);
    for (const auto& mount_pair : mounts_) {
      if (mount_pair.second.get() == mount) {
        still_mounted = true;
      }
    }
  }
  if (!still_mounted) {
    LOG(INFO) << "PKCS#11 initialization cancelled";
    return;
  }
  if (mount->IsMounted() &&
      mount->pkcs11_state() == cryptohome::Mount::kIsBeingInitialized) {
    mount->InsertPkcs11Token();
  }
  LOG(INFO) << "PKCS#11 initialization succeeded.";
  mount->set_pkcs11_state(cryptohome::Mount::kIsInitialized);
}

void Service::InitializePkcs11(cryptohome::Mount* mount) {
  if (!mount) {
    LOG(ERROR) << "InitializePkcs11 called with NULL mount!";
    return;
  }
  // Wait for ownership if there is a working TPM.
  if (tpm_ && tpm_->IsEnabled() && !tpm_->IsOwned()) {
    LOG(WARNING) << "TPM was not owned. TPM initialization call back will"
                 << " handle PKCS#11 initialization.";
    mount->set_pkcs11_state(cryptohome::Mount::kIsWaitingOnTPM);
    return;
  }

  // Ok, so the TPM is owned. Time to request asynchronous initialization of
  // PKCS#11.
  // Make sure cryptohome is mounted, otherwise all of this is for naught.
  if (!mount->IsMounted()) {
    LOG(WARNING) << "PKCS#11 initialization requested but cryptohome is"
                 << " not mounted.";
    return;
  }

  // Reset PKCS#11 initialization status. A successful completion of
  // MountTaskPkcs11_Init would set it in the service thread via NotifyEvent().
  ReportTimerStart(kPkcs11InitTimer);
  mount->set_pkcs11_state(cryptohome::Mount::kIsBeingInitialized);

  mount_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&Service::DoInitializePkcs11, base::Unretained(this),
                     base::Unretained(mount)));
}

bool Service::SeedUrandom() {
  brillo::Blob random;
  if (!tpm_->GetRandomDataBlob(kDefaultRandomSeedLength, &random)) {
    LOG(ERROR) << "Could not get random data from the TPM";
    return false;
  }
  if (!platform_->WriteFile(FilePath(kDefaultEntropySourcePath), random)) {
    LOG(ERROR) << "Error writing data to " << kDefaultEntropySourcePath;
    return false;
  }
  return true;
}

void Service::UploadAlertsDataCallback() {
  Tpm::AlertsData alerts;

  if (!use_tpm_) {
    LOG(WARNING) << "TPM is not enabled. Disabling TPM alert metrics";
    return;
  }

  if (tpm_) {
    bool supported = tpm_->GetAlertsData(&alerts);
    if (!supported) {
      // success return code and unknown chip family means that chip does not
      // support GetAlerts information. Return here as no need to reschedule
      // the delayed task.
      LOG(INFO) << "The TPM chip does not support GetAlertsData. "
                << "Stop UploadAlertsData task.";
      return;
    }

    ReportAlertsData(alerts);
  }

  mount_thread_.task_runner()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&Service::UploadAlertsDataCallback, base::Unretained(this)),
      base::TimeDelta::FromMilliseconds(upload_alerts_period_ms_));
}

bool Service::Reset() {
  if (cryptohome_)
    g_object_unref(cryptohome_);
  cryptohome_ = reinterpret_cast<gobject::Cryptohome*>(
      g_object_new(gobject::cryptohome_get_type(), NULL));
  // Allow references to this instance.
  cryptohome_->service = this;

  if (loop_) {
    ::g_main_loop_unref(loop_);
  }
  loop_ = g_main_loop_new(NULL, false);
  if (!loop_) {
    LOG(ERROR) << "Failed to create main loop";
    return false;
  }

  // Install the local event source for handling async results
  event_source_.Reset(event_source_sink_, g_main_loop_get_context(loop_));
  return true;
}

void Service::NotifyEvent(CryptohomeEventBase* event) {
  if (!strcmp(event->GetEventName(), kMountTaskResultEventType)) {
    MountTaskResult* result = static_cast<MountTaskResult*>(event);
    if (!result->return_data()) {
      g_signal_emit(cryptohome_, async_complete_signal_, 0,
                    result->sequence_id(), result->return_status(),
                    result->return_code());
      // TODO(wad) are there any non-mount uses of this type?
      if (!result->return_status()) {
        RemoveMount(result->mount().get());
      }
      SendAsyncIdInfoToUma(result->sequence_id(), base::Time::Now());
    } else {
      brillo::glib::ScopedArray tmp_array(g_array_new(FALSE, FALSE, 1));
      g_array_append_vals(tmp_array.get(), result->return_data()->data(),
                          result->return_data()->size());
      g_signal_emit(cryptohome_, async_data_complete_signal_, 0,
                    result->sequence_id(), result->return_status(),
                    tmp_array.get());
      brillo::SecureMemset(tmp_array.get()->data, 0, tmp_array.get()->len);
      SendAsyncIdInfoToUma(result->sequence_id(), base::Time::Now());
    }
    if (result->pkcs11_init()) {
      LOG(INFO) << "An asynchronous mount request with sequence id: "
                << result->sequence_id() << " finished; doing PKCS11 init...";
      // We only report and init PKCS#11 for successful mounts.
      if (result->return_status()) {
        InitializePkcs11(result->mount().get());
      }
    } else if (result->guest()) {
      if (!result->return_status()) {
        DLOG(INFO) << "Dropping MountMap entry for failed Guest mount.";
        RemoveMountForUser(guest_user_);
      }
    }
  } else if (!strcmp(event->GetEventName(), kTpmInitStatusEventType)) {
    TpmInitStatus* result = static_cast<TpmInitStatus*>(event);
    g_signal_emit(cryptohome_, tpm_init_signal_, 0, tpm_init_->IsTpmReady(),
                  tpm_init_->IsTpmEnabled(), result->get_took_ownership());
    // TODO(wad) should we package up a InstallAttributes status here too?
  } else if (!strcmp(event->GetEventName(), kDBusErrorReplyEventType)) {
    DBusErrorReply* result = static_cast<DBusErrorReply*>(event);
    result->Run();
  } else if (!strcmp(event->GetEventName(), kDBusBlobReplyEventType)) {
    DBusBlobReply* result = static_cast<DBusBlobReply*>(event);
    result->Run();
  } else if (!strcmp(event->GetEventName(), kDBusReplyEventType)) {
    DBusReply* result = static_cast<DBusReply*>(event);
    result->Run();
  } else if (!strcmp(event->GetEventName(),
                     kDircryptoMigrationProgressEventType)) {
    auto* progress = static_cast<DircryptoMigrationProgress*>(event);
    g_signal_emit(cryptohome_, dircrypto_migration_progress_signal_,
                  0 /* signal detail (not used) */,
                  static_cast<int32_t>(progress->status()),
                  progress->current_bytes(), progress->total_bytes());
  } else if (!strcmp(event->GetEventName(), kClosureEventType)) {
    ClosureEvent* closure_event = static_cast<ClosureEvent*>(event);
    closure_event->Run();
  }
}

void Service::DoResetTPMContext(cryptohome::Mount* mount) {
  if (mount) {
    Crypto* crypto = mount->crypto();
    if (crypto) {
      crypto->EnsureTpm(true);
    }
  }
}

void Service::OwnershipCallback(bool status, bool took_ownership) {
  // Note that this function should only be called once during the lifetime of
  // this process, extra calls will be dropped.
  if (ownership_callback_has_run_) {
    LOG(WARNING) << "Duplicated call to OwnershipCallback.";
    return;
  }
  ownership_callback_has_run_ = true;

  if (took_ownership) {
    ReportTimerStop(kTpmTakeOwnershipTimer);

    // Since ownership is already taken, we are not currently taking ownership.
    tpm_init_->SetTpmBeingOwned(false);

    // Let the |tpm_| object know about the ownership status
    if (tpm_) {
      tpm_->HandleOwnershipTakenEvent();
    }

    // When TPM initialization finishes, we need to tell every Mount to
    // reinitialize its TPM context, since the TPM is now useable, and we might
    // need to kick off their PKCS11 initialization if they were blocked before.
    {
      base::AutoLock _lock(mounts_lock_);
      for (const auto& mount_pair : mounts_) {
        mount_thread_.task_runner()->PostTask(
            FROM_HERE,
            base::Bind(&Service::DoResetTPMContext, base::Unretained(this),
                       base::RetainedRef(mount_pair.second)));
      }
    }
  }
  PostTask(FROM_HERE,
           base::Bind(&Service::ConfigureOwnedTpm, base::Unretained(this),
                      status, took_ownership));
}

void Service::ConfigureOwnedTpm(bool status, bool took_ownership) {
  LOG(INFO) << "Configuring TPM, ownership taken: " << took_ownership << ".";
  if (took_ownership) {
    // Check if we have pending pkcs11 init tasks due to tpm ownership
    // not being done earlier. Trigger initialization if so.
    {
      base::AutoLock _lock(mounts_lock_);
      for (const auto& mount_pair : mounts_) {
        cryptohome::Mount* mount = mount_pair.second.get();
        if (mount->pkcs11_state() == cryptohome::Mount::kIsWaitingOnTPM) {
          InitializePkcs11(mount);
        }
      }
    }
    // Initialize the install-time locked attributes since we
    // can't do it prior to ownership.
    InitializeInstallAttributes();
  }
  event_source_.AddEvent(
      std::make_unique<TpmInitStatus>(took_ownership, status));

  // Do attestation work after AddEvent because it may take long.
  AttestationInitializeTpmComplete();

  // If we mounted before the TPM finished initialization, we must
  // finalize the install attributes now too, otherwise it takes a
  // full re-login cycle to finalize.
  gboolean mounted = FALSE;
  bool is_mounted = (IsMounted(&mounted, NULL) && mounted);
  if (is_mounted && took_ownership &&
      install_attrs_->status() == InstallAttributes::Status::kFirstInstall) {
    scoped_refptr<cryptohome::Mount> guest_mount = GetMountForUser(guest_user_);
    bool guest_mounted = guest_mount.get() && guest_mount->IsMounted();
    if (!guest_mounted)
      install_attrs_->Finalize();
  }
}

void Service::CreateFingerprintManager() {
  if (fingerprint_manager_.get() != nullptr) {
    return;
  }

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;

  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));

  if (!bus->Connect()) {
    LOG(ERROR) << "CreateFingerprintManager: Cannot connect to D-Bus.";
    return;
  }

  fingerprint_manager_ = FingerprintManager::Create(
      bus, dbus::ObjectPath(std::string(biod::kBiodServicePath)
                                .append(kCrosFpBiometricsManagerRelativePath)));
}

void Service::CompleteFingerprintCheckKeyEx(DBusGMethodInvocation* context,
                                            FingerprintScanStatus status) {
  BaseReply reply;
  if (status == FingerprintScanStatus::FAILED_RETRY_ALLOWED)
    reply.set_error(CRYPTOHOME_ERROR_FINGERPRINT_RETRY_REQUIRED);
  if (status == FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED)
    reply.set_error(CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
  SendReply(context, reply);
}

void Service::DoCheckKeyEx(std::unique_ptr<AccountIdentifier> identifier,
                           std::unique_ptr<AuthorizationRequest> authorization,
                           std::unique_ptr<CheckKeyRequest> check_key_request,
                           DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !check_key_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  // Process challenge-response credentials asynchronously.
  if (authorization->key().data().type() ==
      KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    DoChallengeResponseCheckKeyEx(std::move(identifier),
                                  std::move(authorization), context);
    return;
  }

  if (authorization->key().data().type() == KeyData::KEY_TYPE_FINGERPRINT) {
    const std::string obfuscated_username =
        SanitizeUserNameWithSalt(GetAccountId(*identifier), system_salt_);
    BaseReply reply;
    if (!fingerprint_manager_) {
      // Fingerprint manager failed to initialize, or the device may not
      // support fingerprint auth at all.
      reply.set_error(CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
      SendReply(context, reply);
      return;
    }
    if (!fingerprint_manager_->HasAuthSessionForUser(obfuscated_username)) {
      reply.set_error(CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
      SendReply(context, reply);
      return;
    }
    fingerprint_manager_->SetAuthScanDoneCallback(
        base::Bind(&Service::CompleteFingerprintCheckKeyEx,
                   base::Unretained(this), context));

    return;
  }

  // An AuthorizationRequest key without a label will test against
  // all VaultKeysets of a compatible key().data().type().
  if (authorization->key().secret().empty()) {
    SendInvalidArgsReply(context, "No key secret supplied");
    return;
  }

  Credentials credentials(GetAccountId(*identifier),
                          SecureBlob(authorization->key().secret().begin(),
                                     authorization->key().secret().end()));
  credentials.set_key_data(authorization->key().data());

  const std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);

  BaseReply reply;
  bool found_valid_credentials = false;
  {
    base::AutoLock _lock(mounts_lock_);
    for (const auto& mount_pair : mounts_) {
      if (mount_pair.second->AreSameUser(obfuscated_username)) {
        found_valid_credentials = mount_pair.second->AreValid(credentials);
        break;
      }
    }
  }
  if (found_valid_credentials) {
    // Entered the right creds, so reset LE credentials.
    homedirs_->ResetLECredentials(credentials);
    SendReply(context, reply);
    return;
  }
  // Fallthrough to HomeDirs to cover different keys for the same user.

  if (homedirs_->Exists(obfuscated_username)) {
    if (homedirs_->AreCredentialsValid(credentials)) {
      homedirs_->ResetLECredentials(credentials);
    } else {
      // TODO(wad) Should this pass along KEY_NOT_FOUND too?
      reply.set_error(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
      ResetDictionaryAttackMitigation();
    }
  } else {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
  }
  SendReply(context, reply);
}

gboolean Service::CheckKeyEx(GArray* account_id,
                             GArray* authorization_request,
                             GArray* check_key_request,
                             DBusGMethodInvocation* context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<CheckKeyRequest> request(new CheckKeyRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  if (!request->ParseFromArray(check_key_request->data, check_key_request->len))
    request.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(FROM_HERE, base::Bind(&Service::DoCheckKeyEx, base::Unretained(this),
                                 base::Passed(std::move(identifier)),
                                 base::Passed(std::move(authorization)),
                                 base::Passed(std::move(request)),
                                 base::Unretained(context)));
  return TRUE;
}

void Service::DoRemoveKeyEx(AccountIdentifier* identifier,
                            AuthorizationRequest* authorization,
                            RemoveKeyRequest* remove_key_request,
                            DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !remove_key_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  // An AuthorizationRequest key without a label will test against
  // all VaultKeysets of a compatible key().data().type().
  if (authorization->key().secret().empty()) {
    SendInvalidArgsReply(context, "No key secret supplied");
    return;
  }

  if (remove_key_request->key().data().label().empty()) {
    SendInvalidArgsReply(context, "No label provided for target key");
    return;
  }

  BaseReply reply;
  Credentials credentials(GetAccountId(*identifier),
                          SecureBlob(authorization->key().secret().begin(),
                                     authorization->key().secret().end()));
  credentials.set_key_data(authorization->key().data());

  if (!homedirs_->Exists(credentials.GetObfuscatedUsername(system_salt_))) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  reply.set_error(
      homedirs_->RemoveKeyset(credentials, remove_key_request->key().data()));
  if (reply.error() == CRYPTOHOME_ERROR_NOT_SET) {
    // Don't set the error if there wasn't one.
    reply.clear_error();
  }
  SendReply(context, reply);
}

gboolean Service::RemoveKeyEx(GArray* account_id,
                              GArray* authorization_request,
                              GArray* remove_key_request,
                              DBusGMethodInvocation* context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<RemoveKeyRequest> request(new RemoveKeyRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  if (!request->ParseFromArray(remove_key_request->data,
                               remove_key_request->len))
    request.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(
      FROM_HERE,
      base::Bind(&Service::DoRemoveKeyEx, base::Unretained(this),
                 base::Owned(identifier.release()),
                 base::Owned(authorization.release()),
                 base::Owned(request.release()), base::Unretained(context)));
  return TRUE;
}

void Service::DoMassRemoveKeys(AccountIdentifier* account_id,
                               AuthorizationRequest* authorization_request,
                               MassRemoveKeysRequest* mass_remove_keys_request,
                               DBusGMethodInvocation* context) {
  if (!account_id || !authorization_request || !mass_remove_keys_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }
  const std::string username = GetAccountId(*account_id);
  if (username.empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }
  if (authorization_request->key().secret().empty()) {
    SendInvalidArgsReply(context, "No key secret supplied");
    return;
  }
  BaseReply reply;
  Credentials credentials(
      username, SecureBlob(authorization_request->key().secret().begin(),
                           authorization_request->key().secret().end()));
  credentials.set_key_data(authorization_request->key().data());
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(username, system_salt_);
  if (!homedirs_->Exists(obfuscated_username)) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }
  if (!homedirs_->AreCredentialsValid(credentials)) {
    reply.set_error(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
    SendReply(context, reply);
    return;
  }
  // get all labels under the username
  std::vector<std::string> labels;
  if (!homedirs_->GetVaultKeysetLabels(obfuscated_username, &labels)) {
    reply.set_error(CRYPTOHOME_ERROR_KEY_NOT_FOUND);
    SendReply(context, reply);
    return;
  }
  // get all exempt labels from mass_remove_keys_request
  std::unordered_set<std::string> exempt_labels;
  for (int i = 0; i < mass_remove_keys_request->exempt_key_data_size(); i++) {
    exempt_labels.insert(mass_remove_keys_request->exempt_key_data(i).label());
  }
  for (std::string label : labels) {
    if (exempt_labels.find(label) == exempt_labels.end()) {
      // non-exempt label, should be removed
      std::unique_ptr<VaultKeyset> remove_vk(
          homedirs_->GetVaultKeyset(obfuscated_username, label));
      if (!homedirs_->ForceRemoveKeyset(obfuscated_username,
                                        remove_vk->legacy_index())) {
        LOG(ERROR) << "MassRemoveKeys: failed to remove keyset " << label;
        reply.set_error(CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
        SendReply(context, reply);
        return;
      }
    }
  }
  SendReply(context, reply);
}

gboolean Service::MassRemoveKeys(GArray* account_id,
                                 GArray* authorization_request,
                                 GArray* mass_remove_keys_request,
                                 DBusGMethodInvocation* context) {
  auto identifier = std::make_unique<AccountIdentifier>();
  auto authorization = std::make_unique<AuthorizationRequest>();
  auto request = std::make_unique<MassRemoveKeysRequest>();
  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(nullptr);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(nullptr);
  if (!request->ParseFromArray(mass_remove_keys_request->data,
                               mass_remove_keys_request->len))
    request.reset(nullptr);

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(
      FROM_HERE,
      base::Bind(&Service::DoMassRemoveKeys, base::Unretained(this),
                 base::Owned(identifier.release()),
                 base::Owned(authorization.release()),
                 base::Owned(request.release()), base::Unretained(context)));

  return TRUE;
}

void Service::DoListKeysEx(AccountIdentifier* identifier,
                           AuthorizationRequest* authorization,
                           ListKeysRequest* list_keys_request,
                           DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !list_keys_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  const std::string username = GetAccountId(*identifier);
  if (username.empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }
  BaseReply reply;
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(username, system_salt_);
  if (!homedirs_->Exists(obfuscated_username)) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }
  std::vector<std::string> labels;
  if (!homedirs_->GetVaultKeysetLabels(obfuscated_username, &labels)) {
    reply.set_error(CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  }
  ListKeysReply* list_keys_reply = reply.MutableExtension(ListKeysReply::reply);

  for (const auto& label : labels)
    list_keys_reply->add_labels(label);

  SendReply(context, reply);
}

gboolean Service::ListKeysEx(GArray* account_id,
                             GArray* authorization_request,
                             GArray* list_keys_request,
                             DBusGMethodInvocation* context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<ListKeysRequest> request(new ListKeysRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  if (!request->ParseFromArray(list_keys_request->data, list_keys_request->len))
    request.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(FROM_HERE, base::Bind(&Service::DoListKeysEx, base::Unretained(this),
                                 base::Owned(identifier.release()),
                                 base::Owned(authorization.release()),
                                 base::Owned(request.release()),
                                 base::Unretained(context)));
  return TRUE;
}

void Service::DoGetKeyDataEx(AccountIdentifier* identifier,
                             AuthorizationRequest* authorization,
                             GetKeyDataRequest* get_key_data_request,
                             DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !get_key_data_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  if (!get_key_data_request->has_key()) {
    SendInvalidArgsReply(context, "No key attributes provided");
    return;
  }

  BaseReply reply;
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(GetAccountId(*identifier), system_salt_);
  if (!homedirs_->Exists(obfuscated_username)) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  GetKeyDataReply* sub_reply = reply.MutableExtension(GetKeyDataReply::reply);
  // Requests only support using the key label at present.
  std::unique_ptr<VaultKeyset> vk(homedirs_->GetVaultKeyset(
      obfuscated_username, get_key_data_request->key().data().label()));
  if (vk) {
    KeyData* new_kd = sub_reply->add_key_data();
    *new_kd = vk->serialized().key_data();
    // Clear any symmetric KeyAuthorizationSecrets even if they are wrapped.
    for (int a = 0; a < new_kd->authorization_data_size(); ++a) {
      KeyAuthorizationData* auth_data = new_kd->mutable_authorization_data(a);
      for (int s = 0; s < auth_data->secrets_size(); ++s) {
        auth_data->mutable_secrets(s)->clear_symmetric_key();
        auth_data->mutable_secrets(s)->set_wrapped(false);
      }
    }
  }
  // No error is thrown if there is no match.
  reply.clear_error();
  SendReply(context, reply);
}

gboolean Service::GetKeyDataEx(GArray* account_id,
                               GArray* authorization_request,
                               GArray* get_key_data_request,
                               DBusGMethodInvocation* context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<GetKeyDataRequest> request(new GetKeyDataRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len)) {
    identifier.reset(NULL);
  }
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len)) {
    authorization.reset(NULL);
  }
  if (!request->ParseFromArray(get_key_data_request->data,
                               get_key_data_request->len)) {
    request.reset(NULL);
  }

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(
      FROM_HERE,
      base::Bind(&Service::DoGetKeyDataEx, base::Unretained(this),
                 base::Owned(identifier.release()),
                 base::Owned(authorization.release()),
                 base::Owned(request.release()), base::Unretained(context)));
  return TRUE;
}

void Service::DoMigrateKeyEx(AccountIdentifier* account,
                             AuthorizationRequest* auth_request,
                             MigrateKeyRequest* migrate_request,
                             DBusGMethodInvocation* context) {
  if (!account || !auth_request || !migrate_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  // Setup a reply to use during error handling.
  BaseReply reply;

  if (account->account_id().empty()) {
    SendInvalidArgsReply(context, "Must supply account_id.");
    return;
  }

  Credentials credentials(account->account_id(),
                          SecureBlob(migrate_request->secret()));

  scoped_refptr<cryptohome::Mount> mount =
      GetMountForUser(GetAccountId(*account));
  if (!mount) {
    // Create a mount which is not referenced by mounts_ and is cleaned up
    // after Migration is over.
    mount = CreateUntrackedMountForUser(GetAccountId(*account));
  }
  int key_index = -1;
  if (!mount) {
    LOG(ERROR) << "Failed to obtain Mount for Migrate";
    reply.set_error(CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED);
  } else if (!homedirs_->Migrate(credentials,
                                 SecureBlob(auth_request->key().secret()),
                                 &key_index)) {
    reply.set_error(CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED);
  } else {
    if (!mount->SetUserCreds(credentials, key_index))
      LOG(WARNING) << "Failed to set new creds";
    reply.clear_error();
  }

  SendReply(context, reply);
}

gboolean Service::MigrateKeyEx(GArray* account_ary,
                               GArray* auth_request_ary,
                               GArray* migrate_request_ary,
                               DBusGMethodInvocation* context) {
  auto account = std::make_unique<AccountIdentifier>();
  auto auth_request = std::make_unique<AuthorizationRequest>();
  auto migrate_request = std::make_unique<MigrateKeyRequest>();

  // On parsing failure, pass along a nullptr.
  if (!account->ParseFromArray(account_ary->data, account_ary->len))
    account.reset(nullptr);
  if (!auth_request->ParseFromArray(auth_request_ary->data,
                                    auth_request_ary->len)) {
    auth_request.reset(nullptr);
  }
  if (!migrate_request->ParseFromArray(migrate_request_ary->data,
                                       migrate_request_ary->len)) {
    migrate_request.reset(nullptr);
  }

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(FROM_HERE,
           base::Bind(&Service::DoMigrateKeyEx, base::Unretained(this),
                      base::Owned(account.release()),
                      base::Owned(auth_request.release()),
                      base::Owned(migrate_request.release()),
                      base::Unretained(context)));

  return TRUE;
}

void Service::DoAddKeyEx(AccountIdentifier* identifier,
                         AuthorizationRequest* authorization,
                         AddKeyRequest* add_key_request,
                         DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !add_key_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  // Setup a reply for use during error handling.
  BaseReply reply;

  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  // An AuthorizationRequest key without a label will test against
  // all VaultKeysets of a compatible key().data().type().
  if (authorization->key().secret().empty()) {
    SendInvalidArgsReply(context, "No key secret supplied");
    return;
  }

  if (!add_key_request->has_key() || add_key_request->key().secret().empty()) {
    SendInvalidArgsReply(context, "No new key supplied");
    return;
  }

  if (add_key_request->key().data().label().empty()) {
    SendInvalidArgsReply(context, "No new key label supplied");
    return;
  }

  // Ensure any new keys do not contain a wrapped authorization key.
  if (KeyHasWrappedAuthorizationSecrets(add_key_request->key())) {
    SendInvalidArgsReply(context, "KeyAuthorizationSecrets may not be wrapped");
    return;
  }

  Credentials credentials(GetAccountId(*identifier),
                          SecureBlob(authorization->key().secret().begin(),
                                     authorization->key().secret().end()));
  credentials.set_key_data(authorization->key().data());

  if (!homedirs_->Exists(credentials.GetObfuscatedUsername(system_salt_))) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  int index = -1;
  SecureBlob new_secret(add_key_request->key().secret().begin(),
                        add_key_request->key().secret().end());
  reply.set_error(homedirs_->AddKeyset(
      credentials, new_secret, &add_key_request->key().data(),
      add_key_request->clobber_if_exists(), &index));
  if (reply.error() == CRYPTOHOME_ERROR_NOT_SET) {
    // Don't set the error if there wasn't one.
    reply.clear_error();
  }
  SendReply(context, reply);
}

gboolean Service::AddKeyEx(GArray* account_id,
                           GArray* authorization_request,
                           GArray* add_key_request,
                           DBusGMethodInvocation* context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<AddKeyRequest> request(new AddKeyRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  if (!request->ParseFromArray(add_key_request->data, add_key_request->len))
    request.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(FROM_HERE, base::Bind(&Service::DoAddKeyEx, base::Unretained(this),
                                 base::Owned(identifier.release()),
                                 base::Owned(authorization.release()),
                                 base::Owned(request.release()),
                                 base::Unretained(context)));
  return TRUE;
}

void Service::DoAddDataRestoreKey(AccountIdentifier* identifier,
                                  AuthorizationRequest* authorization,
                                  DBusGMethodInvocation* context) {
  if (!identifier || !authorization) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }
  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }
  if (!authorization->has_key() || !authorization->key().has_secret()) {
    SendInvalidArgsReply(context, "No key secret supplied");
    return;
  }
  KeyData new_key_data;
  BaseReply reply;
  const auto data_restore_key =
      CryptoLib::CreateSecureRandomBlob(kDefaultDataRestoreKeyLength);
  new_key_data.set_label(kDataRestoreKeyLabel);
  Credentials credentials(GetAccountId(*identifier),
                          SecureBlob(authorization->key().secret().begin(),
                                     authorization->key().secret().end()));
  credentials.set_key_data(authorization->key().data());
  if (!homedirs_->Exists(credentials.GetObfuscatedUsername(system_salt_))) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }
  int index = -1;
  reply.set_error(homedirs_->AddKeyset(credentials, data_restore_key,
                                       &new_key_data, true, &index));
  if (reply.error() == CRYPTOHOME_ERROR_NOT_SET) {
    // Don't set the error if there wasn't one.
    reply.clear_error();
  } else {
    SendReply(context, reply);
    return;
  }
  // send the raw bytes of data restore key as a part of reply back to caller
  AddDataRestoreKeyReply* extension =
      reply.MutableExtension(AddDataRestoreKeyReply::reply);
  extension->set_data_restore_key(data_restore_key.to_string());
  SendReply(context, reply);
}

gboolean Service::AddDataRestoreKey(GArray* account_id,
                                    GArray* authorization_request,
                                    DBusGMethodInvocation* context) {
  auto identifier = std::make_unique<AccountIdentifier>();
  auto authorization = std::make_unique<AuthorizationRequest>();
  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  PostTask(FROM_HERE,
           base::Bind(&Service::DoAddDataRestoreKey, base::Unretained(this),
                      base::Owned(identifier.release()),
                      base::Owned(authorization.release()),
                      base::Unretained(context)));
  return TRUE;
}

void Service::DoUpdateKeyEx(AccountIdentifier* identifier,
                            AuthorizationRequest* authorization,
                            UpdateKeyRequest* update_key_request,
                            DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !update_key_request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  // Setup a reply for use during error handling.
  BaseReply reply;

  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  // An AuthorizationRequest key without a label will test against
  // all VaultKeysets of a compatible key().data().type().
  if (authorization->key().secret().empty()) {
    SendInvalidArgsReply(context, "No key secret supplied");
    return;
  }

  // Any undefined field in changes() will be left as it is.
  if (!update_key_request->has_changes()) {
    SendInvalidArgsReply(context, "No updates requested");
    return;
  }

  if (KeyHasWrappedAuthorizationSecrets(update_key_request->changes())) {
    SendInvalidArgsReply(context, "KeyAuthorizationSecrets may not be wrapped");
    return;
  }

  Credentials credentials(GetAccountId(*identifier),
                          SecureBlob(authorization->key().secret().begin(),
                                     authorization->key().secret().end()));
  credentials.set_key_data(authorization->key().data());

  if (!homedirs_->Exists(credentials.GetObfuscatedUsername(system_salt_))) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  reply.set_error(
      homedirs_->UpdateKeyset(credentials, &update_key_request->changes(),
                              update_key_request->authorization_signature()));
  if (reply.error() == CRYPTOHOME_ERROR_NOT_SET) {
    // Don't set the error if there wasn't one.
    reply.clear_error();
  }
  SendReply(context, reply);
}

gboolean Service::UpdateKeyEx(GArray* account_id,
                              GArray* authorization_request,
                              GArray* update_key_request,
                              DBusGMethodInvocation* context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<UpdateKeyRequest> request(new UpdateKeyRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  if (!request->ParseFromArray(update_key_request->data,
                               update_key_request->len))
    request.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(
      FROM_HERE,
      base::Bind(&Service::DoUpdateKeyEx, base::Unretained(this),
                 base::Owned(identifier.release()),
                 base::Owned(authorization.release()),
                 base::Owned(request.release()), base::Unretained(context)));
  return TRUE;
}

void Service::DoRemoveEx(AccountIdentifier* identifier,
                         DBusGMethodInvocation* context) {
  if (!identifier) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "Empty account_id.");
    return;
  }

  BaseReply reply;
  if (!homedirs_->Remove(identifier->account_id()))
    reply.set_error(CRYPTOHOME_ERROR_REMOVE_FAILED);
  else
    reply.clear_error();

  SendReply(context, reply);
}

gboolean Service::RemoveEx(GArray* account_id, DBusGMethodInvocation* context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(FROM_HERE, base::Bind(&Service::DoRemoveEx, base::Unretained(this),
                                 base::Owned(identifier.release()),
                                 base::Unretained(context)));

  return TRUE;
}

gboolean Service::RenameCryptohome(const GArray* account_id_from,
                                   const GArray* account_id_to,
                                   DBusGMethodInvocation* response) {
  std::unique_ptr<AccountIdentifier> id_from(new AccountIdentifier);
  std::unique_ptr<AccountIdentifier> id_to(new AccountIdentifier);
  if (!id_from->ParseFromArray(account_id_from->data, account_id_from->len)) {
    id_from.reset(NULL);
  }

  if (!id_to->ParseFromArray(account_id_to->data, account_id_to->len)) {
    id_to.reset(NULL);
  }

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(
      FROM_HERE,
      base::Bind(&Service::DoRenameCryptohome, base::Unretained(this),
                 base::Owned(id_from.release()), base::Owned(id_to.release()),
                 base::Unretained(response)));

  return TRUE;
}

void Service::DoRenameCryptohome(AccountIdentifier* id_from,
                                 AccountIdentifier* id_to,
                                 DBusGMethodInvocation* context) {
  if (!id_from || !id_to) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  scoped_refptr<cryptohome::Mount> mount =
      GetMountForUser(GetAccountId(*id_from));
  const bool is_mounted = mount.get() && mount->IsMounted();
  BaseReply reply;

  if (is_mounted) {
    LOG(ERROR) << "RenameCryptohome('" << GetAccountId(*id_from) << "','"
               << GetAccountId(*id_to)
               << "'): Unable to rename mounted cryptohome.";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
  } else if (!homedirs_) {
    LOG(ERROR) << "RenameCryptohome('" << GetAccountId(*id_from) << "','"
               << GetAccountId(*id_to) << "'): Homedirs not initialized.";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
  } else if (!homedirs_->Rename(GetAccountId(*id_from), GetAccountId(*id_to))) {
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  SendReply(context, reply);
}

gboolean Service::GetAccountDiskUsage(const GArray* account_id,
                                      DBusGMethodInvocation* response) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  if (!identifier->ParseFromArray(account_id->data, account_id->len)) {
    identifier.reset(NULL);
  }

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(FROM_HERE,
           base::Bind(&Service::DoGetAccountDiskUsage, base::Unretained(this),
                      base::Owned(identifier.release()),
                      base::Unretained(response)));
  return TRUE;
}

void Service::DoGetAccountDiskUsage(AccountIdentifier* identifier,
                                    DBusGMethodInvocation* context) {
  if (!identifier) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  BaseReply reply;
  reply.MutableExtension(GetAccountDiskUsageReply::reply)
      ->set_size(homedirs_->ComputeDiskUsage(GetAccountId(*identifier)));

  SendReply(context, reply);
}

gboolean Service::GetSystemSalt(GArray** OUT_salt, GError** error) {
  *OUT_salt = g_array_new(false, false, 1);
  g_array_append_vals(*OUT_salt, system_salt_.data(), system_salt_.size());
  return TRUE;
}

gboolean Service::GetSanitizedUsername(gchar* username,
                                       gchar** OUT_sanitized,
                                       GError** error) {
  // Credentials::GetObfuscatedUsername() returns an uppercase hex encoding,
  // while SanitizeUserName() returns a lowercase hex encoding. They should
  // return the same value, but login_manager is already relying on
  // SanitizeUserName() and that's the value that chrome should see.
  std::string sanitized = brillo::cryptohome::home::SanitizeUserName(username);
  if (sanitized.empty())
    return FALSE;
  *OUT_sanitized = g_strndup(sanitized.data(), sanitized.size());
  return TRUE;
}

gboolean Service::IsMounted(gboolean* OUT_is_mounted, GError** error) {
  // We consider "the cryptohome" to be mounted if any existing cryptohome is
  // mounted.
  *OUT_is_mounted = FALSE;
  base::AutoLock _lock(mounts_lock_);
  for (const auto& mount_pair : mounts_) {
    if (mount_pair.second->IsMounted()) {
      *OUT_is_mounted = TRUE;
      break;
    }
  }
  return TRUE;
}

gboolean Service::IsMountedForUser(gchar* userid,
                                   gboolean* OUT_is_mounted,
                                   gboolean* OUT_is_ephemeral_mount,
                                   GError** error) {
  scoped_refptr<cryptohome::Mount> mount = GetMountForUser(userid);
  *OUT_is_mounted = false;
  *OUT_is_ephemeral_mount = false;
  if (!mount.get())
    return TRUE;
  if (mount->IsNonEphemeralMounted()) {
    *OUT_is_mounted = true;
    *OUT_is_ephemeral_mount = false;
  } else if (mount->IsMounted()) {
    *OUT_is_mounted = true;
    *OUT_is_ephemeral_mount = true;
  }
  return TRUE;
}

void Service::DoUpdateTimestamp(scoped_refptr<cryptohome::Mount> mount) {
  mount->UpdateCurrentUserActivityTimestamp(0);
}

void Service::DoMount(scoped_refptr<cryptohome::Mount> mount,
                      const Credentials& credentials,
                      const Mount::MountArgs& mount_args,
                      base::WaitableEvent* event,
                      MountError* return_code,
                      bool* return_status) {
  *return_status = mount->MountCryptohome(credentials, mount_args, return_code);
  event->Signal();
}

gboolean Service::Mount(const gchar* userid,
                        const gchar* key,
                        gboolean create_if_missing,
                        gboolean ensure_ephemeral,
                        gint* OUT_error_code,
                        gboolean* OUT_result,
                        GError** error) {
  CleanUpHiddenMounts();

  // This is safe even if cryptohomed restarts during a multi-mount
  // session and a new mount is added because cleanup is not forced.
  // An existing process will keep the mount alive.  On the next
  // Unmount() it'll be forcibly cleaned up.  In the case that
  // cryptohomed crashes and misses the Unmount call, the stale
  // mountpoints should still be cleaned up on the next daemon
  // interaction.
  //
  // As we introduce multiple mounts, we can consider API changes to
  // make it clearer what the UI expectations are (AddMount, etc).
  if (mounts_.size() == 0)
    // This could run on every interaction to catch any unused mounts.
    CleanUpStaleMounts(false);

  Credentials credentials(userid, SecureBlob(key, key + strlen(key)));

  scoped_refptr<cryptohome::Mount> guest_mount = GetMountForUser(guest_user_);
  bool guest_mounted = guest_mount.get() && guest_mount->IsMounted();
  if (guest_mounted && !guest_mount->UnmountCryptohome()) {
    LOG(ERROR) << "Could not unmount cryptohome from Guest session";
    *OUT_error_code = MOUNT_ERROR_MOUNT_POINT_BUSY;
    *OUT_result = FALSE;
    return TRUE;
  }

  // Determine whether the mount should be ephemeral.
  bool is_ephemeral = false;
  MountError mount_error = MOUNT_ERROR_NONE;
  if (!GetShouldMountAsEphemeral(userid, ensure_ephemeral, create_if_missing,
                                 &is_ephemeral, &mount_error)) {
    *OUT_error_code = mount_error;
    *OUT_result = FALSE;
    return TRUE;
  }

  // If a cryptohome is mounted for the user already, reuse that mount unless
  // the |is_ephemeral| flag prevents it: When |is_ephemeral| is
  // |true|, a cryptohome backed by tmpfs is required. If the currently
  // mounted cryptohome is backed by a vault, it must be unmounted and
  // remounted with a tmpfs backend.
  scoped_refptr<cryptohome::Mount> user_mount = GetOrCreateMountForUser(userid);
  if (!user_mount) {
    LOG(ERROR) << "Could not initialize user mount.";
    *OUT_error_code = CRYPTOHOME_ERROR_MOUNT_FATAL;
    *OUT_result = FALSE;
    return TRUE;
  }
  if (is_ephemeral && user_mount->IsNonEphemeralMounted()) {
    // TODO(wad,ellyjones) Change this behavior to return failure even
    // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
    if (!user_mount->UnmountCryptohome()) {
      // The MountMap entry is kept since the Unmount failed.
      LOG(ERROR) << "Could not unmount vault before an ephemeral mount.";
      *OUT_error_code = MOUNT_ERROR_MOUNT_POINT_BUSY;
      *OUT_result = FALSE;
      return TRUE;
    }
  }

  // TODO(wad) A case we haven't handled is mount-over of a non-ephemeral user.

  // This is the case where there were 2 mount requests for a given user
  // without any intervening unmount requests. This should only be able to
  // happen if Chrome acts pathologically and re-requests a Mount.  If,
  // for instance, cryptohomed crashed, the MountMap would not contain the
  // entry.
  // TODO(wad) Can we get rid of this code path?

  if (user_mount->IsMounted()) {
    // Count this event to confirm the code path can be removed.
    ReportCrosEvent(kCryptohomeDoubleMount);

    // TODO(wad) This tests against the stored credentials, not the TPM.
    // If mounts are "repopulated", then a trip through the TPM would be needed.
    LOG(INFO) << "Mount exists. Rechecking credentials.";
    if (!user_mount->AreSameUser(
            credentials.GetObfuscatedUsername(system_salt_)) ||
        !user_mount->AreValid(credentials)) {
      // Need to take a trip through the TPM.
      if (!homedirs_->AreCredentialsValid(credentials)) {
        LOG(ERROR) << "Failed to reauthenticate against the existing mount!";
        // TODO(wad) Should we teardown all the mounts if this happens?
        // RemoveAllMounts();
        *OUT_error_code = MOUNT_ERROR_KEY_FAILURE;
        *OUT_result = FALSE;
        return TRUE;
      }
    }

    // As far as PKCS#11 initialization goes, we treat this as a brand new
    // mount request. InitializePkcs11() will detect and re-initialize if
    // necessary except if the mount point is ephemeral as there is no PKCS#11
    // data.
    InitializePkcs11(user_mount.get());
    *OUT_error_code = MOUNT_ERROR_NONE;
    *OUT_result = TRUE;
    return TRUE;
  }

  // Any non-guest mount attempt triggers InstallAttributes finalization.
  // The return value is ignored as it is possible we're pre-ownership.
  // The next login will assure finalization if possible.
  if (install_attrs_->status() == InstallAttributes::Status::kFirstInstall)
    install_attrs_->Finalize();

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = create_if_missing;
  mount_args.is_ephemeral = is_ephemeral;
  mount_args.create_as_ecryptfs = force_ecryptfs_;
  // TODO(kinaba): Currently Mount is not used for type of accounts that
  // we need to force dircrypto. Add an option when it becomes necessary.
  mount_args.force_dircrypto = false;

  MountError return_code = MOUNT_ERROR_NONE;
  bool return_status = false;

  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);

  PostTask(FROM_HERE,
           base::Bind(&Service::DoMount, base::Unretained(this),
                      base::RetainedRef(user_mount), std::cref(credentials),
                      std::cref(mount_args), base::Unretained(&event),
                      base::Unretained(&return_code),
                      base::Unretained(&return_status)));

  event.Wait();

  // Update the timestamp for old user detection in the background.
  PostTask(FROM_HERE,
           base::Bind(&Service::DoUpdateTimestamp, base::Unretained(this),
                      base::RetainedRef(user_mount)));

  user_mount->set_pkcs11_state(cryptohome::Mount::kUninitialized);
  if (return_status) {
    InitializePkcs11(user_mount.get());
  } else {
    RemoveMount(user_mount.get());
  }

  *OUT_error_code = return_code;
  *OUT_result = return_status;
  return TRUE;
}

void Service::DoMountEx(std::unique_ptr<AccountIdentifier> identifier,
                        std::unique_ptr<AuthorizationRequest> authorization,
                        std::unique_ptr<MountRequest> request,
                        DBusGMethodInvocation* context) {
  if (!identifier || !authorization || !request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  // Setup a reply for use during error handling.
  BaseReply reply;

  // Needed to pass along |recreated|
  MountReply* mount_reply = reply.MutableExtension(MountReply::reply);
  mount_reply->set_recreated(false);

  // At present, we only enforce non-empty email addresses.
  // In the future, we may wish to canonicalize if we don't move
  // to requiring a IdP-unique identifier.
  const std::string& account_id = GetAccountId(*identifier);
  if (account_id.empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  if (request->public_mount()) {
    std::string public_mount_passkey;
    if (!GetPublicMountPassKey(account_id, &public_mount_passkey)) {
      LOG(ERROR) << "Could not get public mount passkey.";
      reply.set_error(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
      SendReply(context, reply);
      return;
    }

    // Set the secret as the key for cryptohome authorization/creation.
    authorization->mutable_key()->set_secret(public_mount_passkey);
    if (request->has_create()) {
      request->mutable_create()->mutable_keys(0)->set_secret(
          public_mount_passkey);
    }
  }

  // An AuthorizationRequest key without a label will test against
  // all VaultKeysets of a compatible key().data().type().
  if (authorization->key().secret().empty() &&
      authorization->key().data().type() !=
          KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    SendInvalidArgsReply(context, "No key secret supplied");
    return;
  }

  if (request->has_create()) {
    if (request->create().copy_authorization_key()) {
      Key* auth_key = request->mutable_create()->add_keys();
      *auth_key = authorization->key();
      // Don't allow a key creation and mount if the key lacks
      // the privileges.
      if (!auth_key->data().privileges().mount()) {
        LOG(ERROR) << "Auth key denied";
        reply.set_error(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED);
        SendReply(context, reply);
        return;
      }
    }
    int keys_size = request->create().keys_size();
    if (keys_size == 0) {
      SendInvalidArgsReply(context, "CreateRequest supplied with no keys");
      return;
    } else if (keys_size > 1) {
      LOG(ERROR) << "MountEx: unimplemented CreateRequest with multiple keys";
      reply.set_error(CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
      SendReply(context, reply);
      return;
    } else {
      const Key key = request->create().keys(0);
      // TODO(wad) Ensure the labels are all unique.
      if (!key.has_data() || key.data().label().empty() ||
          (key.secret().empty() &&
           key.data().type() != KeyData::KEY_TYPE_CHALLENGE_RESPONSE)) {
        SendInvalidArgsReply(context,
                             "CreateRequest Keys are not fully specified");
        return;
      }
      if (KeyHasWrappedAuthorizationSecrets(key)) {
        SendInvalidArgsReply(context,
                             "KeyAuthorizationSecrets may not be wrapped");
        return;
      }
    }
  }

  // Determine whether the mount should be ephemeral.
  bool is_ephemeral = false;
  MountError mount_error = MOUNT_ERROR_NONE;
  if (!GetShouldMountAsEphemeral(account_id, request->require_ephemeral(),
                                 request->has_create(), &is_ephemeral,
                                 &mount_error)) {
    reply.set_error(MountErrorToCryptohomeError(mount_error));
    SendReply(context, reply);
    return;
  }

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = request->has_create();
  mount_args.is_ephemeral = is_ephemeral;
  mount_args.create_as_ecryptfs =
      force_ecryptfs_ ||
      (request->has_create() && request->create().force_ecryptfs());
  mount_args.to_migrate_from_ecryptfs = request->to_migrate_from_ecryptfs();
  // Force_ecryptfs_ wins.
  mount_args.force_dircrypto =
      !force_ecryptfs_ && request->force_dircrypto_if_available();
  mount_args.shadow_only = request->hidden_mount();

  // Process challenge-response credentials asynchronously.
  if (authorization->key().data().type() ==
      KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    DoChallengeResponseMountEx(std::move(identifier), std::move(authorization),
                               std::move(request), mount_args, context);
    return;
  }

  auto credentials = std::make_unique<Credentials>(
      account_id, SecureBlob(authorization->key().secret().begin(),
                             authorization->key().secret().end()));
  // Everything else can be the default.
  credentials->set_key_data(authorization->key().data());

  ContinueMountExWithCredentials(std::move(identifier),
                                 std::move(authorization), std::move(request),
                                 std::move(credentials), mount_args, context);
  LOG(INFO) << "Finished mount request process";
}

bool Service::InitForChallengeResponseAuth(CryptohomeErrorCode* error_code) {
  if (challenge_credentials_helper_) {
    // Already successfully initialized.
    return true;
  }

  if (!tpm_) {
    LOG(ERROR) << "Cannot do challenge-response authentication without TPM";
    *error_code = CRYPTOHOME_ERROR_MOUNT_FATAL;
    return false;
  }
  if (!tpm_init_->IsTpmReady()) {
    LOG(ERROR) << "TPM must be initialized in order to do challenge-response "
                  "authentication";
    *error_code = CRYPTOHOME_ERROR_MOUNT_FATAL;
    return false;
  }

  // Fail if the TPM is known to be vulnerable and we're not in a test image.
  const base::Optional<bool> is_srk_roca_vulnerable =
      tpm_->IsSrkRocaVulnerable();
  if (!is_srk_roca_vulnerable.has_value()) {
    LOG(ERROR) << "Cannot do challenge-response mount: Failed to check for "
                  "ROCA vulnerability";
    *error_code = CRYPTOHOME_ERROR_MOUNT_FATAL;
    return false;
  }
  if (is_srk_roca_vulnerable.value()) {
    if (!IsOsTestImage()) {
      LOG(ERROR)
          << "Cannot do challenge-response mount: TPM is ROCA vulnerable";
      *error_code = CRYPTOHOME_ERROR_TPM_UPDATE_REQUIRED;
      return false;
    }
    LOG(WARNING) << "TPM is ROCA vulnerable; ignoring this for "
                    "challenge-response mount due to running in test image";
  }

  // Lazily create the helper object that manages generation/decryption of
  // credentials for challenge-protected vaults.
  Blob delegate_blob, delegate_secret;
  bool has_reset_lock_permissions = false;
  if (!AttestationGetDelegateCredentials(&delegate_blob, &delegate_secret,
                                         &has_reset_lock_permissions)) {
    LOG(ERROR)
        << "Cannot do challenge-response authentication without TPM delegate";
    *error_code = CRYPTOHOME_ERROR_MOUNT_FATAL;
    return false;
  }
  default_challenge_credentials_helper_ =
      std::make_unique<ChallengeCredentialsHelperImpl>(tpm_, delegate_blob,
                                                       delegate_secret);
  challenge_credentials_helper_ = default_challenge_credentials_helper_.get();

  return true;
}

void Service::DoChallengeResponseCheckKeyEx(
    std::unique_ptr<AccountIdentifier> identifier,
    std::unique_ptr<AuthorizationRequest> authorization,
    DBusGMethodInvocation* context) {
  DCHECK_EQ(authorization->key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  BaseReply reply;

  CryptohomeErrorCode error_code = CRYPTOHOME_ERROR_NOT_SET;
  if (!InitForChallengeResponseAuth(&error_code)) {
    reply.set_error(error_code);
    SendReply(context, reply);
    return;
  }
  if (!authorization->has_key_delegate() ||
      !authorization->key_delegate().has_dbus_service_name()) {
    LOG(ERROR) << "Cannot do challenge-response authentication without key "
                  "delegate information";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
    SendReply(context, reply);
    return;
  }
  if (!authorization->key().data().challenge_response_key_size()) {
    LOG(ERROR) << "Missing challenge-response key information";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
    SendReply(context, reply);
    return;
  }
  if (authorization->key().data().challenge_response_key_size() > 1) {
    LOG(ERROR)
        << "Using multiple challenge-response keys at once is unsupported";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
    SendReply(context, reply);
    return;
  }

  // Begin from attempting a lightweight check that doesn't use the vault keyset
  // or heavy TPM operations, and therefore is faster than the full check and
  // also works in case the mount is ephemeral.
  TryLightweightChallengeResponseCheckKeyEx(std::move(identifier),
                                            std::move(authorization), context);
}

void Service::TryLightweightChallengeResponseCheckKeyEx(
    std::unique_ptr<AccountIdentifier> identifier,
    std::unique_ptr<AuthorizationRequest> authorization,
    DBusGMethodInvocation* context) {
  DCHECK_EQ(authorization->key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  DCHECK(challenge_credentials_helper_);

  const std::string& account_id = GetAccountId(*identifier);
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(account_id, system_salt_);

  std::unique_ptr<KeyChallengeService> key_challenge_service =
      key_challenge_service_factory_->New(
          system_dbus_connection_.Connect(),
          authorization->key_delegate().dbus_service_name());
  if (!key_challenge_service) {
    LOG(ERROR) << "Failed to create key challenge service";
    BaseReply reply;
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
    SendReply(context, reply);
    return;
  }

  base::Optional<KeyData> found_session_key_data;
  {
    base::AutoLock lock(mounts_lock_);
    for (const auto& mount_pair : mounts_) {
      const scoped_refptr<cryptohome::Mount>& mount = mount_pair.second;
      if (mount->AreSameUser(obfuscated_username) &&
          mount->GetCurrentUserSession() &&
          KeyMatchesForLightweightChallengeResponseCheck(
              authorization->key().data(), *mount->GetCurrentUserSession())) {
        found_session_key_data = mount->GetCurrentUserSession()->key_data();
        break;
      }
    }
  }
  if (!found_session_key_data) {
    // No matching user session found, so fall back to the full check.
    OnLightweightChallengeResponseCheckKeyExDone(std::move(identifier),
                                                 std::move(authorization),
                                                 context, /*success=*/false);
    return;
  }

  // Attempt the lightweight check against the found user session.
  challenge_credentials_helper_->VerifyKey(
      account_id, *found_session_key_data, std::move(key_challenge_service),
      base::BindOnce(&Service::OnLightweightChallengeResponseCheckKeyExDone,
                     base::Unretained(this), std::move(identifier),
                     std::move(authorization), base::Unretained(context)));
}

void Service::OnLightweightChallengeResponseCheckKeyExDone(
    std::unique_ptr<AccountIdentifier> identifier,
    std::unique_ptr<AuthorizationRequest> authorization,
    DBusGMethodInvocation* context,
    bool success) {
  if (!success) {
    DoFullChallengeResponseCheckKeyEx(std::move(identifier),
                                      std::move(authorization), context);
    return;
  }

  // Note that the LE credentials are not reset here, since we don't have the
  // full credentials after the lightweight check.
  SendReply(context, BaseReply());
}

void Service::DoFullChallengeResponseCheckKeyEx(
    std::unique_ptr<AccountIdentifier> identifier,
    std::unique_ptr<AuthorizationRequest> authorization,
    DBusGMethodInvocation* context) {
  DCHECK_EQ(authorization->key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  DCHECK(challenge_credentials_helper_);

  const std::string& account_id = GetAccountId(*identifier);
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(account_id, system_salt_);

  BaseReply reply;

  std::unique_ptr<KeyChallengeService> key_challenge_service =
      key_challenge_service_factory_->New(
          system_dbus_connection_.Connect(),
          authorization->key_delegate().dbus_service_name());
  if (!key_challenge_service) {
    LOG(ERROR) << "Failed to create key challenge service";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
    SendReply(context, reply);
    return;
  }

  if (!homedirs_->Exists(obfuscated_username)) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  std::unique_ptr<VaultKeyset> vault_keyset(homedirs_->GetVaultKeyset(
      obfuscated_username, authorization->key().data().label()));
  if (!vault_keyset) {
    LOG(ERROR) << "No existing challenge-response vault keyset found";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
    SendReply(context, reply);
    return;
  }
  challenge_credentials_helper_->Decrypt(
      account_id, authorization->key().data(),
      vault_keyset->serialized().signature_challenge_info(),
      std::move(key_challenge_service),
      base::Bind(&Service::OnFullChallengeResponseCheckKeyExDone,
                 base::Unretained(this), base::Unretained(context)));
}

void Service::OnFullChallengeResponseCheckKeyExDone(
    DBusGMethodInvocation* context, std::unique_ptr<Credentials> credentials) {
  if (!credentials) {
    LOG(ERROR) << "Key checking failed due to failure to obtain "
                  "challenge-response credentials";
    BaseReply reply;
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
    SendReply(context, reply);
    return;
  }

  // Entered the right creds, so reset LE credentials.
  homedirs_->ResetLECredentials(*credentials);

  SendReply(context, BaseReply());
}

void Service::DoChallengeResponseMountEx(
    std::unique_ptr<AccountIdentifier> identifier,
    std::unique_ptr<AuthorizationRequest> authorization,
    std::unique_ptr<MountRequest> request,
    const Mount::MountArgs& mount_args,
    DBusGMethodInvocation* context) {
  DCHECK_EQ(authorization->key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

  // Setup a reply for use during error handling.
  BaseReply reply;
  reply.MutableExtension(MountReply::reply)->set_recreated(false);

  CryptohomeErrorCode error_code = CRYPTOHOME_ERROR_NOT_SET;
  if (!InitForChallengeResponseAuth(&error_code)) {
    reply.set_error(error_code);
    SendReply(context, reply);
    return;
  }

  const std::string& account_id = GetAccountId(*identifier);
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(account_id, system_salt_);
  const KeyData key_data = authorization->key().data();

  if (!authorization->has_key_delegate() ||
      !authorization->key_delegate().has_dbus_service_name()) {
    LOG(ERROR) << "Cannot do challenge-response mount without key delegate "
                  "information";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
    SendReply(context, reply);
    return;
  }
  std::unique_ptr<KeyChallengeService> key_challenge_service =
      key_challenge_service_factory_->New(
          system_dbus_connection_.Connect(),
          authorization->key_delegate().dbus_service_name());
  if (!key_challenge_service) {
    LOG(ERROR) << "Failed to create key challenge service";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
    SendReply(context, reply);
    return;
  }

  if (!homedirs_->Exists(obfuscated_username) &&
      !mount_args.create_if_missing) {
    LOG(ERROR) << "Cannot do challenge-response mount. Account not found.";
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  std::unique_ptr<VaultKeyset> vault_keyset(homedirs_->GetVaultKeyset(
      obfuscated_username, authorization->key().data().label()));
  const bool use_existing_credentials =
      vault_keyset && !mount_args.is_ephemeral;
  if (use_existing_credentials) {
    challenge_credentials_helper_->Decrypt(
        account_id, key_data,
        vault_keyset->serialized().signature_challenge_info(),
        std::move(key_challenge_service),
        base::BindOnce(&Service::OnChallengeResponseMountCredentialsObtained,
                       base::Unretained(this),
                       base::Passed(std::move(identifier)),
                       base::Passed(std::move(authorization)),
                       base::Passed(std::move(request)), mount_args,
                       base::Unretained(context)));
  } else {
    if (!mount_args.create_if_missing) {
      LOG(ERROR) << "No existing challenge-response vault keyset found";
      reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
      SendReply(context, reply);
      return;
    }
    std::vector<std::map<uint32_t, Blob>> pcr_restrictions;
    GetChallengeCredentialsPcrRestrictions(obfuscated_username,
                                           &pcr_restrictions);
    challenge_credentials_helper_->GenerateNew(
        account_id, key_data, pcr_restrictions,
        std::move(key_challenge_service),
        base::BindOnce(&Service::OnChallengeResponseMountCredentialsObtained,
                       base::Unretained(this),
                       base::Passed(std::move(identifier)),
                       base::Passed(std::move(authorization)),
                       base::Passed(std::move(request)), mount_args,
                       base::Unretained(context)));
  }
}

void Service::OnChallengeResponseMountCredentialsObtained(
    std::unique_ptr<AccountIdentifier> identifier,
    std::unique_ptr<AuthorizationRequest> authorization,
    std::unique_ptr<MountRequest> request,
    const Mount::MountArgs& mount_args,
    DBusGMethodInvocation* context,
    std::unique_ptr<Credentials> credentials) {
  DCHECK_EQ(authorization->key().data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  if (!credentials) {
    LOG(ERROR) << "Could not mount due to failure to obtain challenge-response "
                  "credentials";
    BaseReply reply;
    reply.MutableExtension(MountReply::reply)->set_recreated(false);
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
    SendReply(context, reply);
    return;
  }
  DCHECK_EQ(credentials->key_data().type(),
            KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  ContinueMountExWithCredentials(std::move(identifier),
                                 std::move(authorization), std::move(request),
                                 std::move(credentials), mount_args, context);
}

void Service::ContinueMountExWithCredentials(
    std::unique_ptr<AccountIdentifier> identifier,
    std::unique_ptr<AuthorizationRequest> authorization,
    std::unique_ptr<MountRequest> request,
    std::unique_ptr<Credentials> credentials,
    const Mount::MountArgs& mount_args,
    DBusGMethodInvocation* context) {
  if (!CleanUpHiddenMounts()) {
    LOG(WARNING) << "Failed to clean up hidden mounts";
  }

  // Setup a reply for use during error handling.
  BaseReply reply;

  // Needed to pass along |recreated|
  MountReply* mount_reply = reply.MutableExtension(MountReply::reply);
  mount_reply->set_recreated(false);

  // See ::Mount for detailed commentary.
  bool other_mounts_active = true;
  if (mounts_.size() == 0)
    other_mounts_active = CleanUpStaleMounts(false);

  if (!request->has_create() &&
      !homedirs_->Exists(credentials->GetObfuscatedUsername(system_salt_))) {
    LOG(ERROR) << "Account not found when mounting with credentials.";
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  // Provide an authoritative filesystem-sanitized username.
  mount_reply->set_sanitized_username(
      brillo::cryptohome::home::SanitizeUserName(GetAccountId(*identifier)));

  // While it would be cleaner to implement the privilege enforcement
  // here, that can only be done if a label was supplied.  If a wildcard
  // was supplied, then we can only perform the enforcement after the
  // matching key is identified.
  //
  // See Mount::MountCryptohome for privilege checking.

  scoped_refptr<cryptohome::Mount> guest_mount = GetMountForUser(guest_user_);
  bool guest_mounted = guest_mount.get() && guest_mount->IsMounted();
  // TODO(wad,ellyjones) Change this behavior to return failure even
  // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
  if (guest_mounted && !guest_mount->UnmountCryptohome()) {
    LOG(ERROR) << "Could not unmount cryptohome from Guest session";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    SendReply(context, reply);
    return;
  }

  scoped_refptr<cryptohome::Mount> user_mount =
      GetOrCreateMountForUser(GetAccountId(*identifier));
  if (!user_mount) {
    LOG(ERROR) << "Could not initialize user mount.";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
    SendReply(context, reply);
    return;
  }

  if (request->hidden_mount() && user_mount->IsMounted()) {
    LOG(ERROR) << "Hidden mount requested, but mount already exists.";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    SendReply(context, reply);
    return;
  }

  // For public mount, don't proceed if there is any existing mount or stale
  // mount. Exceptionally, it is normal and ok to have a failed previous mount
  // attempt for the same user.
  const bool only_self_unmounted_attempt =
      mounts_.size() == 1 && !user_mount->IsMounted();
  if (request->public_mount() && other_mounts_active &&
      !only_self_unmounted_attempt) {
    LOG(ERROR) << "Public mount requested with other mounts active.";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    SendReply(context, reply);
    return;
  }

  // Don't overlay an ephemeral mount over a file-backed one.
  if (mount_args.is_ephemeral && user_mount->IsNonEphemeralMounted()) {
    // TODO(wad,ellyjones) Change this behavior to return failure even
    // on a succesful unmount to tell chrome MOUNT_ERROR_NEEDS_RESTART.
    if (!user_mount->UnmountCryptohome()) {
      LOG(ERROR) << "Could not unmount vault before an ephemeral mount.";
      reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
      SendReply(context, reply);
      return;
    }
  }

  if (user_mount->IsMounted()) {
    // Attempt a short-circuited credential test.
    if (user_mount->AreSameUser(
            credentials->GetObfuscatedUsername(system_salt_)) &&
        user_mount->AreValid(*credentials)) {
      SendReply(context, reply);
      homedirs_->ResetLECredentials(*credentials);
      return;
    }
    // If the Mount has invalid credentials (repopulated from system state)
    // this will ensure a user can still sign-in with the right ones.
    // TODO(wad) Should we unmount on a failed re-mount attempt?
    if (!user_mount->AreValid(*credentials) &&
        !homedirs_->AreCredentialsValid(*credentials)) {
      LOG(ERROR) << "Credentials are invalid";
      reply.set_error(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
    } else {
      homedirs_->ResetLECredentials(*credentials);
    }
    SendReply(context, reply);
    return;
  }

  // See Mount for a relevant comment.
  if (install_attrs_->status() == InstallAttributes::Status::kFirstInstall) {
    install_attrs_->Finalize();
  }

  // As per the other timers, this really only tracks time spent in
  // MountCryptohome() not in the other areas prior.
  ReportTimerStart(kMountExTimer);
  MountError code = MOUNT_ERROR_NONE;
  bool status = user_mount->MountCryptohome(*credentials, mount_args, &code);
  user_mount->set_pkcs11_state(cryptohome::Mount::kUninitialized);

  // Mark the timer as done.
  ReportTimerStop(kMountExTimer);
  if (!status) {
    LOG(ERROR) << "Failed to mount cryptohome, error = " << code;
    reply.set_error(MountErrorToCryptohomeError(code));
    ResetDictionaryAttackMitigation();
  }
  if (code == MOUNT_ERROR_RECREATED) {
    mount_reply->set_recreated(true);
  }
  if (status) {
    homedirs_->ResetLECredentials(*credentials);
  }

  SendReply(context, reply);

  if (!request->hidden_mount()) {
    // Update user activity timestamp to be able to detect old users.
    // This action is not mandatory, so we perform it after
    // CryptohomeMount() returns, in background.
    user_mount->UpdateCurrentUserActivityTimestamp(0);
    // Time to push the task for PKCS#11 initialization.
    // TODO(wad) This call will PostTask back to the same thread. It is safe,
    //           but it seems pointless.
    InitializePkcs11(user_mount.get());
  }
}

void Service::GetChallengeCredentialsPcrRestrictions(
    const std::string& obfuscated_username,
    std::vector<std::map<uint32_t, Blob>>* pcr_restrictions) {
  {
    std::map<uint32_t, Blob> pcrs_1;
    for (const auto& pcr :
         tpm_->GetPcrMap(obfuscated_username, false /* use_extended_pcr */)) {
      pcrs_1[pcr.first] = BlobFromString(pcr.second);
    }
    pcr_restrictions->push_back(pcrs_1);
  }

  {
    std::map<uint32_t, Blob> pcrs_2;
    for (const auto& pcr :
         tpm_->GetPcrMap(obfuscated_username, true /* use_extended_pcr */)) {
      pcrs_2[pcr.first] = BlobFromString(pcr.second);
    }
    pcr_restrictions->push_back(pcrs_2);
  }
}

gboolean Service::MountEx(const GArray* account_id,
                          const GArray* authorization_request,
                          const GArray* mount_request,
                          DBusGMethodInvocation* context) {
  LOG(INFO) << "Received a mount request.";

  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<AuthorizationRequest> authorization(new AuthorizationRequest);
  std::unique_ptr<MountRequest> request(new MountRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len))
    identifier.reset(NULL);
  if (!authorization->ParseFromArray(authorization_request->data,
                                     authorization_request->len))
    authorization.reset(NULL);
  if (!request->ParseFromArray(mount_request->data, mount_request->len))
    request.reset(NULL);

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(FROM_HERE, base::Bind(&Service::DoMountEx, base::Unretained(this),
                                 base::Passed(std::move(identifier)),
                                 base::Passed(std::move(authorization)),
                                 base::Passed(std::move(request)),
                                 base::Unretained(context)));
  return TRUE;
}

void Service::SendDircryptoMigrationProgressSignal(
    DircryptoMigrationStatus status,
    uint64_t current_bytes,
    uint64_t total_bytes) {
  event_source_.AddEvent(std::make_unique<DircryptoMigrationProgress>(
      status, current_bytes, total_bytes));
}

void Service::DoMountGuestEx(scoped_refptr<cryptohome::Mount> guest_mount,
                             std::unique_ptr<MountGuestRequest> request_pb,
                             DBusGMethodInvocation* context) {
  if (!request_pb) {
    SendInvalidArgsReply(context, "Bad MountGuestRequest");
    return;
  }

  BaseReply reply;
  // As per the other timers, this really only tracks time spent in
  // MountGuestCryptohome() not in the other areas prior.
  ReportTimerStart(kMountGuestExTimer);
  bool success = guest_mount->MountGuestCryptohome();
  // Mark the timer as done.
  ReportTimerStop(kMountGuestExTimer);

  if (!success)
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
  else
    reply.clear_error();

  SendReply(context, reply);
}

gboolean Service::MountGuestEx(GArray* request,
                               DBusGMethodInvocation* context) {
  auto request_pb = std::make_unique<MountGuestRequest>();
  if (!request_pb->ParseFromArray(request->data, request->len))
    request_pb.reset(nullptr);

  if (mounts_.size() != 0)
    LOG(WARNING) << "Guest mount requested with other mounts active.";
  // Rather than make it safe to check the size, then clean up, just always
  // clean up.
  bool ok = RemoveAllMounts(true);
  // Create a ref-counted guest mount for async use and then throw it away.
  scoped_refptr<cryptohome::Mount> guest_mount =
      GetOrCreateMountForUser(guest_user_);

  BaseReply reply;
  if (!ok) {
    LOG(ERROR) << "Could not unmount cryptohomes for Guest use";
    if (!RemoveMountForUser(guest_user_)) {
      LOG(ERROR) << "Unexpectedly cannot drop unused Guest mount from map.";
    }
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
    return TRUE;
  }
  if (!guest_mount) {
    LOG(ERROR) << "Could not initialize guest mount.";
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
    return TRUE;
  }

  PostTask(FROM_HERE,
           base::Bind(&Service::DoMountGuestEx, base::Unretained(this),
                      guest_mount, base::Passed(std::move(request_pb)),
                      base::Unretained(context)));
  return TRUE;
}

// Unmount all mounted cryptohomes.
gboolean Service::Unmount(gboolean* OUT_result, GError** error) {
  *OUT_result = RemoveAllMounts(true);
  // If there are any unexpected mounts lingering from a crash/restart,
  // clean them up now.
  CleanUpStaleMounts(true);
  return TRUE;
}

void Service::DoUnmountEx(std::unique_ptr<UnmountRequest> request_pb,
                          DBusGMethodInvocation* context) {
  if (!request_pb) {
    SendInvalidArgsReply(context, "Bad UnmountRequest");
    return;
  }

  BaseReply reply;
  if (!RemoveAllMounts(true))
    reply.set_error(CRYPTOHOME_ERROR_MOUNT_FATAL);
  else
    reply.clear_error();

  // If there are any unexpected mounts lingering from a crash/restart,
  // clean them up now.
  CleanUpStaleMounts(true);

  SendReply(context, reply);

  // TODO(chromium:1109147): Remove this INFO log after we solve this issue.
  LOG(INFO) << "Finished unmount request process";
}

gboolean Service::UnmountEx(GArray* request, DBusGMethodInvocation* context) {
  // TODO(chromium:1109147): Remove this INFO log after we solve this issue.
  LOG(INFO) << "Received an unmount request.";
  auto request_pb = std::make_unique<UnmountRequest>();
  if (!request_pb->ParseFromArray(request->data, request->len))
    request_pb.reset(nullptr);

  mount_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&Service::DoUnmountEx, base::Unretained(this),
                            base::Passed(std::move(request_pb)),
                            base::Unretained(context)));
  return TRUE;
}

gboolean Service::UpdateCurrentUserActivityTimestamp(gint time_shift_sec,
                                                     GError** error) {
  base::AutoLock _lock(mounts_lock_);
  for (const auto& mount_pair : mounts_) {
    mount_pair.second->UpdateCurrentUserActivityTimestamp(time_shift_sec);
  }
  return TRUE;
}

gboolean Service::TpmIsReady(gboolean* OUT_ready, GError** error) {
  *OUT_ready = tpm_init_->IsTpmReady();
  return TRUE;
}

gboolean Service::TpmIsEnabled(gboolean* OUT_enabled, GError** error) {
  *OUT_enabled = tpm_init_->IsTpmEnabled();
  return TRUE;
}

gboolean Service::TpmGetPassword(gchar** OUT_password, GError** error) {
  SecureBlob password;
  if (!tpm_init_->GetTpmPassword(&password)) {
    *OUT_password = NULL;
    return TRUE;
  }
  // Convert to UTF-8 for sending over DBus. In case the original string
  // contained only ASCII characters, the result will be identical to the
  // original password.
  SecureBlob utf8_password(
      base::SysWideToUTF8(std::wstring(password.begin(), password.end())));
  // Make sure we copy and NULL-terminate the entire UTF-8 string, even if
  // there are 00 bytes in the middle of it. strndup/g_strndup would have
  // stopped at the first 00. Can still be stripped later by DBus code, though.
  size_t ret_size = utf8_password.size();
  gchar* ret_str = g_new(gchar, ret_size + 1);
  if (ret_str) {
    memcpy(ret_str, utf8_password.char_data(), ret_size);
    ret_str[ret_size] = 0;
  }
  *OUT_password = ret_str;
  return TRUE;
}

gboolean Service::TpmIsOwned(gboolean* OUT_owned, GError** error) {
  *OUT_owned = tpm_init_->IsTpmOwned();
  return TRUE;
}

gboolean Service::TpmIsBeingOwned(gboolean* OUT_owning, GError** error) {
  ReportDeprecatedApiCalled(DeprecatedApiEvent::kTpmIsBeingOwned);
  *OUT_owning = tpm_init_->IsTpmBeingOwned();
  return TRUE;
}

gboolean Service::TpmCanAttemptOwnership(GError** error) {
  if (!tpm_init_->OwnershipRequested()) {
    ReportTimerStart(kTpmTakeOwnershipTimer);
    tpm_init_->AsyncTakeOwnership();
  }
  return TRUE;
}

gboolean Service::TpmClearStoredPassword(GError** error) {
  tpm_init_->ClearStoredTpmPassword();
  return TRUE;
}

gboolean Service::TpmGetVersionStructured(guint32* OUT_family,
                                          guint64* OUT_spec_level,
                                          guint32* OUT_manufacturer,
                                          guint32* OUT_tpm_model,
                                          guint64* OUT_firmware_version,
                                          gchar** OUT_vendor_specific,
                                          GError** error) {
  cryptohome::Tpm::TpmVersionInfo version_info;
  if (!tpm_init_->GetVersion(&version_info)) {
    LOG(ERROR) << "Could not get TPM version information.";
    *OUT_family = 0;
    *OUT_spec_level = 0;
    *OUT_manufacturer = 0;
    *OUT_tpm_model = 0;
    *OUT_firmware_version = 0;
    *OUT_vendor_specific = nullptr;
    return FALSE;
  }

  *OUT_family = version_info.family;
  *OUT_spec_level = version_info.spec_level;
  *OUT_manufacturer = version_info.manufacturer;
  *OUT_tpm_model = version_info.tpm_model;
  *OUT_firmware_version = version_info.firmware_version;
  std::string vendor_specific_hex = base::HexEncode(
      version_info.vendor_specific.data(), version_info.vendor_specific.size());
  *OUT_vendor_specific = g_strdup(vendor_specific_hex.c_str());

  return TRUE;
}

// Returns true if all Pkcs11 tokens are ready.
gboolean Service::Pkcs11IsTpmTokenReady(gboolean* OUT_ready, GError** error) {
  *OUT_ready = TRUE;
  base::AutoLock _lock(mounts_lock_);
  for (const auto& mount_pair : mounts_) {
    cryptohome::Mount* mount = mount_pair.second.get();
    bool ok = (mount->pkcs11_state() == cryptohome::Mount::kIsInitialized);
    *OUT_ready = *OUT_ready && ok;
  }
  return TRUE;
}

gboolean Service::Pkcs11GetTpmTokenInfo(gchar** OUT_label,
                                        gchar** OUT_user_pin,
                                        gint* OUT_slot,
                                        GError** error) {
  std::string label, pin;
  pkcs11_init_->GetTpmTokenInfo(&label, &pin);
  *OUT_label = g_strdup(reinterpret_cast<const gchar*>(label.c_str()));
  *OUT_user_pin = g_strdup(reinterpret_cast<const gchar*>(pin.c_str()));

  *OUT_slot = -1;
  CK_SLOT_ID slot;
  if (pkcs11_init_->GetTpmTokenSlotForPath(FilePath(chaps::kSystemTokenPath),
                                           &slot))
    *OUT_slot = slot;
  return TRUE;
}

gboolean Service::Pkcs11GetTpmTokenInfoForUser(gchar* IN_username,
                                               gchar** OUT_label,
                                               gchar** OUT_user_pin,
                                               gint* OUT_slot,
                                               GError** error) {
  const std::string username = reinterpret_cast<const char*>(IN_username);

  std::string label, pin;
  pkcs11_init_->GetTpmTokenInfoForUser(username, &label, &pin);
  *OUT_label = g_strdup(reinterpret_cast<const gchar*>(label.c_str()));
  *OUT_user_pin = g_strdup(reinterpret_cast<const gchar*>(pin.c_str()));

  *OUT_slot = -1;
  CK_SLOT_ID slot;
  FilePath token_path = homedirs_->GetChapsTokenDir(username);
  if (pkcs11_init_->GetTpmTokenSlotForPath(token_path, &slot))
    *OUT_slot = slot;
  return TRUE;
}

gboolean Service::Pkcs11Terminate(gchar* username, GError** error) {
  base::AutoLock _lock(mounts_lock_);
  for (const auto& mount_pair : mounts_)
    mount_pair.second->RemovePkcs11Token();
  return TRUE;
}

gboolean Service::InstallAttributesGet(gchar* name,
                                       GArray** OUT_value,
                                       gboolean* OUT_successful,
                                       GError** error) {
  brillo::Blob value;
  *OUT_successful = install_attrs_->Get(name, &value);
  // We must set the GArray now because if we return without setting it,
  // dbus-glib loops forever.
  *OUT_value = g_array_new(false, false, sizeof(value.front()));
  if (!(*OUT_value)) {
    return FALSE;
  }
  if (*OUT_successful) {
    g_array_append_vals(*OUT_value, value.data(), value.size());
  }
  return TRUE;
}

gboolean Service::InstallAttributesSet(gchar* name,
                                       GArray* value,
                                       gboolean* OUT_successful,
                                       GError** error) {
  // Convert from GArray to vector
  brillo::Blob value_blob;
  value_blob.assign(value->data, value->data + value->len);
  *OUT_successful = install_attrs_->Set(name, value_blob);
  return TRUE;
}

gboolean Service::InstallAttributesFinalize(gboolean* OUT_finalized,
                                            GError** error) {
  *OUT_finalized = install_attrs_->Finalize();

  // Check if the machine is enterprise owned and report this to mount_.
  DetectEnterpriseOwnership();

  return TRUE;
}

gboolean Service::InstallAttributesCount(gint* OUT_count, GError** error) {
  // TODO(wad) for all of these functions return error on uninit.
  // Follow the CHROMEOS_LOGIN_ERROR quark example in brillo/dbus/
  *OUT_count = install_attrs_->Count();
  return TRUE;
}

gboolean Service::InstallAttributesIsReady(gboolean* OUT_ready,
                                           GError** error) {
  *OUT_ready =
      (install_attrs_->status() != InstallAttributes::Status::kUnknown &&
       install_attrs_->status() != InstallAttributes::Status::kTpmNotOwned);
  return TRUE;
}

gboolean Service::InstallAttributesIsSecure(gboolean* OUT_is_secure,
                                            GError** error) {
  *OUT_is_secure = (install_attrs_->is_secure() == true);
  return TRUE;
}

gboolean Service::InstallAttributesIsInvalid(gboolean* OUT_is_invalid,
                                             GError** error) {
  // Is true after a failed init or prior to Init().
  *OUT_is_invalid =
      (install_attrs_->status() == InstallAttributes::Status::kInvalid);
  return TRUE;
}

gboolean Service::InstallAttributesIsFirstInstall(
    gboolean* OUT_is_first_install, GError** error) {
  *OUT_is_first_install =
      (install_attrs_->status() == InstallAttributes::Status::kFirstInstall);
  return TRUE;
}

void Service::DoSignBootLockbox(const brillo::Blob& request,
                                DBusGMethodInvocation* context) {
  ReportDeprecatedApiCalled(DeprecatedApiEvent::kSignBootLockbox);

  SignBootLockboxRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size()) ||
      !request_pb.has_data()) {
    SendInvalidArgsReply(context, "Bad SignBootLockboxRequest");
    return;
  }
  BaseReply reply;
  SecureBlob signature;
  if (!boot_lockbox_->Sign(brillo::BlobFromString(request_pb.data()),
                           &signature)) {
    reply.set_error(CRYPTOHOME_ERROR_LOCKBOX_CANNOT_SIGN);
  } else {
    reply.MutableExtension(SignBootLockboxReply::reply)
        ->set_signature(signature.to_string());
  }
  SendReply(context, reply);
}

gboolean Service::SignBootLockbox(const GArray* request,
                                  DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(&Service::DoSignBootLockbox, base::Unretained(this),
                      brillo::Blob(request->data, request->data + request->len),
                      base::Unretained(context)));
  return TRUE;
}

void Service::DoVerifyBootLockbox(const brillo::Blob& request,
                                  DBusGMethodInvocation* context) {
  ReportDeprecatedApiCalled(DeprecatedApiEvent::kVerifyBootLockbox);

  VerifyBootLockboxRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size()) ||
      !request_pb.has_data() || !request_pb.has_signature()) {
    SendInvalidArgsReply(context, "Bad VerifyBootLockboxRequest");
    return;
  }
  BaseReply reply;
  if (!boot_lockbox_->Verify(brillo::BlobFromString(request_pb.data()),
                             SecureBlob(request_pb.signature()))) {
    reply.set_error(CRYPTOHOME_ERROR_LOCKBOX_SIGNATURE_INVALID);
  }
  SendReply(context, reply);
}

gboolean Service::VerifyBootLockbox(const GArray* request,
                                    DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(&Service::DoVerifyBootLockbox, base::Unretained(this),
                      brillo::Blob(request->data, request->data + request->len),
                      base::Unretained(context)));
  return TRUE;
}

void Service::DoFinalizeBootLockbox(const brillo::Blob& request,
                                    DBusGMethodInvocation* context) {
  ReportDeprecatedApiCalled(DeprecatedApiEvent::kFinalizeBootLockbox);
  FinalizeBootLockboxRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad FinalizeBootLockboxRequest");
    return;
  }
  BaseReply reply;
  if (!boot_lockbox_->FinalizeBoot()) {
    reply.set_error(CRYPTOHOME_ERROR_TPM_COMM_ERROR);
  }
  SendReply(context, reply);
}

gboolean Service::FinalizeBootLockbox(const GArray* request,
                                      DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(&Service::DoFinalizeBootLockbox, base::Unretained(this),
                      brillo::Blob(request->data, request->data + request->len),
                      base::Unretained(context)));
  return TRUE;
}

void Service::DoGetBootAttribute(const brillo::Blob& request,
                                 DBusGMethodInvocation* context) {
  ReportDeprecatedApiCalled(DeprecatedApiEvent::kGetBootAttribute);

  GetBootAttributeRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad GetBootAttributeRequest");
    return;
  }
  BaseReply reply;
  std::string value;
  if (!boot_attributes_->Get(request_pb.name(), &value)) {
    reply.set_error(CRYPTOHOME_ERROR_BOOT_ATTRIBUTE_NOT_FOUND);
  } else {
    reply.MutableExtension(GetBootAttributeReply::reply)->set_value(value);
  }
  SendReply(context, reply);
}

gboolean Service::GetBootAttribute(const GArray* request,
                                   DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(&Service::DoGetBootAttribute, base::Unretained(this),
                      brillo::Blob(request->data, request->data + request->len),
                      base::Unretained(context)));
  return TRUE;
}

void Service::DoSetBootAttribute(const brillo::Blob& request,
                                 DBusGMethodInvocation* context) {
  ReportDeprecatedApiCalled(DeprecatedApiEvent::kSetBootAttribute);

  SetBootAttributeRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad SetBootAttributeRequest");
    return;
  }
  BaseReply reply;
  boot_attributes_->Set(request_pb.name(), request_pb.value());
  SendReply(context, reply);
}

gboolean Service::SetBootAttribute(const GArray* request,
                                   DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(&Service::DoSetBootAttribute, base::Unretained(this),
                      brillo::Blob(request->data, request->data + request->len),
                      base::Unretained(context)));
  return TRUE;
}

void Service::DoFlushAndSignBootAttributes(const brillo::Blob& request,
                                           DBusGMethodInvocation* context) {
  ReportDeprecatedApiCalled(DeprecatedApiEvent::kFlushAndSignBootAttributes);

  FlushAndSignBootAttributesRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad FlushAndSignBootAttributesRequest");
    return;
  }
  BaseReply reply;
  if (!boot_attributes_->FlushAndSign()) {
    reply.set_error(CRYPTOHOME_ERROR_BOOT_ATTRIBUTES_CANNOT_SIGN);
  }
  SendReply(context, reply);
}

gboolean Service::FlushAndSignBootAttributes(const GArray* request,
                                             DBusGMethodInvocation* context) {
  PostTask(
      FROM_HERE,
      base::Bind(&Service::DoFlushAndSignBootAttributes, base::Unretained(this),
                 brillo::Blob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoGetLoginStatus(const brillo::SecureBlob& request,
                               DBusGMethodInvocation* context) {
  GetLoginStatusRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad GetLoginStatusRequest");
    return;
  }
  BaseReply reply;
  std::string owner;
  reply.MutableExtension(GetLoginStatusReply::reply)
      ->set_owner_user_exists(homedirs_->GetPlainOwner(&owner));
  reply.MutableExtension(GetLoginStatusReply::reply)
      ->set_boot_lockbox_finalized(boot_lockbox_->IsFinalized());
  SendReply(context, reply);
}

gboolean Service::GetLoginStatus(const GArray* request,
                                 DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(&Service::DoGetLoginStatus, base::Unretained(this),
                      SecureBlob(request->data, request->data + request->len),
                      base::Unretained(context)));
  return TRUE;
}

void Service::DoTpmAttestationGetEnrollmentPreparationsEx(
    const brillo::Blob& request, DBusGMethodInvocation* context) {
  AttestationGetEnrollmentPreparationsRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context,
                         "Bad AttestationGetEnrollmentPreparationsRequest");
    return;
  }
  BaseReply reply;
  AttestationGetEnrollmentPreparationsReply* extension =
      reply.MutableExtension(AttestationGetEnrollmentPreparationsReply::reply);
  if (!AttestationGetEnrollmentPreparations(request_pb, extension)) {
    reply.set_error(CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);
  }
  SendReply(context, reply);
}

gboolean Service::TpmAttestationGetEnrollmentPreparationsEx(
    const GArray* request, DBusGMethodInvocation* context) {
  mount_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&Service::DoTpmAttestationGetEnrollmentPreparationsEx,
                 base::Unretained(this),
                 brillo::Blob(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoGetTpmStatus(const brillo::SecureBlob& request,
                             DBusGMethodInvocation* context) {
  GetTpmStatusRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad GetTpmStatusRequest");
    return;
  }
  BaseReply reply;
  GetTpmStatusReply* extension =
      reply.MutableExtension(GetTpmStatusReply::reply);
  extension->set_enabled(tpm_init_->IsTpmEnabled());
  extension->set_owned(tpm_init_->IsTpmOwned());
  SecureBlob owner_password;
  if (tpm_init_->GetTpmPassword(&owner_password)) {
    extension->set_initialized(false);
    extension->set_owner_password(owner_password.to_string());
  } else {
    // Initialized is true only when the TPM is owned and the owner password has
    // already been destroyed.
    extension->set_initialized(extension->owned());
  }
  int counter;
  int threshold;
  bool lockout;
  int seconds_remaining;
  if (tpm_->GetDictionaryAttackInfo(&counter, &threshold, &lockout,
                                    &seconds_remaining)) {
    extension->set_dictionary_attack_counter(counter);
    extension->set_dictionary_attack_threshold(threshold);
    extension->set_dictionary_attack_lockout_in_effect(lockout);
    extension->set_dictionary_attack_lockout_seconds_remaining(
        seconds_remaining);
  }
  extension->set_install_lockbox_finalized(
      extension->owned() &&
      install_attrs_->status() == InstallAttributes::Status::kValid);
  extension->set_boot_lockbox_finalized(boot_lockbox_->IsFinalized());
  extension->set_is_locked_to_single_user(
      base::PathExists(base::FilePath(kLockedToSingleUserFile)));
  AttestationGetTpmStatus(extension);
  SendReply(context, reply);
}

gboolean Service::GetTpmStatus(const GArray* request,
                               DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(&Service::DoGetTpmStatus, base::Unretained(this),
                      SecureBlob(request->data, request->data + request->len),
                      base::Unretained(context)));
  return TRUE;
}

void Service::OnStartFingerprintAuthSessionDone(DBusGMethodInvocation* context,
                                                bool success) {
  VLOG(1) << "Start fingerprint auth session result: " << success;
  BaseReply reply;
  if (!success)
    reply.set_error(CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
  SendReply(context, reply);
}

void Service::DoStartFingerprintAuthSession(
    std::unique_ptr<AccountIdentifier> identifier,
    std::unique_ptr<StartFingerprintAuthSessionRequest> request,
    DBusGMethodInvocation* context) {
  if (!identifier || !request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }
  if (GetAccountId(*identifier).empty()) {
    SendInvalidArgsReply(context, "No email supplied");
    return;
  }

  BaseReply reply;
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(GetAccountId(*identifier), system_salt_);
  if (!homedirs_->Exists(obfuscated_username)) {
    reply.set_error(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
    SendReply(context, reply);
    return;
  }

  fingerprint_manager_->StartAuthSessionAsyncForUser(
      obfuscated_username,
      base::Bind(&Service::OnStartFingerprintAuthSessionDone,
                 base::Unretained(this), context));
}

gboolean Service::StartFingerprintAuthSession(
    const GArray* account_id,
    const GArray* start_auth_session_request,
    DBusGMethodInvocation* context) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  std::unique_ptr<StartFingerprintAuthSessionRequest> request(
      new StartFingerprintAuthSessionRequest);

  // On parsing failure, pass along a NULL.
  if (!identifier->ParseFromArray(account_id->data, account_id->len)) {
    identifier.reset(NULL);
  }
  if (!request->ParseFromArray(start_auth_session_request->data,
                               start_auth_session_request->len)) {
    request.reset(NULL);
  }

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(
      FROM_HERE,
      base::Bind(&Service::DoStartFingerprintAuthSession,
                 base::Unretained(this), base::Passed(std::move(identifier)),
                 base::Passed(std::move(request)), base::Unretained(context)));
  return TRUE;
}

void Service::DoEndFingerprintAuthSession(
    std::unique_ptr<EndFingerprintAuthSessionRequest> request,
    DBusGMethodInvocation* context) {
  if (!request) {
    SendInvalidArgsReply(context, "Failed to parse parameters.");
    return;
  }

  fingerprint_manager_->EndAuthSession();
  SendReply(context, BaseReply());
}

gboolean Service::EndFingerprintAuthSession(
    const GArray* end_auth_session_request, DBusGMethodInvocation* context) {
  std::unique_ptr<EndFingerprintAuthSessionRequest> request(
      new EndFingerprintAuthSessionRequest);

  // On parsing failure, pass along a NULL.
  if (!request->ParseFromArray(end_auth_session_request->data,
                               end_auth_session_request->len)) {
    request.reset(NULL);
  }

  // If PBs don't parse, the validation in the handler will catch it.
  PostTask(
      FROM_HERE,
      base::Bind(&Service::DoEndFingerprintAuthSession, base::Unretained(this),
                 base::Passed(std::move(request)), base::Unretained(context)));
  return TRUE;
}

void Service::DoGetFirmwareManagementParameters(
    const brillo::SecureBlob& request, DBusGMethodInvocation* context) {
  GetFirmwareManagementParametersRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad GetFirmwareManagementParametersRequest");
    return;
  }
  BaseReply reply;
  GetFirmwareManagementParametersReply* extension =
      reply.MutableExtension(GetFirmwareManagementParametersReply::reply);

  if (!firmware_management_parameters_->Load()) {
    reply.set_error(CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID);
    SendReply(context, reply);
    return;
  }

  uint32_t flags;
  if (firmware_management_parameters_->GetFlags(&flags)) {
    extension->set_flags(flags);
  }

  brillo::Blob hash;
  if (firmware_management_parameters_->GetDeveloperKeyHash(&hash)) {
    extension->set_developer_key_hash(brillo::BlobToString(hash));
  }

  SendReply(context, reply);
}

gboolean Service::GetFirmwareManagementParameters(
    const GArray* request, DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(&Service::DoGetFirmwareManagementParameters,
                      base::Unretained(this),
                      SecureBlob(request->data, request->data + request->len),
                      base::Unretained(context)));
  return TRUE;
}

void Service::DoSetFirmwareManagementParameters(
    const brillo::SecureBlob& request, DBusGMethodInvocation* context) {
  SetFirmwareManagementParametersRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad SetFirmwareManagementParametersRequest");
    return;
  }

  BaseReply reply;
  if (!firmware_management_parameters_->Create()) {
    reply.set_error(
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE);
    SendReply(context, reply);
    return;
  }

  uint32_t flags = 0;
  if (request_pb.has_flags()) {
    flags = request_pb.flags();
  }

  std::unique_ptr<brillo::Blob> hash;
  if (request_pb.has_developer_key_hash()) {
    hash.reset(new brillo::Blob(
        brillo::BlobFromString(request_pb.developer_key_hash())));
  }

  if (!firmware_management_parameters_->Store(flags, hash.get())) {
    reply.set_error(
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE);
    SendReply(context, reply);
    return;
  }

  SendReply(context, reply);
}

gboolean Service::SetFirmwareManagementParameters(
    const GArray* request, DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(&Service::DoSetFirmwareManagementParameters,
                      base::Unretained(this),
                      SecureBlob(request->data, request->data + request->len),
                      base::Unretained(context)));
  return TRUE;
}

void Service::DoRemoveFirmwareManagementParameters(
    const brillo::SecureBlob& request, DBusGMethodInvocation* context) {
  RemoveFirmwareManagementParametersRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context,
                         "Bad RemoveFirmwareManagementParametersRequest");
    return;
  }
  BaseReply reply;
  if (!firmware_management_parameters_->Destroy()) {
    reply.set_error(
        CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE);
    SendReply(context, reply);
    return;
  }

  SendReply(context, reply);
}

gboolean Service::RemoveFirmwareManagementParameters(
    const GArray* request, DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(&Service::DoRemoveFirmwareManagementParameters,
                      base::Unretained(this),
                      SecureBlob(request->data, request->data + request->len),
                      base::Unretained(context)));
  return TRUE;
}

gboolean Service::GetStatusString(gchar** OUT_status, GError** error) {
  base::DictionaryValue dv;
  auto mounts = std::make_unique<base::ListValue>();
  {
    base::AutoLock _lock(mounts_lock_);
    for (const auto& mount_pair : mounts_) {
      mounts->Append(mount_pair.second->GetStatus());
    }
  }
  auto attrs = install_attrs_->GetStatus();

  Tpm::TpmStatusInfo tpm_status_info;
  tpm_->GetStatus(tpm_init_->GetCryptohomeKey(), &tpm_status_info);
  auto tpm = std::make_unique<base::DictionaryValue>();
  tpm->SetBoolean("can_connect", tpm_status_info.can_connect);
  tpm->SetBoolean("can_load_srk", tpm_status_info.can_load_srk);
  tpm->SetBoolean("can_load_srk_pubkey",
                  tpm_status_info.can_load_srk_public_key);
  tpm->SetBoolean("srk_vulnerable_roca", tpm_status_info.srk_vulnerable_roca);
  tpm->SetBoolean("has_cryptohome_key", tpm_status_info.has_cryptohome_key);
  tpm->SetBoolean("can_encrypt", tpm_status_info.can_encrypt);
  tpm->SetBoolean("can_decrypt", tpm_status_info.can_decrypt);
  tpm->SetBoolean("has_context", tpm_status_info.this_instance_has_context);
  tpm->SetBoolean("has_key_handle",
                  tpm_status_info.this_instance_has_key_handle);
  tpm->SetInteger("last_error", tpm_status_info.last_tpm_error);

  tpm->SetBoolean("enabled", tpm_->IsEnabled());
  tpm->SetBoolean("owned", tpm_->IsOwned());
  tpm->SetBoolean("being_owned", tpm_->IsBeingOwned());

  dv.Set("mounts", std::move(mounts));
  dv.Set("installattrs", std::move(attrs));
  dv.Set("tpm", std::move(tpm));
  std::string json;
  base::JSONWriter::WriteWithOptions(dv, base::JSONWriter::OPTIONS_PRETTY_PRINT,
                                     &json);
  *OUT_status = g_strdup(json.c_str());
  return TRUE;
}

void Service::DoAutoCleanup() {
  homedirs_->disk_cleanup()->FreeDiskSpace();
  last_auto_cleanup_time_ = platform_->GetCurrentTime();
}

void Service::UpdateCurrentUserActivityTimestamp() {
  base::AutoLock _lock(mounts_lock_);
  for (const auto& mount_pair : mounts_) {
    mount_pair.second->UpdateCurrentUserActivityTimestamp(0);
  }
}

// Called on Mount thread.
void Service::LowDiskCallback() {
  bool low_disk_space_signal_emitted = false;
  auto free_disk_space = homedirs_->disk_cleanup()->AmountOfFreeDiskSpace();
  auto free_space_state =
      homedirs_->disk_cleanup()->GetFreeDiskSpaceState(free_disk_space);
  if (free_space_state == DiskCleanup::FreeSpaceState::kError) {
    LOG(ERROR) << "Error getting free disk space";
  } else if (free_space_state ==
                 DiskCleanup::FreeSpaceState::kNeedNormalCleanup ||
             free_space_state ==
                 DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup) {
    g_signal_emit(cryptohome_, low_disk_space_signal_,
                  0 /* signal detail (not used) */,
                  static_cast<uint64_t>(free_disk_space.value()));
    low_disk_space_signal_emitted = true;
  }

  const base::Time current_time = platform_->GetCurrentTime();

  const bool time_for_auto_cleanup =
      current_time - last_auto_cleanup_time_ >
      base::TimeDelta::FromMilliseconds(kAutoCleanupPeriodMS);

  // We shouldn't repeat cleanups on every minute if the disk space
  // stays below the threshold. Trigger it only if there was no notification
  // previously or if enterprise owned and free space can be reclaimed.
  const bool early_cleanup_needed =
      low_disk_space_signal_emitted &&
      (!low_disk_space_signal_was_emitted_ ||
       homedirs_->disk_cleanup()->IsFreeableDiskSpaceAvailable());

  if (time_for_auto_cleanup || early_cleanup_needed)
    DoAutoCleanup();

  const bool time_for_user_activity_period_update =
      current_time - last_user_activity_timestamp_time_ >
      base::TimeDelta::FromHours(kUpdateUserActivityPeriodHours);

  if (time_for_user_activity_period_update) {
    last_user_activity_timestamp_time_ = current_time;
    UpdateCurrentUserActivityTimestamp();
  }

  low_disk_space_signal_was_emitted_ = low_disk_space_signal_emitted;

  // Schedule our next call. If the thread is terminating, we would
  // not be called. We use base::Unretained here because the Service object is
  // never destroyed.
  mount_thread_.task_runner()->PostDelayedTask(
      FROM_HERE, base::Bind(&Service::LowDiskCallback, base::Unretained(this)),
      base::TimeDelta::FromMilliseconds(low_disk_notification_period_ms_));
}

void Service::ResetDictionaryAttackMitigation() {
  if (!use_tpm_ || !tpm_init_ || !tpm_init_->IsTpmReady()) {
    return;
  }

  // If tpm_manager exists, the DA reset and UMA reporting should happen there.
  if (use_tpm_ && tpm_ && tpm_->DoesUseTpmManager()) {
    // tpm_manager doesn't take delegate as input.
    brillo::Blob unused_blob;
    if (!tpm_->ResetDictionaryAttackMitigation(unused_blob, unused_blob)) {
      LOG(WARNING) << "Failed to reset DA.";
    }
    return;
  }

  int counter = 0;
  int threshold;
  int seconds_remaining;
  bool lockout;
  if (!tpm_->GetDictionaryAttackInfo(&counter, &threshold, &lockout,
                                     &seconds_remaining)) {
    ReportDictionaryAttackResetStatus(kCounterQueryFailed);
    return;
  }
  ReportDictionaryAttackCounter(counter);
  if (counter == 0) {
    ReportDictionaryAttackResetStatus(kResetNotNecessary);
    return;
  }
  brillo::Blob delegate_blob, delegate_secret;
  bool has_reset_lock_permissions = false;
  if (!AttestationGetDelegateCredentials(&delegate_blob, &delegate_secret,
                                         &has_reset_lock_permissions)) {
    ReportDictionaryAttackResetStatus(kDelegateNotAvailable);
    return;
  }
  if (!has_reset_lock_permissions) {
    ReportDictionaryAttackResetStatus(kDelegateNotAllowed);
    return;
  }
  if (!tpm_->ResetDictionaryAttackMitigation(delegate_blob, delegate_secret)) {
    if (!tpm_->IsCurrentPCR0ValueValid()) {
      ReportDictionaryAttackResetStatus(kInvalidPcr0State);
    } else {
      ReportDictionaryAttackResetStatus(kResetAttemptFailed);
    }
    return;
  }
  ReportDictionaryAttackResetStatus(kResetAttemptSucceeded);
}

void Service::DetectEnterpriseOwnership() {
  static const char true_str[] = "true";
  const brillo::Blob true_value(true_str, true_str + base::size(true_str));
  brillo::Blob value;
  if (install_attrs_->Get("enterprise.owned", &value) && value == true_value) {
    enterprise_owned_ = true;
    // Update any active mounts with the state.
    base::AutoLock _lock(mounts_lock_);
    for (const auto& mount_pair : mounts_) {
      mount_pair.second->set_enterprise_owned(true);
    }
    homedirs_->set_enterprise_owned(true);
  }
}

scoped_refptr<cryptohome::Mount> Service::CreateUntrackedMountForUser(
    const std::string& username) {
  scoped_refptr<cryptohome::Mount> m;
  // TODO(dlunev): Decide if finalization should be moved to MountFactory.
  EnsureBootLockboxFinalized();
  m = mount_factory_->New();
  if (!m->Init(platform_, crypto_, user_timestamp_cache_.get())) {
    return nullptr;
  }
  m->set_enterprise_owned(enterprise_owned_);
  m->set_legacy_mount(legacy_mount_);
  return m;
}

scoped_refptr<cryptohome::Mount> Service::GetOrCreateMountForUser(
    const std::string& username) {
  base::AutoLock _lock(mounts_lock_);
  if (mounts_.count(username) == 0U) {
    // We don't have a mount associated with |username|, let's create one.
    scoped_refptr<cryptohome::Mount> m = CreateUntrackedMountForUser(username);
    if (!m) {
      return nullptr;
    }
    mounts_[username] = m;
  }
  return mounts_[username];
}

bool Service::RemoveMountForUser(const std::string& username) {
  base::AutoLock _lock(mounts_lock_);
  if (mounts_.count(username) != 0) {
    return (1U == mounts_.erase(username));
  }
  return true;
}

void Service::RemoveMount(cryptohome::Mount* mount) {
  base::AutoLock _lock(mounts_lock_);
  for (auto it = mounts_.begin(); it != mounts_.end(); ++it) {
    if (it->second.get() == mount) {
      mounts_.erase(it);
      break;
    }
  }
}

void Service::EnsureBootLockboxFinalized() {
  if (boot_lockbox_ && !boot_lockbox_->FinalizeBoot()) {
    LOG(WARNING) << "Failed to finalize boot lockbox when mounting guest "
                    "cryptohome";
  }
#if USE_TPM2
  // Lock NVRamBootLockbox
  auto nvram_boot_lockbox_client = BootLockboxClient::CreateBootLockboxClient();
  if (!nvram_boot_lockbox_client) {
    LOG(WARNING) << "Failed to create nvram_boot_lockbox_client";
    return;
  }

  if (!nvram_boot_lockbox_client->Finalize()) {
    LOG(WARNING) << "Failed to finalize nvram lockbox.";
  }
#endif  // USE_TMP2
}

bool Service::RemoveAllMounts(bool unmount) {
  bool ok = true;
  base::AutoLock _lock(mounts_lock_);
  for (auto it = mounts_.begin(); it != mounts_.end();) {
    scoped_refptr<cryptohome::Mount> mount = it->second;
    if (unmount && mount->IsMounted()) {
      if (mount->pkcs11_state() == cryptohome::Mount::kIsBeingInitialized) {
        // Reset the state.
        mount->set_pkcs11_state(cryptohome::Mount::kUninitialized);
        // And also reset the global failure reported state.
        // TODO(wad,ellyjones,dkrahn) De-globalize this when Chaps support
        // multiple mounts.
        reported_pkcs11_init_fail_ = false;
      }
      ok = ok && mount->UnmountCryptohome();
    }
    mounts_.erase(it++);
  }
  return ok;
}

bool Service::GetMountPointForUser(const std::string& username,
                                   FilePath* path) {
  scoped_refptr<cryptohome::Mount> mount;

  mount = GetMountForUser(username);
  if (!mount.get() || !mount->IsMounted())
    return false;
  *path = mount->mount_point();
  return true;
}

scoped_refptr<cryptohome::Mount> Service::GetMountForUser(
    const std::string& username) {
  scoped_refptr<cryptohome::Mount> mount = nullptr;
  base::AutoLock _lock(mounts_lock_);
  if (mounts_.count(username) == 1) {
    mount = mounts_[username];
  }
  return mount;
}

bool Service::CreatePublicMountSaltIfNeeded() {
  if (!public_mount_salt_.empty())
    return true;
  FilePath saltfile(kPublicMountSaltFilePath);
  return crypto_->GetOrCreateSalt(saltfile, CRYPTOHOME_DEFAULT_SALT_LENGTH,
                                  false, &public_mount_salt_);
}

bool Service::GetPublicMountPassKey(const std::string& public_mount_id,
                                    std::string* public_mount_passkey) {
  if (!CreatePublicMountSaltIfNeeded())
    return false;
  SecureBlob passkey;
  Crypto::PasswordToPasskey(public_mount_id.c_str(), public_mount_salt_,
                            &passkey);
  *public_mount_passkey = passkey.to_string();
  return true;
}

void Service::DispatchEventsForTesting() {
  event_source_.HandleDispatch();
}

gboolean Service::MigrateToDircrypto(const GArray* account_id,
                                     const GArray* migrate_request,
                                     GError** error) {
  auto identifier = std::make_unique<AccountIdentifier>();
  if (!identifier->ParseFromArray(account_id->data, account_id->len)) {
    LOG(ERROR) << "Failed to parse identifier.";
    return FALSE;
  }

  auto request = std::make_unique<MigrateToDircryptoRequest>();
  if (!request->ParseFromArray(migrate_request->data, migrate_request->len)) {
    LOG(ERROR) << "Failed to parse migrate_request.";
    return FALSE;
  }
  MigrationType migration_type = request->minimal_migration()
                                     ? MigrationType::MINIMAL
                                     : MigrationType::FULL;
  // This Dbus method just kicks the migration task on the mount thread,
  // and replies immediately.
  PostTask(FROM_HERE,
           base::Bind(&Service::DoMigrateToDircrypto, base::Unretained(this),
                      base::Owned(identifier.release()), migration_type));
  return TRUE;
}

void Service::SendDircryptoMigrationProgressSignalProto(
    const user_data_auth::DircryptoMigrationProgress& progress) {
  SendDircryptoMigrationProgressSignal(
      dircrypto_data_migrator::MigrationHelper::ConvertDircryptoMigrationStatus(
          progress.status()),
      progress.current_bytes(), progress.total_bytes());
}

void Service::DoMigrateToDircrypto(AccountIdentifier* identifier,
                                   MigrationType migration_type) {
  scoped_refptr<cryptohome::Mount> mount =
      GetMountForUser(GetAccountId(*identifier));
  if (!mount.get()) {
    LOG(ERROR) << "Failed to get mount.";
    SendDircryptoMigrationProgressSignal(DIRCRYPTO_MIGRATION_FAILED, 0, 0);
    return;
  }
  LOG(INFO) << "Migrating to dircrypto.";
  if (!mount->MigrateToDircrypto(
          base::Bind(&Service::SendDircryptoMigrationProgressSignalProto,
                     base::Unretained(this)),
          migration_type)) {
    LOG(ERROR) << "Failed to migrate.";
    SendDircryptoMigrationProgressSignal(DIRCRYPTO_MIGRATION_FAILED, 0, 0);
    return;
  }
  LOG(INFO) << "Migration done.";
  SendDircryptoMigrationProgressSignal(DIRCRYPTO_MIGRATION_SUCCESS, 0, 0);
}

gboolean Service::NeedsDircryptoMigration(const GArray* account_id,
                                          gboolean* OUT_needs_migration,
                                          GError** error) {
  std::unique_ptr<AccountIdentifier> identifier(new AccountIdentifier);
  if (!identifier->ParseFromArray(account_id->data, account_id->len)) {
    LOG(ERROR) << "No user supplied.";
    return FALSE;
  }

  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(GetAccountId(*identifier), system_salt_);
  if (!homedirs_->Exists(obfuscated_username)) {
    LOG(ERROR) << "Unknown user.";
    return FALSE;
  }

  *OUT_needs_migration = !force_ecryptfs_ && homedirs_->NeedsDircryptoMigration(
                                                 obfuscated_username);
  return TRUE;
}

gboolean Service::GetSupportedKeyPolicies(const GArray* request,
                                          DBusGMethodInvocation* context) {
  PostTask(
      FROM_HERE,
      base::Bind(&Service::DoGetSupportedKeyPolicies, base::Unretained(this),
                 std::string(request->data, request->data + request->len),
                 base::Unretained(context)));
  return TRUE;
}

void Service::DoGetSupportedKeyPolicies(const std::string& request,
                                        DBusGMethodInvocation* context) {
  GetSupportedKeyPoliciesRequest request_pb;
  if (!request_pb.ParseFromArray(request.data(), request.size())) {
    SendInvalidArgsReply(context, "Bad GetSupportedKeyPoliciesRequest");
    return;
  }

  BaseReply reply;
  GetSupportedKeyPoliciesReply* extension =
      reply.MutableExtension(GetSupportedKeyPoliciesReply::reply);

  if (use_tpm_ && tpm_) {
    if (tpm_->GetLECredentialBackend() &&
        tpm_->GetLECredentialBackend()->IsSupported()) {
      extension->set_low_entropy_credentials(true);
    }
  } else {
    extension->set_low_entropy_credentials(false);
  }

  SendReply(context, reply);
}

bool Service::GetShouldMountAsEphemeral(const std::string& account_id,
                                        bool is_ephemeral_mount_requested,
                                        bool has_create_request,
                                        bool* is_ephemeral,
                                        MountError* error) const {
  const bool is_or_will_be_owner = homedirs_->IsOrWillBeOwner(account_id);
  if (is_ephemeral_mount_requested && is_or_will_be_owner) {
    LOG(ERROR) << "An ephemeral cryptohome can only be mounted when the user "
                  "is not the owner.";
    *error = MOUNT_ERROR_FATAL;
    return false;
  }
  *is_ephemeral =
      !is_or_will_be_owner &&
      (homedirs_->AreEphemeralUsersEnabled() || is_ephemeral_mount_requested);
  if (*is_ephemeral && !has_create_request) {
    LOG(ERROR) << "An ephemeral cryptohome can only be mounted when its "
                  "creation on-the-fly is allowed.";
    *error = MOUNT_ERROR_USER_DOES_NOT_EXIST;
    return false;
  }
  return true;
}

int Service::NextSequence() {
  // AtomicSequenceNumber is zero-based, so increment so that the sequence ids
  // are one-based.
  return sequence_holder_.GetNext() + 1;
}

gboolean Service::IsQuotaSupported(gboolean* OUT_quota_supported,
                                   GError** error) {
  *OUT_quota_supported = arc_disk_quota_->IsQuotaSupported();
  return TRUE;
}

gboolean Service::GetCurrentSpaceForUid(guint32 uid,
                                        gint64* OUT_cur_space,
                                        GError** error) {
  *OUT_cur_space = arc_disk_quota_->GetCurrentSpaceForUid(uid);
  return TRUE;
}

gboolean Service::GetCurrentSpaceForGid(guint32 gid,
                                        gint64* OUT_cur_space,
                                        GError** error) {
  *OUT_cur_space = arc_disk_quota_->GetCurrentSpaceForGid(gid);
  return TRUE;
}

gboolean Service::LockToSingleUserMountUntilReboot(
    const GArray* request, DBusGMethodInvocation* context) {
  LockToSingleUserMountUntilRebootRequest request_pb;
  if (!request_pb.ParseFromArray(request->data, request->len)) {
    SendInvalidArgsReply(context, "Bad DisableLoginUntilRebootRequest.");
    return FALSE;
  }

  if (!request_pb.has_account_id()) {
    SendInvalidArgsReply(context, "Missing account_id.");
    return FALSE;
  }

  const std::string obfuscated_username = SanitizeUserNameWithSalt(
      GetAccountId(request_pb.account_id()), system_salt_);
  mount_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&Service::DoLockToSingleUserMountUntilReboot,
                            base::Unretained(this), obfuscated_username,
                            base::Unretained(context)));
  return TRUE;
}

void Service::DoGetRsuDeviceId(DBusGMethodInvocation* context) {
  std::string device_id;
  bool success = tpm_->GetRsuDeviceId(&device_id);
  // This happens when device requests RSU lookup key too frequently.
  if (!success) {
    SendFailureReply(context, "Unable to retrieve lookup key!");
    return;
  }

  BaseReply reply;
  reply.MutableExtension(GetRsuDeviceIdReply::reply)
      ->set_rsu_device_id(device_id);

  SendReply(context, reply);
}

gboolean Service::GetRsuDeviceId(const GArray* request,
                                 DBusGMethodInvocation* context) {
  PostTask(FROM_HERE,
           base::Bind(&Service::DoGetRsuDeviceId, base::Unretained(this),
                      base::Unretained(context)));
  return TRUE;
}

void Service::DoLockToSingleUserMountUntilReboot(
    const std::string& obfuscated_username, DBusGMethodInvocation* context) {
  homedirs_->SetLockedToSingleUser();
  brillo::Blob pcr_value;
  BaseReply reply;
  LockToSingleUserMountUntilRebootReply* reply_extension =
      reply.MutableExtension(LockToSingleUserMountUntilRebootReply::reply);
  if (!tpm_->ReadPCR(kTpmSingleUserPCR, &pcr_value)) {
    LOG(ERROR) << "Failed to read PCR";
    reply_extension->set_result(FAILED_TO_READ_PCR);
    reply.set_error(CRYPTOHOME_ERROR_TPM_COMM_ERROR);
  } else if (pcr_value != brillo::Blob(pcr_value.size(), 0)) {
    reply_extension->set_result(PCR_ALREADY_EXTENDED);
  } else {
    brillo::Blob extention_blob(obfuscated_username.begin(),
                                obfuscated_username.end());
    if (tpm_->GetVersion() == cryptohome::Tpm::TPM_1_2) {
      extention_blob = CryptoLib::Sha1(extention_blob);
    }
    if (!tpm_->ExtendPCR(kTpmSingleUserPCR, extention_blob)) {
      reply_extension->set_result(FAILED_TO_EXTEND_PCR);
      reply.set_error(CRYPTOHOME_ERROR_TPM_COMM_ERROR);
    }
  }
  SendReply(context, reply);
}

gboolean Service::CheckHealth(const GArray* request,
                              DBusGMethodInvocation* context) {
  CheckHealthRequest request_pb;
  if (!request_pb.ParseFromArray(request->data, request->len)) {
    SendInvalidArgsReply(context, "Bad CheckHealthRequest.");
    return FALSE;
  }

  BaseReply reply;
  CheckHealthReply* reply_extension =
      reply.MutableExtension(CheckHealthReply::reply);
  const bool is_powerwash_required = !crypto_->CanUnsealWithUserAuth();
  reply_extension->set_requires_powerwash(is_powerwash_required);

  SendReply(context, reply);
  return TRUE;
}

void Service::set_cleanup_threshold(uint64_t cleanup_threshold) {
  homedirs_->disk_cleanup()->set_cleanup_threshold(cleanup_threshold);
}

void Service::set_aggressive_cleanup_threshold(
    uint64_t aggressive_cleanup_threshold) {
  homedirs_->disk_cleanup()->set_aggressive_cleanup_threshold(
      aggressive_cleanup_threshold);
}

void Service::set_target_free_space(uint64_t target_free_space) {
  homedirs_->disk_cleanup()->set_target_free_space(target_free_space);
}

void Service::PostTaskToEventLoop(base::OnceClosure task) {
  event_source_.AddEvent(std::make_unique<ClosureEvent>(std::move(task)));
}

void Service::LogAsyncIdInfo(int async_id,
                             std::string name,
                             base::Time start_time) {
  async_id_tracked_info_[async_id] = {name, start_time};
}

void Service::SendAsyncIdInfoToUma(int async_id, base::Time finished_time) {
  auto it = async_id_tracked_info_.find(async_id);
  if (it == async_id_tracked_info_.end()) {
    LOG(WARNING) << __func__ << ": async_id: " << async_id << " not found.";
    return;
  }
  const RequestTrackedInfo& info = it->second;
  ReportAsyncDbusRequestTotalTime(info.name, finished_time - info.start_time);
  async_id_tracked_info_.erase(it);
}

gboolean Service::ShutdownService(gpointer user_data) {
  Service* service = reinterpret_cast<Service*>(user_data);
  LOG(INFO) << "Service shutting down...";
  service->Shutdown();
  // Returns false because we only need to handle it once.
  return false;
}

}  // namespace cryptohome
