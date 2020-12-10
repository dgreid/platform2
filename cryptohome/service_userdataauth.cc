// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/file_path.h>
#include <brillo/cryptohome.h>
#include <chromeos/constants/cryptohome.h>
#include <chromeos/libhwsec/task_dispatching_framework.h>

#include "cryptohome/service_userdataauth.h"
#include "cryptohome/userdataauth.h"

namespace cryptohome {

using ::hwsec::ThreadSafeDBusMethodResponse;

void UserDataAuthAdaptor::IsMounted(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::IsMountedReply>> response,
    const user_data_auth::IsMountedRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoIsMounted, base::Unretained(this),
          in_request.username(),
          ThreadSafeDBusMethodResponse<user_data_auth::IsMountedReply>::
              MakeThreadSafe(std::move(response))));
}

void UserDataAuthAdaptor::DoIsMounted(
    const std::string username,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<user_data_auth::IsMountedReply>>
        response) {
  bool is_ephemeral = false;
  bool is_mounted = service_->IsMounted(username, &is_ephemeral);

  user_data_auth::IsMountedReply reply;
  reply.set_is_mounted(is_mounted);
  reply.set_is_ephemeral_mount(is_ephemeral);
  std::move(response)->Return(reply);
}

void UserDataAuthAdaptor::Unmount(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UnmountReply>> response,
    const user_data_auth::UnmountRequest& in_request) {
  // Unmount request doesn't have any parameters
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoUnmount, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::UnmountReply>::
              MakeThreadSafe(std::move(response))));
}

void UserDataAuthAdaptor::DoUnmount(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<user_data_auth::UnmountReply>>
        response) {
  bool unmount_ok = service_->Unmount();

  user_data_auth::UnmountReply reply;
  if (!unmount_ok) {
    reply.set_error(
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }
  response->Return(reply);
}

void UserDataAuthAdaptor::Mount(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::MountReply>> response,
    const user_data_auth::MountRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoMount, base::Unretained(this),
          ThreadSafeDBusMethodResponse<
              user_data_auth::MountReply>::MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoMount(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::MountReply>> response,
    const user_data_auth::MountRequest& in_request) {
  service_->DoMount(
      in_request, base::BindOnce(
                      [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                             user_data_auth::MountReply>> local_response,
                         const user_data_auth::MountReply& reply) {
                        local_response->Return(reply);
                      },
                      std::move(response)));
}

void UserDataAuthAdaptor::StartAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::StartAuthSessionReply>> response,
    const user_data_auth::StartAuthSessionRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoStartAuthSession, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::StartAuthSessionReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoStartAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::StartAuthSessionReply>> response,
    const user_data_auth::StartAuthSessionRequest& in_request) {
  service_->StartAuthSession(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::StartAuthSessionReply>> local_response,
             const user_data_auth::StartAuthSessionReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::AuthenticateAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::AuthenticateAuthSessionReply>> response,
    const user_data_auth::AuthenticateAuthSessionRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoAuthenticateAuthSession,
                     base::Unretained(this),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::AuthenticateAuthSessionReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoAuthenticateAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::AuthenticateAuthSessionReply>> response,
    const user_data_auth::AuthenticateAuthSessionRequest& in_request) {
  service_->AuthenticateAuthSession(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::AuthenticateAuthSessionReply>> local_response,
             const user_data_auth::AuthenticateAuthSessionReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::Remove(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveReply>> response,
    const user_data_auth::RemoveRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoRemove, base::Unretained(this),
          ThreadSafeDBusMethodResponse<
              user_data_auth::RemoveReply>::MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoRemove(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveReply>> response,
    const user_data_auth::RemoveRequest& in_request) {
  user_data_auth::RemoveReply reply;
  auto status = service_->Remove(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::Rename(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RenameReply>> response,
    const user_data_auth::RenameRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoRename, base::Unretained(this),
          ThreadSafeDBusMethodResponse<
              user_data_auth::RenameReply>::MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoRename(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RenameReply>> response,
    const user_data_auth::RenameRequest& in_request) {
  user_data_auth::RenameReply reply;
  auto status = service_->Rename(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::ListKeys(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::ListKeysReply>> response,
    const user_data_auth::ListKeysRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoListKeys, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::ListKeysReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoListKeys(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::ListKeysReply>> response,
    const user_data_auth::ListKeysRequest& in_request) {
  // TODO(b/136152258): Add unit test for this method.
  user_data_auth::ListKeysReply reply;
  std::vector<std::string> labels;
  auto status = service_->ListKeys(in_request, &labels);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  if (status == user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    // The contents is |labels| is valid.
    *reply.mutable_labels() = {labels.begin(), labels.end()};
  }
  response->Return(reply);
}

void UserDataAuthAdaptor::GetKeyData(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetKeyDataReply>> response,
    const user_data_auth::GetKeyDataRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoGetKeyData, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::GetKeyDataReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoGetKeyData(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetKeyDataReply>> response,
    const user_data_auth::GetKeyDataRequest& in_request) {
  user_data_auth::GetKeyDataReply reply;
  cryptohome::KeyData data_out;
  bool found = false;
  auto status = service_->GetKeyData(in_request, &data_out, &found);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  if (reply.error() == user_data_auth::CRYPTOHOME_ERROR_NOT_SET && found) {
    *reply.add_key_data() = data_out;
  }
  response->Return(reply);
}

void UserDataAuthAdaptor::CheckKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::CheckKeyReply>> response,
    const user_data_auth::CheckKeyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoCheckKey, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::CheckKeyReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoCheckKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::CheckKeyReply>> response,
    const user_data_auth::CheckKeyRequest& in_request) {
  service_->CheckKey(
      in_request, base::BindOnce(&UserDataAuthAdaptor::DoCheckKeyDone,
                                 base::Unretained(this), std::move(response)));
}

void UserDataAuthAdaptor::DoCheckKeyDone(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::CheckKeyReply>> response,
    user_data_auth::CryptohomeErrorCode status) {
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  user_data_auth::CheckKeyReply reply;
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::AddKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::AddKeyReply>> response,
    const user_data_auth::AddKeyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoAddKey, base::Unretained(this),
          ThreadSafeDBusMethodResponse<
              user_data_auth::AddKeyReply>::MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoAddKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::AddKeyReply>> response,
    const user_data_auth::AddKeyRequest& in_request) {
  user_data_auth::AddKeyReply reply;
  auto status = service_->AddKey(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::AddDataRestoreKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::AddDataRestoreKeyReply>> response,
    const user_data_auth::AddDataRestoreKeyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoAddDataRestoreKey, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::AddDataRestoreKeyReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoAddDataRestoreKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::AddDataRestoreKeyReply>> response,
    const user_data_auth::AddDataRestoreKeyRequest& in_request) {
  user_data_auth::AddDataRestoreKeyReply reply;
  brillo::SecureBlob data_restore_key;
  auto status = service_->AddDataRestoreKey(in_request, &data_restore_key);

  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  if (status == user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    reply.set_data_restore_key(data_restore_key.to_string());
  }
  response->Return(reply);
}

void UserDataAuthAdaptor::RemoveKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveKeyReply>> response,
    const user_data_auth::RemoveKeyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoRemoveKey, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::RemoveKeyReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoRemoveKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveKeyReply>> response,
    const user_data_auth::RemoveKeyRequest& in_request) {
  user_data_auth::RemoveKeyReply reply;
  auto status = service_->RemoveKey(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::MassRemoveKeys(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::MassRemoveKeysReply>> response,
    const user_data_auth::MassRemoveKeysRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoMassRemoveKeys, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::MassRemoveKeysReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoMassRemoveKeys(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::MassRemoveKeysReply>> response,
    const user_data_auth::MassRemoveKeysRequest& in_request) {
  user_data_auth::MassRemoveKeysReply reply;
  auto status = service_->MassRemoveKeys(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::MigrateKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::MigrateKeyReply>> response,
    const user_data_auth::MigrateKeyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoMigrateKey, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::MigrateKeyReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoMigrateKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::MigrateKeyReply>> response,
    const user_data_auth::MigrateKeyRequest& in_request) {
  user_data_auth::MigrateKeyReply reply;
  auto status = service_->MigrateKey(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::StartFingerprintAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::StartFingerprintAuthSessionReply>> response,
    const user_data_auth::StartFingerprintAuthSessionRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoStartFingerprintAuthSession,
                     base::Unretained(this),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::StartFingerprintAuthSessionReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoStartFingerprintAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::StartFingerprintAuthSessionReply>> response,
    const user_data_auth::StartFingerprintAuthSessionRequest& in_request) {
  service_->StartFingerprintAuthSession(
      in_request,
      base::BindOnce(&UserDataAuthAdaptor::DoStartFingerprintAuthSessionDone,
                     base::Unretained(this), std::move(response)));
}

void UserDataAuthAdaptor::DoStartFingerprintAuthSessionDone(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::StartFingerprintAuthSessionReply>> response,
    const user_data_auth::StartFingerprintAuthSessionReply& reply) {
  response->Return(reply);
}

void UserDataAuthAdaptor::EndFingerprintAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::EndFingerprintAuthSessionReply>> response,
    const user_data_auth::EndFingerprintAuthSessionRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE, base::BindOnce(&UserDataAuth::EndFingerprintAuthSession,
                                base::Unretained(service_)));

  // This function returns immediately after ending the auth session.
  // Also, this is always successful.
  user_data_auth::EndFingerprintAuthSessionReply reply;
  response->Return(reply);
}

void UserDataAuthAdaptor::GetWebAuthnSecret(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetWebAuthnSecretReply>> response,
    const user_data_auth::GetWebAuthnSecretRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoGetWebAuthnSecret, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::GetWebAuthnSecretReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoGetWebAuthnSecret(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetWebAuthnSecretReply>> response,
    const user_data_auth::GetWebAuthnSecretRequest& in_request) {
  response->Return(service_->GetWebAuthnSecret(in_request));
}

void UserDataAuthAdaptor::StartMigrateToDircrypto(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::StartMigrateToDircryptoReply>> response,
    const user_data_auth::StartMigrateToDircryptoRequest& in_request) {
  // This will be called whenever there's a status update from the migration.
  auto status_callback = base::Bind(
      [](UserDataAuthAdaptor* adaptor,
         const user_data_auth::DircryptoMigrationProgress& progress) {
        adaptor->SendDircryptoMigrationProgressSignal(progress);
      },
      base::Unretained(this));

  // Kick start the migration process.
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::Bind(&UserDataAuth::StartMigrateToDircrypto,
                 base::Unretained(service_), in_request, status_callback));

  // This function returns immediately after starting the migration process.
  // Also, this is always successful. Failure will be notified through the
  // signal.
  user_data_auth::StartMigrateToDircryptoReply reply;
  response->Return(reply);
}

void UserDataAuthAdaptor::NeedsDircryptoMigration(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::NeedsDircryptoMigrationReply>> response,
    const user_data_auth::NeedsDircryptoMigrationRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoNeedsDircryptoMigration,
                     base::Unretained(this),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::NeedsDircryptoMigrationReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoNeedsDircryptoMigration(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::NeedsDircryptoMigrationReply>> response,
    const user_data_auth::NeedsDircryptoMigrationRequest& in_request) {
  user_data_auth::NeedsDircryptoMigrationReply reply;
  bool result = false;
  auto status =
      service_->NeedsDircryptoMigration(in_request.account_id(), &result);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  reply.set_needs_dircrypto_migration(result);
  response->Return(reply);
}

void UserDataAuthAdaptor::GetSupportedKeyPolicies(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetSupportedKeyPoliciesReply>> response,
    const user_data_auth::GetSupportedKeyPoliciesRequest& in_request) {
  user_data_auth::GetSupportedKeyPoliciesReply reply;
  reply.set_low_entropy_credentials_supported(
      service_->IsLowEntropyCredentialSupported());
  response->Return(reply);
}

void UserDataAuthAdaptor::GetAccountDiskUsage(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetAccountDiskUsageReply>> response,
    const user_data_auth::GetAccountDiskUsageRequest& in_request) {
  // Note that this is a long running call, so we're posting it to mount thread.
  service_->PostTaskToMountThread(
      FROM_HERE, base::BindOnce(&UserDataAuthAdaptor::DoGetAccountDiskUsage,
                                base::Unretained(this),
                                ThreadSafeDBusMethodResponse<
                                    user_data_auth::GetAccountDiskUsageReply>::
                                    MakeThreadSafe(std::move(response)),
                                in_request));
}

void UserDataAuthAdaptor::DoGetAccountDiskUsage(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetAccountDiskUsageReply>> response,
    const user_data_auth::GetAccountDiskUsageRequest& in_request) {
  user_data_auth::GetAccountDiskUsageReply reply;
  // Note that for now, this call always succeeds, so |reply.error| is unset.
  reply.set_size(service_->GetAccountDiskUsage(in_request.identifier()));
  response->Return(reply);
}

void UserDataAuthAdaptor::LowDiskSpaceCallback(uint64_t free_disk_space) {
  user_data_auth::LowDiskSpace signal_payload;
  signal_payload.set_disk_free_bytes(free_disk_space);
  SendLowDiskSpaceSignal(signal_payload);
}

void ArcQuotaAdaptor::GetArcDiskFeatures(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetArcDiskFeaturesReply>> response,
    const user_data_auth::GetArcDiskFeaturesRequest& in_request) {
  user_data_auth::GetArcDiskFeaturesReply reply;
  reply.set_quota_supported(service_->IsArcQuotaSupported());
  response->Return(reply);
}

void ArcQuotaAdaptor::GetCurrentSpaceForArcUid(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetCurrentSpaceForArcUidReply>> response,
    const user_data_auth::GetCurrentSpaceForArcUidRequest& in_request) {
  user_data_auth::GetCurrentSpaceForArcUidReply reply;
  reply.set_cur_space(service_->GetCurrentSpaceForArcUid(in_request.uid()));
  response->Return(reply);
}

void ArcQuotaAdaptor::GetCurrentSpaceForArcGid(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetCurrentSpaceForArcGidReply>> response,
    const user_data_auth::GetCurrentSpaceForArcGidRequest& in_request) {
  user_data_auth::GetCurrentSpaceForArcGidReply reply;
  reply.set_cur_space(service_->GetCurrentSpaceForArcGid(in_request.gid()));
  response->Return(reply);
}

void ArcQuotaAdaptor::GetCurrentSpaceForArcProjectId(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetCurrentSpaceForArcProjectIdReply>> response,
    const user_data_auth::GetCurrentSpaceForArcProjectIdRequest& in_request) {
  user_data_auth::GetCurrentSpaceForArcProjectIdReply reply;
  reply.set_cur_space(
      service_->GetCurrentSpaceForArcProjectId(in_request.project_id()));
  response->Return(reply);
}

void ArcQuotaAdaptor::SetProjectId(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::SetProjectIdReply>> response,
    const user_data_auth::SetProjectIdRequest& in_request) {
  user_data_auth::SetProjectIdReply reply;
  reply.set_success(service_->SetProjectId(
      in_request.project_id(), in_request.parent_path(),
      FilePath(in_request.child_path()), in_request.account_id()));
  response->Return(reply);
}

void Pkcs11Adaptor::Pkcs11IsTpmTokenReady(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11IsTpmTokenReadyReply>> response,
    const user_data_auth::Pkcs11IsTpmTokenReadyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&Pkcs11Adaptor::DoPkcs11IsTpmTokenReady,
                     base::Unretained(this),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::Pkcs11IsTpmTokenReadyReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void Pkcs11Adaptor::DoPkcs11IsTpmTokenReady(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11IsTpmTokenReadyReply>> response,
    const user_data_auth::Pkcs11IsTpmTokenReadyRequest& in_request) {
  user_data_auth::Pkcs11IsTpmTokenReadyReply reply;
  reply.set_ready(service_->Pkcs11IsTpmTokenReady());
  response->Return(reply);
}

void Pkcs11Adaptor::Pkcs11GetTpmTokenInfo(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11GetTpmTokenInfoReply>> response,
    const user_data_auth::Pkcs11GetTpmTokenInfoRequest& in_request) {
  user_data_auth::Pkcs11GetTpmTokenInfoReply reply;
  *reply.mutable_token_info() =
      service_->Pkcs11GetTpmTokenInfo(in_request.username());
  response->Return(reply);
}

void Pkcs11Adaptor::Pkcs11Terminate(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11TerminateReply>> response,
    const user_data_auth::Pkcs11TerminateRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &Pkcs11Adaptor::DoPkcs11Terminate, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::Pkcs11TerminateReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void Pkcs11Adaptor::DoPkcs11Terminate(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11TerminateReply>> response,
    const user_data_auth::Pkcs11TerminateRequest& in_request) {
  user_data_auth::Pkcs11TerminateReply reply;
  service_->Pkcs11Terminate();
  response->Return(reply);
}

void InstallAttributesAdaptor::InstallAttributesGet(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InstallAttributesGetReply>> response,
    const user_data_auth::InstallAttributesGetRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&InstallAttributesAdaptor::DoInstallAttributesGet,
                     base::Unretained(this),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::InstallAttributesGetReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void InstallAttributesAdaptor::DoInstallAttributesGet(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InstallAttributesGetReply>> response,
    const user_data_auth::InstallAttributesGetRequest& in_request) {
  user_data_auth::InstallAttributesGetReply reply;
  std::vector<uint8_t> data;
  bool result = service_->InstallAttributesGet(in_request.name(), &data);
  if (result) {
    *reply.mutable_value() = {data.begin(), data.end()};
  } else {
    reply.set_error(
        user_data_auth::CRYPTOHOME_ERROR_INSTALL_ATTRIBUTES_GET_FAILED);
  }
  response->Return(reply);
}

void InstallAttributesAdaptor::InstallAttributesSet(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InstallAttributesSetReply>> response,
    const user_data_auth::InstallAttributesSetRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&InstallAttributesAdaptor::DoInstallAttributesSet,
                     base::Unretained(this),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::InstallAttributesSetReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void InstallAttributesAdaptor::DoInstallAttributesSet(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InstallAttributesSetReply>> response,
    const user_data_auth::InstallAttributesSetRequest& in_request) {
  user_data_auth::InstallAttributesSetReply reply;
  std::vector<uint8_t> data(in_request.value().begin(),
                            in_request.value().end());
  bool result = service_->InstallAttributesSet(in_request.name(), data);
  if (!result) {
    reply.set_error(
        user_data_auth::CRYPTOHOME_ERROR_INSTALL_ATTRIBUTES_SET_FAILED);
  }
  response->Return(reply);
}

void InstallAttributesAdaptor::InstallAttributesFinalize(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InstallAttributesFinalizeReply>> response,
    const user_data_auth::InstallAttributesFinalizeRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&InstallAttributesAdaptor::DoInstallAttributesFinalize,
                     base::Unretained(this),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::InstallAttributesFinalizeReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void InstallAttributesAdaptor::DoInstallAttributesFinalize(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InstallAttributesFinalizeReply>> response,
    const user_data_auth::InstallAttributesFinalizeRequest& in_request) {
  user_data_auth::InstallAttributesFinalizeReply reply;
  if (!service_->InstallAttributesFinalize()) {
    reply.set_error(
        user_data_auth::CRYPTOHOME_ERROR_INSTALL_ATTRIBUTES_FINALIZE_FAILED);
  }
  response->Return(reply);
}

void InstallAttributesAdaptor::InstallAttributesGetStatus(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InstallAttributesGetStatusReply>> response,
    const user_data_auth::InstallAttributesGetStatusRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&InstallAttributesAdaptor::DoInstallAttributesGetStatus,
                     base::Unretained(this),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::InstallAttributesGetStatusReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void InstallAttributesAdaptor::DoInstallAttributesGetStatus(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InstallAttributesGetStatusReply>> response,
    const user_data_auth::InstallAttributesGetStatusRequest& in_request) {
  user_data_auth::InstallAttributesGetStatusReply reply;
  reply.set_count(service_->InstallAttributesCount());
  reply.set_is_secure(service_->InstallAttributesIsSecure());
  reply.set_state(UserDataAuth::InstallAttributesStatusToProtoEnum(
      service_->InstallAttributesGetStatus()));
  response->Return(reply);
}

void InstallAttributesAdaptor::GetFirmwareManagementParameters(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetFirmwareManagementParametersReply>> response,
    const user_data_auth::GetFirmwareManagementParametersRequest& in_request) {
  user_data_auth::GetFirmwareManagementParametersReply reply;
  user_data_auth::FirmwareManagementParameters fwmp;
  auto status = service_->GetFirmwareManagementParameters(&fwmp);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);

  if (status == user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    *reply.mutable_fwmp() = fwmp;
  }
  response->Return(reply);
}

void InstallAttributesAdaptor::RemoveFirmwareManagementParameters(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveFirmwareManagementParametersReply>> response,
    const user_data_auth::RemoveFirmwareManagementParametersRequest&
        in_request) {
  user_data_auth::RemoveFirmwareManagementParametersReply reply;
  if (!service_->RemoveFirmwareManagementParameters()) {
    reply.set_error(
        user_data_auth::
            CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE);
  }
  response->Return(reply);
}

void InstallAttributesAdaptor::SetFirmwareManagementParameters(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::SetFirmwareManagementParametersReply>> response,
    const user_data_auth::SetFirmwareManagementParametersRequest& in_request) {
  user_data_auth::SetFirmwareManagementParametersReply reply;
  auto status = service_->SetFirmwareManagementParameters(in_request.fwmp());
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetSystemSalt(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetSystemSaltReply>> response,
    const user_data_auth::GetSystemSaltRequest& in_request) {
  user_data_auth::GetSystemSaltReply reply;
  const brillo::SecureBlob& salt = service_->GetSystemSalt();
  reply.set_salt(salt.char_data(), salt.size());
  response->Return(reply);
}

void CryptohomeMiscAdaptor::UpdateCurrentUserActivityTimestamp(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UpdateCurrentUserActivityTimestampReply>> response,
    const user_data_auth::UpdateCurrentUserActivityTimestampRequest&
        in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &CryptohomeMiscAdaptor::DoUpdateCurrentUserActivityTimestamp,
          base::Unretained(this),
          ThreadSafeDBusMethodResponse<
              user_data_auth::UpdateCurrentUserActivityTimestampReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void CryptohomeMiscAdaptor::DoUpdateCurrentUserActivityTimestamp(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UpdateCurrentUserActivityTimestampReply>> response,
    const user_data_auth::UpdateCurrentUserActivityTimestampRequest&
        in_request) {
  user_data_auth::UpdateCurrentUserActivityTimestampReply reply;
  bool success =
      service_->UpdateCurrentUserActivityTimestamp(in_request.time_shift_sec());
  if (!success) {
    reply.set_error(
        user_data_auth::CRYPTOHOME_ERROR_UPDATE_USER_ACTIVITY_TIMESTAMP_FAILED);
  }
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetSanitizedUsername(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetSanitizedUsernameReply>> response,
    const user_data_auth::GetSanitizedUsernameRequest& in_request) {
  user_data_auth::GetSanitizedUsernameReply reply;
  reply.set_sanitized_username(
      brillo::cryptohome::home::SanitizeUserName(in_request.username()));
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetLoginStatus(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetLoginStatusReply>> response,
    const user_data_auth::GetLoginStatusRequest& in_request) {
  user_data_auth::GetLoginStatusReply reply;
  reply.set_owner_user_exists(service_->OwnerUserExists());
  reply.set_is_locked_to_single_user(
      base::PathExists(base::FilePath(kLockedToSingleUserFile)));
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetStatusString(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetStatusStringReply>> response,
    const user_data_auth::GetStatusStringRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &CryptohomeMiscAdaptor::DoGetStatusString, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::GetStatusStringReply>::
              MakeThreadSafe(std::move(response))));
}

void CryptohomeMiscAdaptor::DoGetStatusString(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetStatusStringReply>> response) {
  user_data_auth::GetStatusStringReply reply;
  reply.set_status(service_->GetStatusString());
  std::move(response)->Return(reply);
}

void CryptohomeMiscAdaptor::LockToSingleUserMountUntilReboot(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::LockToSingleUserMountUntilRebootReply>> response,
    const user_data_auth::LockToSingleUserMountUntilRebootRequest& in_request) {
  user_data_auth::LockToSingleUserMountUntilRebootReply reply;
  auto status =
      service_->LockToSingleUserMountUntilReboot(in_request.account_id());
  reply.set_error(status);
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetRsuDeviceId(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetRsuDeviceIdReply>> response,
    const user_data_auth::GetRsuDeviceIdRequest& in_request) {
  user_data_auth::GetRsuDeviceIdReply reply;
  std::string rsu_device_id;
  if (!service_->GetRsuDeviceId(&rsu_device_id)) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED,
                             "Unable to retrieve lookup key!");
    return;
  }
  *reply.mutable_rsu_device_id() = rsu_device_id;
  response->Return(reply);
}

void CryptohomeMiscAdaptor::CheckHealth(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::CheckHealthReply>> response,
    const user_data_auth::CheckHealthRequest& in_request) {
  user_data_auth::CheckHealthReply reply;
  reply.set_requires_powerwash(service_->RequiresPowerwash());
  response->Return(reply);
}

}  // namespace cryptohome
