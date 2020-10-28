// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/constants/cryptohome.h>

#include "cryptohome/dircrypto_data_migrator/migration_helper.h"
#include "cryptohome/proxy/legacy_cryptohome_interface_adaptor.h"

namespace {
constexpr base::TimeDelta kDefaultTimeout = base::TimeDelta::FromMinutes(2);
constexpr base::TimeDelta kAttestationProxyTimeout =
    base::TimeDelta::FromMinutes(2);
constexpr base::TimeDelta kMountTimeout = base::TimeDelta::FromMinutes(2);
}

namespace cryptohome {

void LegacyCryptohomeInterfaceAdaptor::RegisterAsync() {
  RegisterWithDBusObject(dbus_object_);

  // Register the dbus signal handlers
  userdataauth_proxy_->RegisterDircryptoMigrationProgressSignalHandler(
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::OnDircryptoMigrationProgressSignal,
          base::Unretained(this)),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::OnSignalConnectedHandler,
                 base::Unretained(this)));
  userdataauth_proxy_->RegisterLowDiskSpaceSignalHandler(
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::OnLowDiskSpaceSignal,
                 base::Unretained(this)),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::OnSignalConnectedHandler,
                 base::Unretained(this)));
  tpm_ownership_proxy_->RegisterSignalOwnershipTakenSignalHandler(
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::OnOwnershipTakenSignal,
                 base::Unretained(this)),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::OnSignalConnectedHandler,
                 base::Unretained(this)));
}

void LegacyCryptohomeInterfaceAdaptor::IsMounted(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::IsMountedRequest request;
  userdataauth_proxy_->IsMountedAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::IsMountedOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::IsMountedOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::IsMountedReply& reply) {
  response->Return(reply.is_mounted());
}

void LegacyCryptohomeInterfaceAdaptor::IsMountedForUser(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool, bool>>
        response,
    const std::string& in_username) {
  auto response_shared = std::make_shared<SharedDBusMethodResponse<bool, bool>>(
      std::move(response));

  user_data_auth::IsMountedRequest request;
  request.set_username(in_username);
  userdataauth_proxy_->IsMountedAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::IsMountedForUserOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool, bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::IsMountedForUserOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool, bool>> response,
    const user_data_auth::IsMountedReply& reply) {
  response->Return(reply.is_mounted(), reply.is_ephemeral_mount());
}

void LegacyCryptohomeInterfaceAdaptor::ListKeysEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::ListKeysRequest& /*in_list_keys_request*/) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::ListKeysRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  // Note that in_list_keys_request is empty
  userdataauth_proxy_->ListKeysAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ListKeysExOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::ListKeysExOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::ListKeysReply& reply) {
  cryptohome::BaseReply result;
  result.set_error(static_cast<cryptohome::CryptohomeErrorCode>(reply.error()));
  cryptohome::ListKeysReply* result_extension =
      result.MutableExtension(cryptohome::ListKeysReply::reply);
  result_extension->mutable_labels()->CopyFrom(reply.labels());
  ClearErrorIfNotSet(&result);
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::CheckKeyEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::CheckKeyRequest& in_check_key_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::CheckKeyRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  userdataauth_proxy_->CheckKeyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::CheckKeyReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::RemoveKeyEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::RemoveKeyRequest& in_remove_key_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::RemoveKeyRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  request.mutable_key()->CopyFrom(in_remove_key_request.key());
  userdataauth_proxy_->RemoveKeyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::RemoveKeyReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::MassRemoveKeys(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::MassRemoveKeysRequest& in_mass_remove_keys_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::MassRemoveKeysRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  request.mutable_exempt_key_data()->CopyFrom(
      in_mass_remove_keys_request.exempt_key_data());
  userdataauth_proxy_->MassRemoveKeysAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::MassRemoveKeysReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetKeyDataEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::GetKeyDataRequest& in_get_key_data_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::GetKeyDataRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  request.mutable_key()->CopyFrom(in_get_key_data_request.key());
  userdataauth_proxy_->GetKeyDataAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::GetKeyDataOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetKeyDataOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::GetKeyDataReply& reply) {
  cryptohome::BaseReply result;
  result.set_error(static_cast<cryptohome::CryptohomeErrorCode>(reply.error()));
  cryptohome::GetKeyDataReply* result_extension =
      result.MutableExtension(cryptohome::GetKeyDataReply::reply);
  result_extension->mutable_key_data()->CopyFrom(reply.key_data());
  ClearErrorIfNotSet(&result);
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::MigrateKeyEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::MigrateKeyRequest& in_migrate_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::MigrateKeyRequest request;
  request.mutable_account_id()->CopyFrom(in_account);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  request.set_secret(in_migrate_request.secret());
  userdataauth_proxy_->MigrateKeyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::MigrateKeyReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::AddKeyEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::AddKeyRequest& in_add_key_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::AddKeyRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  request.mutable_key()->CopyFrom(in_add_key_request.key());
  request.set_clobber_if_exists(in_add_key_request.clobber_if_exists());
  userdataauth_proxy_->AddKeyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::AddKeyReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::AddDataRestoreKey(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::AddDataRestoreKeyRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  userdataauth_proxy_->AddDataRestoreKeyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::AddDataRestoreKeyOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::AddDataRestoreKeyOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::AddDataRestoreKeyReply& reply) {
  cryptohome::BaseReply result;
  result.set_error(static_cast<cryptohome::CryptohomeErrorCode>(reply.error()));
  cryptohome::AddDataRestoreKeyReply* result_extension =
      result.MutableExtension(cryptohome::AddDataRestoreKeyReply::reply);
  if (result.error() == CRYPTOHOME_ERROR_NOT_SET) {
    result_extension->set_data_restore_key(reply.data_restore_key());
  }
  ClearErrorIfNotSet(&result);
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::UpdateKeyEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::UpdateKeyRequest& in_update_key_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::UpdateKeyRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  request.mutable_changes()->CopyFrom(in_update_key_request.changes());
  request.set_authorization_signature(
      in_update_key_request.authorization_signature());
  userdataauth_proxy_->UpdateKeyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::UpdateKeyReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::RemoveEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::RemoveRequest request;
  request.mutable_identifier()->CopyFrom(in_account);
  userdataauth_proxy_->RemoveAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::RemoveReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetSystemSalt(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>>
        response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::vector<uint8_t>>>(
          std::move(response));

  user_data_auth::GetSystemSaltRequest request;
  misc_proxy_->GetSystemSaltAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::GetSystemSaltOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>>,
          base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetSystemSaltOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>>> response,
    const user_data_auth::GetSystemSaltReply& reply) {
  std::vector<uint8_t> salt(reply.salt().begin(), reply.salt().end());
  response->Return(salt);
}

void LegacyCryptohomeInterfaceAdaptor::GetSanitizedUsername(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string>>
        response,
    const std::string& in_username) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::string>>(
          std::move(response));

  user_data_auth::GetSanitizedUsernameRequest request;
  request.set_username(in_username);
  misc_proxy_->GetSanitizedUsernameAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::GetSanitizedUsernameOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<std::string>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetSanitizedUsernameOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::string>> response,
    const user_data_auth::GetSanitizedUsernameReply& reply) {
  response->Return(reply.sanitized_username());
}

void LegacyCryptohomeInterfaceAdaptor::MountEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::MountRequest& in_mount_request) {
  std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>>
      response_shared =
          std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
              std::move(response));

  user_data_auth::MountRequest request;
  *request.mutable_account() = in_account_id;
  request.mutable_authorization()->CopyFrom(in_authorization_request);
  request.set_require_ephemeral(in_mount_request.require_ephemeral());
  if (in_mount_request.has_create()) {
    request.mutable_create()->mutable_keys()->CopyFrom(
        in_mount_request.create().keys());
    request.mutable_create()->set_copy_authorization_key(
        in_mount_request.create().copy_authorization_key());
    request.mutable_create()->set_force_ecryptfs(
        in_mount_request.create().force_ecryptfs());
  }
  request.set_force_dircrypto_if_available(
      in_mount_request.force_dircrypto_if_available());
  request.set_to_migrate_from_ecryptfs(
      in_mount_request.to_migrate_from_ecryptfs());
  request.set_public_mount(in_mount_request.public_mount());
  request.set_hidden_mount(in_mount_request.hidden_mount());
  request.set_guest_mount(false);
  // There's another MountGuestEx to handle guest mount. This method only
  // deal with non-guest mount so guest_mount is false here.

  userdataauth_proxy_->MountAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::MountExOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kMountTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::MountExOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::MountReply& reply) {
  if (reply.error() ==
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT) {
    constexpr char error_msg[] =
        "Invalid argument on MountEx(), see logs for more details.";
    LOG(WARNING) << error_msg;
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_INVALID_ARGS, error_msg);
    return;
  }
  cryptohome::BaseReply result;
  result.set_error(static_cast<cryptohome::CryptohomeErrorCode>(reply.error()));
  MountReply* result_extension =
      result.MutableExtension(cryptohome::MountReply::reply);
  result_extension->set_recreated(reply.recreated());
  result_extension->set_sanitized_username(reply.sanitized_username());
  ClearErrorIfNotSet(&result);
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::MountGuestEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::MountGuestRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::MountRequest request;
  request.set_guest_mount(true);

  userdataauth_proxy_->MountAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::MountReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kMountTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::RenameCryptohome(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_cryptohome_id_from,
    const cryptohome::AccountIdentifier& in_cryptohome_id_to) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::RenameRequest request;
  *request.mutable_id_from() = in_cryptohome_id_from;
  *request.mutable_id_to() = in_cryptohome_id_to;
  userdataauth_proxy_->RenameAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::RenameReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetAccountDiskUsage(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::GetAccountDiskUsageRequest request;
  *request.mutable_identifier() = in_account_id;
  userdataauth_proxy_->GetAccountDiskUsageAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::GetAccountDiskUsageOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetAccountDiskUsageOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::GetAccountDiskUsageReply& reply) {
  cryptohome::BaseReply result;
  result.set_error(static_cast<cryptohome::CryptohomeErrorCode>(reply.error()));
  cryptohome::GetAccountDiskUsageReply* result_extension =
      result.MutableExtension(cryptohome::GetAccountDiskUsageReply::reply);
  result_extension->set_size(reply.size());
  ClearErrorIfNotSet(&result);
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::UnmountEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::UnmountRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::UnmountRequest request;
  userdataauth_proxy_->UnmountAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::UnmountReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::UpdateCurrentUserActivityTimestamp(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    int32_t in_time_shift_sec) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<>>(std::move(response));

  user_data_auth::UpdateCurrentUserActivityTimestampRequest request;
  request.set_time_shift_sec(in_time_shift_sec);
  misc_proxy_->UpdateCurrentUserActivityTimestampAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     UpdateCurrentUserActivityTimestampOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::
    UpdateCurrentUserActivityTimestampOnSuccess(
        std::shared_ptr<SharedDBusMethodResponse<>> response,
        const user_data_auth::UpdateCurrentUserActivityTimestampReply& reply) {
  if (reply.error() != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(WARNING) << "UpdateCurrentUserActivityTimestamp() failure received by "
                    "cryptohome-proxy, error "
                 << static_cast<int>(reply.error());
  }
  response->Return();
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsReady(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  tpm_manager::GetTpmStatusRequest request;
  tpm_ownership_proxy_->GetTpmStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::TpmIsReadyOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsReadyOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const tpm_manager::GetTpmStatusReply& reply) {
  response->Return(reply.enabled() && reply.owned());
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsEnabled(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  tpm_manager::GetTpmStatusRequest request;
  tpm_ownership_proxy_->GetTpmStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::TpmIsEnabledOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsEnabledOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const tpm_manager::GetTpmStatusReply& reply) {
  response->Return(reply.enabled());
}

void LegacyCryptohomeInterfaceAdaptor::TpmGetPassword(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string>>
        response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::string>>(
          std::move(response));

  tpm_manager::GetTpmStatusRequest request;
  tpm_ownership_proxy_->GetTpmStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::TpmGetPasswordOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<std::string>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmGetPasswordOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::string>> response,
    const tpm_manager::GetTpmStatusReply& reply) {
  response->Return(reply.local_data().owner_password());
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsOwned(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  tpm_manager::GetTpmStatusRequest request;
  tpm_ownership_proxy_->GetTpmStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::TpmIsOwnedOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsOwnedOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const tpm_manager::GetTpmStatusReply& reply) {
  response->Return(reply.owned());
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsBeingOwned(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  // Note that TpmIsBeingOwned is no longer available. External owners can no
  // longer query whether TPM Ownership is currently being taken.
  LOG(WARNING) << "Obsolete TpmIsBeingOwned is called.";
  response->Return(false);
}

void LegacyCryptohomeInterfaceAdaptor::TpmCanAttemptOwnership(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) {
  tpm_manager::TakeOwnershipRequest request;
  tpm_ownership_proxy_->TakeOwnershipAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmCanAttemptOwnershipOnSuccess,
          base::Unretained(this)),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmCanAttemptOwnershipOnFailure,
          base::Unretained(this)),
      kDefaultTimeout.InMilliseconds());

  // Note that this method is special in the sense that this call will return
  // immediately as soon as the target method is called on the UserDataAuth
  // side. The result from the target method on UserDataAuth side is not passed
  // back to the caller of this method, but instead is logged if there's any
  // failure.
  response->Return();
}

void LegacyCryptohomeInterfaceAdaptor::TpmCanAttemptOwnershipOnSuccess(
    const tpm_manager::TakeOwnershipReply& reply) {
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(WARNING) << "TakeOwnership failure observed in "
                    "TpmCanAttemptOwnership() of cryptohome-proxy. Status: "
                 << static_cast<int>(reply.status());
  }
}

void LegacyCryptohomeInterfaceAdaptor::TpmCanAttemptOwnershipOnFailure(
    brillo::Error* err) {
  // Note that creation of Error object already logs the error.
  LOG(WARNING) << "TakeOwnership encountered an error, observed in "
                  "TpmCanAttemptOwnership() of cryptohome-proxy.";
}

void LegacyCryptohomeInterfaceAdaptor::TpmClearStoredPassword(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<>>(std::move(response));

  tpm_manager::ClearStoredOwnerPasswordRequest request;
  tpm_ownership_proxy_->ClearStoredOwnerPasswordAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmClearStoredPasswordOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmClearStoredPasswordOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<>> response,
    const tpm_manager::ClearStoredOwnerPasswordReply& reply) {
  response->Return();
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsAttestationPrepared(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  attestation::GetEnrollmentPreparationsRequest request;

  std::shared_ptr<SharedDBusMethodResponse<bool>> response_shared(
      new SharedDBusMethodResponse<bool>(std::move(response)));

  attestation_proxy_->GetEnrollmentPreparationsAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmIsAttestationPreparedOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsAttestationPreparedOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::GetEnrollmentPreparationsReply& reply) {
  bool prepared = false;
  for (const auto& preparation : reply.enrollment_preparations()) {
    if (preparation.second) {
      prepared = true;
      break;
    }
  }

  response->Return(prepared);
}

void LegacyCryptohomeInterfaceAdaptor::
    TpmAttestationGetEnrollmentPreparationsEx(
        std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
            cryptohome::BaseReply>> response,
        const cryptohome::AttestationGetEnrollmentPreparationsRequest&
            in_request) {
  base::Optional<attestation::ACAType> aca_type;
  const int in_pca_type = in_request.pca_type();
  aca_type = IntegerToACAType(in_pca_type);
  if (!aca_type.has_value()) {
    std::string error_msg =
        "Requested ACA type " + std::to_string(in_pca_type) +
        " is not supported in TpmAttestationGetEnrollmentPreparationsEx()";
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED, error_msg);
    return;
  }

  std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>>
      response_shared(new SharedDBusMethodResponse<cryptohome::BaseReply>(
          std::move(response)));

  attestation::GetEnrollmentPreparationsRequest request;
  request.set_aca_type(aca_type.value());

  attestation_proxy_->GetEnrollmentPreparationsAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationGetEnrollmentPreparationsExOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::
    TpmAttestationGetEnrollmentPreparationsExOnSuccess(
        std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>>
            response,
        const attestation::GetEnrollmentPreparationsReply& reply) {
  cryptohome::BaseReply result;
  AttestationGetEnrollmentPreparationsReply* extension =
      result.MutableExtension(AttestationGetEnrollmentPreparationsReply::reply);

  if (reply.status() != attestation::STATUS_SUCCESS) {
    // Failure.
    result.set_error(CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);
  } else {
    auto map = reply.enrollment_preparations();
    for (auto it = map.cbegin(), end = map.cend(); it != end; ++it) {
      (*extension->mutable_enrollment_preparations())[it->first] = it->second;
    }
  }

  ClearErrorIfNotSet(&result);
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::TpmVerifyAttestationData(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    bool in_is_cros_core) {
  std::shared_ptr<SharedDBusMethodResponse<bool>> response_shared(
      new SharedDBusMethodResponse<bool>(std::move(response)));

  attestation::VerifyRequest request;
  request.set_cros_core(in_is_cros_core);
  request.set_ek_only(false);

  attestation_proxy_->VerifyAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmVerifyAttestationDataOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmVerifyAttestationDataOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::VerifyReply& reply) {
  if (reply.status() != attestation::STATUS_SUCCESS) {
    std::string error_msg =
        "TpmVerifyAttestationData(): Attestation daemon returned status " +
        std::to_string(static_cast<int>(reply.status()));
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, error_msg);
    return;
  }
  response->Return(reply.verified());
}

void LegacyCryptohomeInterfaceAdaptor::TpmVerifyEK(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    bool in_is_cros_core) {
  std::shared_ptr<SharedDBusMethodResponse<bool>> response_shared(
      new SharedDBusMethodResponse<bool>(std::move(response)));

  attestation::VerifyRequest request;
  request.set_cros_core(in_is_cros_core);
  request.set_ek_only(true);

  attestation_proxy_->VerifyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::TpmVerifyEKOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmVerifyEKOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::VerifyReply& reply) {
  if (reply.status() != attestation::STATUS_SUCCESS) {
    std::string error_msg =
        "TpmVerifyEK(): Attestation daemon returned status " +
        std::to_string(static_cast<int>(reply.status()));
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, error_msg);
    return;
  }
  response->Return(reply.verified());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationCreateEnrollRequest(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    int32_t in_pca_type) {
  attestation::CreateEnrollRequestRequest request;
  base::Optional<attestation::ACAType> aca_type;
  aca_type = IntegerToACAType(in_pca_type);
  if (!aca_type.has_value()) {
    std::string error_msg = "Requested ACA type " +
                            std::to_string(in_pca_type) + " is not supported";
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED, error_msg);
    return;
  }
  request.set_aca_type(aca_type.value());

  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::vector<uint8_t>>>(
          std::move(response));

  attestation_proxy_->CreateEnrollRequestAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationCreateEnrollRequestOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>>,
          base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::
    TpmAttestationCreateEnrollRequestOnSuccess(
        std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>>>
            response,
        const attestation::CreateEnrollRequestReply& reply) {
  if (reply.status() != attestation::STATUS_SUCCESS) {
    std::string error_msg = "Attestation daemon returned status " +
                            std::to_string(static_cast<int>(reply.status()));
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, error_msg);
    return;
  }
  std::vector<uint8_t> result(reply.pca_request().begin(),
                              reply.pca_request().end());
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::AsyncTpmAttestationCreateEnrollRequest(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    int32_t in_pca_type) {
  attestation::CreateEnrollRequestRequest request;

  base::Optional<attestation::ACAType> aca_type;
  aca_type = IntegerToACAType(in_pca_type);
  if (!aca_type.has_value()) {
    std::string error_msg =
        "AsyncTpmAttestationCreateEnrollRequest(): Requested ACA type " +
        std::to_string(in_pca_type) + " is not supported";
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED, error_msg);
    return;
  }
  request.set_aca_type(aca_type.value());

  int async_id = HandleAsyncData<attestation::CreateEnrollRequestRequest,
                                 attestation::CreateEnrollRequestReply>(
      &attestation::CreateEnrollRequestReply::pca_request, request,
      base::BindOnce(
          &org::chromium::AttestationProxyInterface::CreateEnrollRequestAsync,
          base::Unretained(attestation_proxy_)),
      kAttestationProxyTimeout.InMilliseconds());
  response->Return(async_id);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationEnroll(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    int32_t in_pca_type,
    const std::vector<uint8_t>& in_pca_response) {
  attestation::FinishEnrollRequest request;
  request.set_pca_response(in_pca_response.data(), in_pca_response.size());
  base::Optional<attestation::ACAType> aca_type;
  aca_type = IntegerToACAType(in_pca_type);
  if (!aca_type.has_value()) {
    std::string error_msg = "Requested ACA type " +
                            std::to_string(in_pca_type) + " is not supported";
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED, error_msg);
    return;
  }
  request.set_aca_type(aca_type.value());

  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));
  attestation_proxy_->FinishEnrollAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::TpmAttestationEnrollSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationEnrollSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::FinishEnrollReply& reply) {
  response->Return(reply.status() ==
                   attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::AsyncTpmAttestationEnroll(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    int32_t in_pca_type,
    const std::vector<uint8_t>& in_pca_response) {
  attestation::FinishEnrollRequest request;
  request.set_pca_response(in_pca_response.data(), in_pca_response.size());
  base::Optional<attestation::ACAType> aca_type;
  aca_type = IntegerToACAType(in_pca_type);
  if (!aca_type.has_value()) {
    std::string error_msg = "Requested ACA type " +
                            std::to_string(in_pca_type) + " is not supported";
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED, error_msg);
    return;
  }
  request.set_aca_type(aca_type.value());

  int async_id = HandleAsyncStatus<attestation::FinishEnrollRequest,
                                   attestation::FinishEnrollReply>(
      request,
      base::BindOnce(
          &org::chromium::AttestationProxyInterface::FinishEnrollAsync,
          base::Unretained(attestation_proxy_)),
      kAttestationProxyTimeout.InMilliseconds());

  response->Return(async_id);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationCreateCertRequest(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    int32_t in_pca_type,
    int32_t in_certificate_profile,
    const std::string& in_username,
    const std::string& in_request_origin) {
  attestation::CreateCertificateRequestRequest request;
  request.set_certificate_profile(
      IntegerToCertificateProfile(in_certificate_profile));
  request.set_username(in_username);
  request.set_request_origin(in_request_origin);
  base::Optional<attestation::ACAType> aca_type;
  aca_type = IntegerToACAType(in_pca_type);
  if (!aca_type.has_value()) {
    std::string error_msg = "Requested ACA type " +
                            std::to_string(in_pca_type) + " is not supported";
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED, error_msg);
    return;
  }
  request.set_aca_type(aca_type.value());

  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::vector<uint8_t>>>(
          std::move(response));
  attestation_proxy_->CreateCertificateRequestAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationCreateCertRequestOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>>,
          base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationCreateCertRequestOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>>> response,
    const attestation::CreateCertificateRequestReply& reply) {
  if (reply.status() != attestation::STATUS_SUCCESS) {
    std::string error_msg = "Attestation daemon returned status " +
                            std::to_string(static_cast<int>(reply.status()));
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, error_msg);
    return;
  }
  std::vector<uint8_t> result(reply.pca_request().begin(),
                              reply.pca_request().end());
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::AsyncTpmAttestationCreateCertRequest(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    int32_t in_pca_type,
    int32_t in_certificate_profile,
    const std::string& in_username,
    const std::string& in_request_origin) {
  attestation::CreateCertificateRequestRequest request;

  base::Optional<attestation::ACAType> aca_type;
  aca_type = IntegerToACAType(in_pca_type);
  if (!aca_type.has_value()) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED,
                             "Requested ACA type is not supported");
    return;
  }

  request.set_aca_type(aca_type.value());
  request.set_certificate_profile(
      IntegerToCertificateProfile(in_certificate_profile));
  request.set_username(in_username);
  request.set_request_origin(in_request_origin);
  int async_id = HandleAsyncData<attestation::CreateCertificateRequestRequest,
                                 attestation::CreateCertificateRequestReply>(
      &attestation::CreateCertificateRequestReply::pca_request, request,
      base::BindOnce(&org::chromium::AttestationProxyInterface::
                         CreateCertificateRequestAsync,
                     base::Unretained(attestation_proxy_)),
      kAttestationProxyTimeout.InMilliseconds());
  response->Return(async_id);
  return;
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationFinishCertRequest(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    const std::vector<uint8_t>& in_pca_response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  attestation::FinishCertificateRequestRequest request;
  request.set_pca_response(in_pca_response.data(), in_pca_response.size());
  request.set_key_label(in_key_name);
  if (in_is_user_specific) {
    request.set_username(in_username);
  }

  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>(
          std::move(response));
  attestation_proxy_->FinishCertificateRequestAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationFinishCertRequestOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>,
                                                          bool>,
          base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationFinishCertRequestOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
        response,
    const attestation::FinishCertificateRequestReply& reply) {
  std::vector<uint8_t> cert;
  if (reply.status() == attestation::AttestationStatus::STATUS_SUCCESS) {
    cert.assign(reply.certificate().begin(), reply.certificate().end());
  } else {
    LOG(WARNING) << "TpmAttestationFinishCertRequest(): Attestation daemon "
                    "returned status "
                 << static_cast<int>(reply.status());
  }
  response->Return(
      cert, reply.status() == attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::AsyncTpmAttestationFinishCertRequest(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    const std::vector<uint8_t>& in_pca_response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  attestation::FinishCertificateRequestRequest request;
  request.set_pca_response(in_pca_response.data(), in_pca_response.size());
  request.set_key_label(in_key_name);
  if (in_is_user_specific) {
    request.set_username(in_username);
  }

  int async_id = HandleAsyncData<attestation::FinishCertificateRequestRequest,
                                 attestation::FinishCertificateRequestReply>(
      &attestation::FinishCertificateRequestReply::certificate, request,
      base::BindOnce(&org::chromium::AttestationProxyInterface::
                         FinishCertificateRequestAsync,
                     base::Unretained(attestation_proxy_)),
      kAttestationProxyTimeout.InMilliseconds());
  response->Return(async_id);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationEnrollEx(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    int32_t in_pca_type,
    bool in_forced) {
  DCHECK(false) << "Not implemented.";
}

void LegacyCryptohomeInterfaceAdaptor::AsyncTpmAttestationEnrollEx(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    int32_t in_pca_type,
    bool in_forced) {
  DCHECK(false) << "Not implemented.";
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetCertificateEx(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    int32_t in_certificate_profile,
    const std::string& in_username,
    const std::string& in_request_origin,
    int32_t in_pca_type,
    int32_t in_key_type,
    const std::string& in_key_name,
    bool in_forced,
    bool in_shall_trigger_enrollment) {
  DCHECK(false) << "Not implemented.";
}

void LegacyCryptohomeInterfaceAdaptor::AsyncTpmAttestationGetCertificateEx(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    int32_t in_certificate_profile,
    const std::string& in_username,
    const std::string& in_request_origin,
    int32_t in_pca_type,
    int32_t in_key_type,
    const std::string& in_key_name,
    bool in_forced,
    bool in_shall_trigger_enrollment) {
  DCHECK(false) << "Not implemented.";
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsAttestationEnrolled(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  std::shared_ptr<SharedDBusMethodResponse<bool>> response_shared(
      new SharedDBusMethodResponse<bool>(std::move(response)));

  attestation::GetStatusRequest request;
  request.set_extended_status(false);

  attestation_proxy_->GetStatusAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmIsAttestationEnrolledOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsAttestationEnrolledOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::GetStatusReply& reply) {
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    std::string error_msg =
        "TpmIsAttestationEnrolled(): Attestation daemon returned status " +
        std::to_string(static_cast<int>(reply.status()));
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, error_msg);
    return;
  }
  response->Return(reply.enrolled());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationDoesKeyExist(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  std::shared_ptr<SharedDBusMethodResponse<bool>> response_shared(
      new SharedDBusMethodResponse<bool>(std::move(response)));

  attestation::GetKeyInfoRequest request;
  request.set_key_label(in_key_name);
  if (in_is_user_specific) {
    request.set_username(in_username);
  }

  attestation_proxy_->GetKeyInfoAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationDoesKeyExistOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationDoesKeyExistOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::GetKeyInfoReply& reply) {
  response->Return(reply.status() ==
                   attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetCertificate(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
      response_shared(new SharedDBusMethodResponse<std::vector<uint8_t>, bool>(
          std::move(response)));

  attestation::GetKeyInfoRequest request;
  request.set_key_label(in_key_name);
  if (in_is_user_specific) {
    request.set_username(in_username);
  }

  attestation_proxy_->GetKeyInfoAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationGetCertificateOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>,
                                                          bool>,
          base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetCertificateOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
        response,
    const attestation::GetKeyInfoReply& reply) {
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    std::string error_msg =
        "TpmAttestationGetCertificate(): Attestation daemon returned status " +
        std::to_string(static_cast<int>(reply.status()));
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, error_msg);
    return;
  }
  std::vector<uint8_t> cert(reply.certificate().begin(),
                            reply.certificate().end());
  response->Return(
      cert, reply.status() == attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetPublicKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
      response_shared(new SharedDBusMethodResponse<std::vector<uint8_t>, bool>(
          std::move(response)));

  attestation::GetKeyInfoRequest request;
  request.set_key_label(in_key_name);
  if (in_is_user_specific) {
    request.set_username(in_username);
  }

  attestation_proxy_->GetKeyInfoAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationGetPublicKeyOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>,
                                                          bool>,
          base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetPublicKeyOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
        response,
    const attestation::GetKeyInfoReply& reply) {
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    std::string error_msg =
        "TpmAttestationGetPublicKey(): Attestation daemon returned status " +
        std::to_string(static_cast<int>(reply.status()));
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, error_msg);
    return;
  }
  std::vector<uint8_t> public_key(reply.public_key().begin(),
                                  reply.public_key().end());
  response->Return(
      public_key,
      reply.status() == attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetEnrollmentId(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    bool in_ignore_cache) {
  attestation::GetEnrollmentIdRequest request;
  request.set_ignore_cache(in_ignore_cache);

  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>(
          std::move(response));
  attestation_proxy_->GetEnrollmentIdAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationGetEnrollmentIdOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>,
                                                          bool>,
          base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetEnrollmentIdOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
        response,
    const attestation::GetEnrollmentIdReply& reply) {
  std::vector<uint8_t> enrollment_id;
  if (reply.status() == attestation::AttestationStatus::STATUS_SUCCESS) {
    enrollment_id.assign(reply.enrollment_id().begin(),
                         reply.enrollment_id().end());
  } else {
    LOG(WARNING) << "TpmAttestationGetEnrollmentId(): Attestation daemon "
                    "returned status "
                 << static_cast<int>(reply.status());
  }
  response->Return(
      enrollment_id,
      reply.status() == attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationRegisterKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  attestation::RegisterKeyWithChapsTokenRequest request;
  request.set_key_label(in_key_name);
  if (in_is_user_specific) {
    request.set_username(in_username);
  }

  int async_id =
      HandleAsyncStatus<attestation::RegisterKeyWithChapsTokenRequest,
                        attestation::RegisterKeyWithChapsTokenReply>(
          request,
          base::BindOnce(&org::chromium::AttestationProxyInterface::
                             RegisterKeyWithChapsTokenAsync,
                         base::Unretained(attestation_proxy_)),
          kAttestationProxyTimeout.InMilliseconds());

  response->Return(async_id);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationSignEnterpriseChallenge(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name,
    const std::string& in_domain,
    const std::vector<uint8_t>& in_device_id,
    bool in_include_signed_public_key,
    const std::vector<uint8_t>& in_challenge) {
  TpmAttestationSignEnterpriseVaChallenge(
      std::move(response), static_cast<int32_t>(attestation::DEFAULT_VA),
      in_is_user_specific, in_username, in_key_name, in_domain, in_device_id,
      in_include_signed_public_key, in_challenge);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationSignEnterpriseVaChallenge(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    int32_t in_va_type,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name,
    const std::string& in_domain,
    const std::vector<uint8_t>& in_device_id,
    bool in_include_signed_public_key,
    const std::vector<uint8_t>& in_challenge) {
  TpmAttestationSignEnterpriseVaChallengeV2Actual(
      std::move(response), in_va_type, in_is_user_specific, in_username,
      in_key_name, in_domain, in_device_id, in_include_signed_public_key,
      in_challenge, base::nullopt);
}

void LegacyCryptohomeInterfaceAdaptor::
    TpmAttestationSignEnterpriseVaChallengeV2(
        std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>>
            response,
        int32_t in_va_type,
        bool in_is_user_specific,
        const std::string& in_username,
        const std::string& in_key_name,
        const std::string& in_domain,
        const std::vector<uint8_t>& in_device_id,
        bool in_include_signed_public_key,
        const std::vector<uint8_t>& in_challenge,
        const std::string& in_key_name_for_spkac) {
  TpmAttestationSignEnterpriseVaChallengeV2Actual(
      std::move(response), in_va_type, in_is_user_specific, in_username,
      in_key_name, in_domain, in_device_id, in_include_signed_public_key,
      in_challenge, in_key_name_for_spkac);
}

void LegacyCryptohomeInterfaceAdaptor::
    TpmAttestationSignEnterpriseVaChallengeV2Actual(
        std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>>
            response,
        int32_t in_va_type,
        bool in_is_user_specific,
        const std::string& in_username,
        const std::string& in_key_name,
        const std::string& in_domain,
        const std::vector<uint8_t>& in_device_id,
        bool in_include_signed_public_key,
        const std::vector<uint8_t>& in_challenge,
        const base::Optional<std::string> in_key_name_for_spkac) {
  base::Optional<attestation::VAType> va_type;
  va_type = IntegerToVAType(in_va_type);
  if (!va_type.has_value()) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED,
                             "Requested VA type is not supported");
    return;
  }

  attestation::SignEnterpriseChallengeRequest request;
  request.set_va_type(va_type.value());
  request.set_key_label(in_key_name);
  if (in_is_user_specific) {
    request.set_username(in_username);
  }
  request.set_domain(in_domain);
  *request.mutable_device_id() = {in_device_id.begin(), in_device_id.end()};
  request.set_include_signed_public_key(in_include_signed_public_key);
  *request.mutable_challenge() = {in_challenge.begin(), in_challenge.end()};
  if (in_key_name_for_spkac) {
    request.set_key_name_for_spkac(in_key_name_for_spkac.value());
  }

  int async_id = HandleAsyncData<attestation::SignEnterpriseChallengeRequest,
                                 attestation::SignEnterpriseChallengeReply>(
      &attestation::SignEnterpriseChallengeReply::challenge_response, request,
      base::BindOnce(&org::chromium::AttestationProxyInterface::
                         SignEnterpriseChallengeAsync,
                     base::Unretained(attestation_proxy_)),
      kAttestationProxyTimeout.InMilliseconds());

  response->Return(async_id);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationSignSimpleChallenge(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name,
    const std::vector<uint8_t>& in_challenge) {
  attestation::SignSimpleChallengeRequest request;
  request.set_key_label(in_key_name);
  if (in_is_user_specific) {
    request.set_username(in_username);
  }
  *request.mutable_challenge() = {in_challenge.begin(), in_challenge.end()};

  int async_id = HandleAsyncData<attestation::SignSimpleChallengeRequest,
                                 attestation::SignSimpleChallengeReply>(
      &attestation::SignSimpleChallengeReply::challenge_response, request,
      base::BindOnce(
          &org::chromium::AttestationProxyInterface::SignSimpleChallengeAsync,
          base::Unretained(attestation_proxy_)),
      kAttestationProxyTimeout.InMilliseconds());

  response->Return(async_id);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetKeyPayload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
      response_shared(new SharedDBusMethodResponse<std::vector<uint8_t>, bool>(
          std::move(response)));

  attestation::GetKeyInfoRequest request;
  request.set_key_label(in_key_name);
  if (in_is_user_specific) {
    request.set_username(in_username);
  }

  attestation_proxy_->GetKeyInfoAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationGetKeyPayloadOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>,
                                                          bool>,
          base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetKeyPayloadOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
        response,
    const attestation::GetKeyInfoReply& reply) {
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    std::string error_msg =
        "TpmAttestationGetKeyPayload(): Attestation daemon returned status " +
        std::to_string(static_cast<int>(reply.status()));
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, error_msg);
    return;
  }
  std::vector<uint8_t> public_key(reply.payload().begin(),
                                  reply.payload().end());
  response->Return(
      public_key,
      reply.status() == attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationSetKeyPayload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name,
    const std::vector<uint8_t>& in_payload) {
  std::shared_ptr<SharedDBusMethodResponse<bool>> response_shared(
      new SharedDBusMethodResponse<bool>(std::move(response)));

  attestation::SetKeyPayloadRequest request;
  request.set_key_label(in_key_name);
  if (in_is_user_specific) {
    request.set_username(in_username);
  }
  *request.mutable_payload() = {in_payload.begin(), in_payload.end()};

  attestation_proxy_->SetKeyPayloadAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationSetKeyPayloadOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationSetKeyPayloadOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::SetKeyPayloadReply& reply) {
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    LOG(WARNING)
        << "TpmAttestationSetKeyPayload(): Attestation daemon returned status "
        << static_cast<int>(reply.status());
  }
  response->Return(reply.status() ==
                   attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationDeleteKeys(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_prefix) {
  std::shared_ptr<SharedDBusMethodResponse<bool>> response_shared(
      new SharedDBusMethodResponse<bool>(std::move(response)));

  attestation::DeleteKeysRequest request;
  request.set_key_label_match(in_key_prefix);
  request.set_match_behavior(
      attestation::DeleteKeysRequest::MATCH_BEHAVIOR_PREFIX);
  if (in_is_user_specific) {
    request.set_username(in_username);
  }

  attestation_proxy_->DeleteKeysAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmAttestationDeleteKeysOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationDeleteKeysOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::DeleteKeysReply& reply) {
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    LOG(WARNING)
        << "TpmAttestationDeleteKeys(): Attestation daemon returned status "
        << static_cast<int>(reply.status());
  }
  response->Return(reply.status() ==
                   attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationDeleteKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  std::shared_ptr<SharedDBusMethodResponse<bool>> response_shared(
      new SharedDBusMethodResponse<bool>(std::move(response)));

  attestation::DeleteKeysRequest request;
  request.set_key_label_match(in_key_name);
  request.set_match_behavior(
      attestation::DeleteKeysRequest::MATCH_BEHAVIOR_EXACT);
  if (in_is_user_specific) {
    request.set_username(in_username);
  }

  attestation_proxy_->DeleteKeysAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmAttestationDeleteKeysOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationDeleteKeyOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::DeleteKeysReply& reply) {
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    LOG(WARNING)
        << "TpmAttestationDeleteKey(): Attestation daemon returned status "
        << static_cast<int>(reply.status());
  }
  response->Return(reply.status() ==
                   attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetEK(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string, bool>>
        response) {
  std::shared_ptr<SharedDBusMethodResponse<std::string, bool>> response_shared(
      new SharedDBusMethodResponse<std::string, bool>(std::move(response)));

  attestation::GetEndorsementInfoRequest request;

  attestation_proxy_->GetEndorsementInfoAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetEKOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::string, bool>,
          base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetEKOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::string, bool>> response,
    const attestation::GetEndorsementInfoReply& reply) {
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    LOG(WARNING) << "TpmAttestationGetEK(): Attestation daemon returned status "
                 << static_cast<int>(reply.status());
  }
  response->Return(
      reply.ek_info(),
      reply.status() == attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationResetIdentity(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    const std::string& in_reset_token) {
  attestation::ResetIdentityRequest request;
  request.set_reset_token(in_reset_token);

  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>(
          std::move(response));
  attestation_proxy_->ResetIdentityAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationResetIdentityOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>,
                                                          bool>,
          base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationResetIdentityOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
        response,
    const attestation::ResetIdentityReply& reply) {
  std::vector<uint8_t> reset_request;
  if (reply.status() == attestation::AttestationStatus::STATUS_SUCCESS) {
    reset_request.assign(reply.reset_request().begin(),
                         reply.reset_request().end());
  } else {
    LOG(WARNING)
        << "TpmAttestationResetIdentity(): Attestation daemon returned status "
        << static_cast<int>(reply.status());
  }
  response->Return(
      reset_request,
      reply.status() == attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::TpmGetVersionStructured(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<uint32_t,
                                                           uint64_t,
                                                           uint32_t,
                                                           uint32_t,
                                                           uint64_t,
                                                           std::string>>
        response) {
  auto response_shared = std::make_shared<SharedDBusMethodResponse<
      uint32_t, uint64_t, uint32_t, uint32_t, uint64_t, std::string>>(
      std::move(response));

  tpm_manager::GetVersionInfoRequest request;
  tpm_ownership_proxy_->GetVersionInfoAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmGetVersionStructuredOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<
              uint32_t, uint64_t, uint32_t, uint32_t, uint64_t, std::string>,
          base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::TpmGetVersionStructuredOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<uint32_t,
                                             uint64_t,
                                             uint32_t,
                                             uint32_t,
                                             uint64_t,
                                             std::string>> response,
    const tpm_manager::GetVersionInfoReply& reply) {
  // Note that the TpmGetVersionSuccessStructured method in CryptohomeInterface
  // doesn't return any error, so we don't check reply.status() here.
  response->Return(reply.family(), reply.spec_level(), reply.manufacturer(),
                   reply.tpm_model(), reply.firmware_version(),
                   base::HexEncode(reply.vendor_specific().data(),
                                   reply.vendor_specific().size()));
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11IsTpmTokenReady(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::Pkcs11IsTpmTokenReadyRequest request;
  pkcs11_proxy_->Pkcs11IsTpmTokenReadyAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::Pkcs11IsTpmTokenReadyOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11IsTpmTokenReadyOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::Pkcs11IsTpmTokenReadyReply& reply) {
  response->Return(reply.ready());
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11GetTpmTokenInfo(
    std::unique_ptr<brillo::dbus_utils::
                        DBusMethodResponse<std::string, std::string, int32_t>>
        response) {
  auto response_shared = std::make_shared<
      SharedDBusMethodResponse<std::string, std::string, int32_t>>(
      std::move(response));

  user_data_auth::Pkcs11GetTpmTokenInfoRequest request;
  pkcs11_proxy_->Pkcs11GetTpmTokenInfoAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::Pkcs11GetTpmTokenInfoOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::string,
                                                          std::string, int32_t>,
          base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11GetTpmTokenInfoForUser(
    std::unique_ptr<brillo::dbus_utils::
                        DBusMethodResponse<std::string, std::string, int32_t>>
        response,
    const std::string& in_username) {
  auto response_shared = std::make_shared<
      SharedDBusMethodResponse<std::string, std::string, int32_t>>(
      std::move(response));

  user_data_auth::Pkcs11GetTpmTokenInfoRequest request;
  request.set_username(in_username);
  // Note that the response needed for Pkcs11GetTpmTokenInfo and
  // Pkcs11GetTpmTokenInfoForUser are the same, so we'll use the
  // Pkcs11GetTpmTokenInfo version here.
  pkcs11_proxy_->Pkcs11GetTpmTokenInfoAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::Pkcs11GetTpmTokenInfoOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::string,
                                                          std::string, int32_t>,
          base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11GetTpmTokenInfoOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::string, std::string, int32_t>>
        response,
    const user_data_auth::Pkcs11GetTpmTokenInfoReply& reply) {
  response->Return(reply.token_info().label(), reply.token_info().user_pin(),
                   reply.token_info().slot());
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11Terminate(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    const std::string& in_username) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<>>(std::move(response));

  user_data_auth::Pkcs11TerminateRequest request;
  request.set_username(in_username);
  pkcs11_proxy_->Pkcs11TerminateAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::Pkcs11TerminateOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11TerminateOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<>> response,
    const user_data_auth::Pkcs11TerminateReply& reply) {
  response->Return();
}

void LegacyCryptohomeInterfaceAdaptor::GetStatusString(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string>>
        response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::string>>(
          std::move(response));

  user_data_auth::GetStatusStringRequest request;
  misc_proxy_->GetStatusStringAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::GetStatusStringOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<std::string>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetStatusStringOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::string>> response,
    const user_data_auth::GetStatusStringReply& reply) {
  response->Return(reply.status());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesGet(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    const std::string& in_name) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>(
          std::move(response));

  user_data_auth::InstallAttributesGetRequest request;
  request.set_name(in_name);
  install_attributes_proxy_->InstallAttributesGetAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::InstallAttributesGetOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>,
                                                          bool>,
          base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesGetOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
        response,
    const user_data_auth::InstallAttributesGetReply& reply) {
  std::vector<uint8_t> result(reply.value().begin(), reply.value().end());
  bool success = (reply.error() == user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  response->Return(result, success);
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesSet(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    const std::string& in_name,
    const std::vector<uint8_t>& in_value) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::InstallAttributesSetRequest request;
  request.set_name(in_name);
  *request.mutable_value() = {in_value.begin(), in_value.end()};
  install_attributes_proxy_->InstallAttributesSetAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::InstallAttributesSetOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesSetOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::InstallAttributesSetReply& reply) {
  bool success = (reply.error() == user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  response->Return(success);
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesCount(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<int32_t>>(std::move(response));

  user_data_auth::InstallAttributesGetStatusRequest request;
  install_attributes_proxy_->InstallAttributesGetStatusAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::InstallAttributesCountOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<int32_t>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesCountOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<int32_t>> response,
    const user_data_auth::InstallAttributesGetStatusReply& reply) {
  response->Return(reply.count());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesFinalize(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::InstallAttributesFinalizeRequest request;
  install_attributes_proxy_->InstallAttributesFinalizeAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::InstallAttributesFinalizeOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesFinalizeOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::InstallAttributesFinalizeReply& reply) {
  bool success = (reply.error() == user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  response->Return(success);
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsReady(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::InstallAttributesGetStatusRequest request;
  install_attributes_proxy_->InstallAttributesGetStatusAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsReadyOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsReadyOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::InstallAttributesGetStatusReply& reply) {
  bool ready =
      (reply.state() != user_data_auth::InstallAttributesState::UNKNOWN &&
       reply.state() != user_data_auth::InstallAttributesState::TPM_NOT_OWNED);
  response->Return(ready);
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsSecure(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::InstallAttributesGetStatusRequest request;
  install_attributes_proxy_->InstallAttributesGetStatusAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsSecureOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsSecureOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::InstallAttributesGetStatusReply& reply) {
  response->Return(reply.is_secure());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsInvalid(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::InstallAttributesGetStatusRequest request;
  install_attributes_proxy_->InstallAttributesGetStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     InstallAttributesIsInvalidOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsInvalidOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::InstallAttributesGetStatusReply& reply) {
  bool is_invalid =
      (reply.state() == user_data_auth::InstallAttributesState::INVALID);
  response->Return(is_invalid);
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsFirstInstall(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::InstallAttributesGetStatusRequest request;
  install_attributes_proxy_->InstallAttributesGetStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     InstallAttributesIsFirstInstallOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsFirstInstallOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::InstallAttributesGetStatusReply& reply) {
  bool is_first_install =
      (reply.state() == user_data_auth::InstallAttributesState::FIRST_INSTALL);
  response->Return(is_first_install);
}

void LegacyCryptohomeInterfaceAdaptor::SignBootLockbox(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::SignBootLockboxRequest& in_request) {
  // Note that this version of Boot Lockbox is deprecated for security and
  // performance issue. Please use the version in bootlockboxd instead.
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Deprecated method SignBootLockbox() called");
}

void LegacyCryptohomeInterfaceAdaptor::VerifyBootLockbox(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::VerifyBootLockboxRequest& in_request) {
  // Note that this version of Boot Lockbox is deprecated for security and
  // performance issue. Please use the version in bootlockboxd instead.
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Deprecated method VerifyBootLockbox() called");
}

void LegacyCryptohomeInterfaceAdaptor::FinalizeBootLockbox(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::FinalizeBootLockboxRequest& in_request) {
  // Note that this version of Boot Lockbox is deprecated for security and
  // performance issue. Please use the version in bootlockboxd instead.
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Deprecated method FinalizeBootLockbox() called");
}

void LegacyCryptohomeInterfaceAdaptor::GetBootAttribute(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetBootAttributeRequest& in_request) {
  // BootAttribute series methods are no longer available.
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Obsolete method GetBootAttribute() called");
}

void LegacyCryptohomeInterfaceAdaptor::SetBootAttribute(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::SetBootAttributeRequest& in_request) {
  // BootAttribute series methods are no longer available.
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Obsolete method SetBootAttribute() called");
}

void LegacyCryptohomeInterfaceAdaptor::FlushAndSignBootAttributes(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::FlushAndSignBootAttributesRequest& in_request) {
  // BootAttribute series methods are no longer available.
  response->ReplyWithError(
      FROM_HERE, brillo::errors::dbus::kDomain, DBUS_ERROR_NOT_SUPPORTED,
      "Obsolete method FlushAndSignBootAttributes() called");
}

void LegacyCryptohomeInterfaceAdaptor::GetLoginStatus(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetLoginStatusRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::GetLoginStatusRequest request;
  misc_proxy_->GetLoginStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::GetLoginStatusOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetLoginStatusOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::GetLoginStatusReply& reply) {
  cryptohome::BaseReply result;
  result.set_error(static_cast<cryptohome::CryptohomeErrorCode>(reply.error()));
  auto* extension =
      result.MutableExtension(cryptohome::GetLoginStatusReply::reply);
  extension->set_owner_user_exists(reply.owner_user_exists());

  // See definition of user_data_auth::GetLoginStatusReply for more information
  // on why |boot_lockbox_finalized| is deprecated.
  // Note that it's set to a false value here to ensure clients that expect this
  // field continues to work.
  extension->set_boot_lockbox_finalized(false);

  ClearErrorIfNotSet(&result);
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::GetTpmStatus(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetTpmStatusRequest& in_request) {
  // This method requires the output of more than 1 method and thus is divided
  // into various parts:
  // - TpmManager stage: Calls GetTpmStatus() in tpm_manager
  // - DictionaryAttack stage: Calls GetDictionaryAttackInfo() in tpm_manager
  // - InstallAttributes stage: Calls InstallAttributesGetStatus() in
  // UserDataAuth
  // - Attestation stage: Calls GetStatus() in attestation
  // The 4 stages is executed back to back according to the sequence listed
  // above. After all of them are done, we'll take their results and form the
  // response for this method call.
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  tpm_manager::GetTpmStatusRequest request;
  tpm_ownership_proxy_->GetTpmStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     GetTpmStatusOnStageOwnershipStatusDone,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetTpmStatusOnStageOwnershipStatusDone(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const tpm_manager::GetTpmStatusReply& status_reply) {
  if (status_reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << "GetTpmStatus() failed to call GetTpmStatus in tpm_manager, "
                  "error status "
               << static_cast<int>(status_reply.status());
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, "GetTpmStatus() failed");
    return;
  }

  BaseReply reply;
  GetTpmStatusReply* extension =
      reply.MutableExtension(cryptohome::GetTpmStatusReply::reply);
  extension->set_enabled(status_reply.enabled());
  extension->set_owned(status_reply.owned());
  if (!status_reply.local_data().owner_password().empty()) {
    extension->set_initialized(false);
    extension->set_owner_password(status_reply.local_data().owner_password());
  } else {
    // Initialized is true only when the TPM is owned and the owner password has
    // already been destroyed.
    extension->set_initialized(extension->owned());
  }

  bool has_reset_lock_permissions = true;
  if (status_reply.local_data().owner_password().empty()) {
    if (status_reply.local_data().lockout_password().empty() &&
        !status_reply.local_data().has_owner_delegate()) {
      has_reset_lock_permissions = false;
    } else if (status_reply.local_data().has_owner_delegate() &&
               !status_reply.local_data()
                    .owner_delegate()
                    .has_reset_lock_permissions()) {
      has_reset_lock_permissions = false;
    }
  }
  extension->set_has_reset_lock_permissions(has_reset_lock_permissions);

  tpm_manager::GetDictionaryAttackInfoRequest request;
  tpm_ownership_proxy_->GetDictionaryAttackInfoAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     GetTpmStatusOnStageDictionaryAttackDone,
                 base::Unretained(this), response, reply),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetTpmStatusOnStageDictionaryAttackDone(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    BaseReply reply,
    const tpm_manager::GetDictionaryAttackInfoReply& da_reply) {
  // Note that it is intentional that we do not fail even if
  // GetDictionaryAttackInfo() fails. This failure is logged as an error, but
  // not acted upon.

  GetTpmStatusReply* extension =
      reply.MutableExtension(cryptohome::GetTpmStatusReply::reply);
  if (da_reply.status() == tpm_manager::STATUS_SUCCESS) {
    extension->set_dictionary_attack_counter(
        da_reply.dictionary_attack_counter());
    extension->set_dictionary_attack_threshold(
        da_reply.dictionary_attack_threshold());
    extension->set_dictionary_attack_lockout_in_effect(
        da_reply.dictionary_attack_lockout_in_effect());
    extension->set_dictionary_attack_lockout_seconds_remaining(
        da_reply.dictionary_attack_lockout_seconds_remaining());
  } else {
    LOG(ERROR) << "Failed to call GetDictionaryAttackInfo() in GetTpmStatus(), "
                  "error status "
               << static_cast<int>(da_reply.status());
  }

  user_data_auth::InstallAttributesGetStatusRequest request;
  install_attributes_proxy_->InstallAttributesGetStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     GetTpmStatusOnStageInstallAttributesDone,
                 base::Unretained(this), response, reply),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetTpmStatusOnStageInstallAttributesDone(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    BaseReply reply,
    const user_data_auth::InstallAttributesGetStatusReply& install_attr_reply) {
  if (install_attr_reply.error() != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "GetTpmStatus() failed to call InstallAttributesGetStatus in "
                  "UserDataAuth, error status "
               << static_cast<int>(install_attr_reply.error());
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED,
                             "InstallAttributesGetStatus() failed");
    return;
  }

  GetTpmStatusReply* extension =
      reply.MutableExtension(cryptohome::GetTpmStatusReply::reply);

  extension->set_install_lockbox_finalized(
      extension->owned() && install_attr_reply.state() ==
                                user_data_auth::InstallAttributesState::VALID);

  // Set up the parameters for GetStatus() in attestationd.
  attestation::GetStatusRequest request;
  request.set_extended_status(true);

  attestation_proxy_->GetStatusAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::GetTpmStatusOnStageAttestationDone,
          base::Unretained(this), response, reply),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetTpmStatusOnStageAttestationDone(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    BaseReply reply,
    const attestation::GetStatusReply& attestation_reply) {
  GetTpmStatusReply* extension =
      reply.MutableExtension(cryptohome::GetTpmStatusReply::reply);

  extension->set_boot_lockbox_finalized(false);
  extension->set_is_locked_to_single_user(platform_->FileExists(
      base::FilePath(cryptohome::kLockedToSingleUserFile)));

  if (attestation_reply.status() ==
      attestation::AttestationStatus::STATUS_SUCCESS) {
    extension->set_attestation_prepared(
        attestation_reply.prepared_for_enrollment());
    extension->set_attestation_enrolled(attestation_reply.enrolled());
    extension->set_verified_boot_measured(attestation_reply.verified_boot());
    for (auto it = attestation_reply.identities().cbegin(),
              end = attestation_reply.identities().cend();
         it != end; ++it) {
      auto* identity = extension->mutable_identities()->Add();
      identity->set_features(it->features());
    }
    for (auto it = attestation_reply.identity_certificates().cbegin(),
              end = attestation_reply.identity_certificates().cend();
         it != end; ++it) {
      GetTpmStatusReply::IdentityCertificate identity_certificate;
      identity_certificate.set_identity(it->second.identity());
      identity_certificate.set_aca(it->second.aca());
      extension->mutable_identity_certificates()->insert(
          google::protobuf::Map<int, GetTpmStatusReply::IdentityCertificate>::
              value_type(it->first, identity_certificate));
    }
  } else {
    LOG(ERROR) << "Failed to call GetStatus() in attestation during "
                  "GetTpmStatus(), error status "
               << static_cast<int>(attestation_reply.status());

    extension->set_attestation_prepared(false);
    extension->set_attestation_enrolled(false);
    extension->set_verified_boot_measured(false);
  }

  ClearErrorIfNotSet(&reply);
  response->Return(reply);
}

void LegacyCryptohomeInterfaceAdaptor::GetEndorsementInfo(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetEndorsementInfoRequest& in_request) {
  std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>>
      response_shared(new SharedDBusMethodResponse<cryptohome::BaseReply>(
          std::move(response)));

  attestation::GetEndorsementInfoRequest request;

  attestation_proxy_->GetEndorsementInfoAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::GetEndorsementInfoOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kAttestationProxyTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetEndorsementInfoOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const attestation::GetEndorsementInfoReply& reply) {
  cryptohome::BaseReply result;
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    LOG(WARNING) << "GetEndorsementInfo(): Attestation daemon returned status "
                 << static_cast<int>(reply.status());
    result.set_error(cryptohome::CRYPTOHOME_ERROR_TPM_EK_NOT_AVAILABLE);
  } else {
    GetEndorsementInfoReply* extension =
        result.MutableExtension(cryptohome::GetEndorsementInfoReply::reply);
    extension->set_ek_public_key(reply.ek_public_key());
    if (!reply.ek_certificate().empty()) {
      extension->set_ek_certificate(reply.ek_certificate());
    }
  }
  ClearErrorIfNotSet(&result);
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::InitializeCastKey(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::InitializeCastKeyRequest& in_request) {
  // InitializeCastKey() is no longer available.
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Obsolete method InitializeCastKey() called");
}

void LegacyCryptohomeInterfaceAdaptor::StartFingerprintAuthSession(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::StartFingerprintAuthSessionRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::StartFingerprintAuthSessionRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  userdataauth_proxy_->StartFingerprintAuthSessionAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::StartFingerprintAuthSessionReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::EndFingerprintAuthSession(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::EndFingerprintAuthSessionRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::EndFingerprintAuthSessionRequest request;
  userdataauth_proxy_->EndFingerprintAuthSessionAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::EndFingerprintAuthSessionReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetWebAuthnSecret(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::GetWebAuthnSecretRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::GetWebAuthnSecretRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  userdataauth_proxy_->GetWebAuthnSecretAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::GetWebAuthnSecretOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::GetWebAuthnSecretOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::GetWebAuthnSecretReply& reply) {
  cryptohome::BaseReply result;
  result.set_error(static_cast<cryptohome::CryptohomeErrorCode>(reply.error()));
  cryptohome::GetWebAuthnSecretReply* result_extension =
      result.MutableExtension(cryptohome::GetWebAuthnSecretReply::reply);
  if (result.error() == CRYPTOHOME_ERROR_NOT_SET) {
    result_extension->set_webauthn_secret(reply.webauthn_secret());
  }
  ClearErrorIfNotSet(&result);
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::GetFirmwareManagementParameters(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetFirmwareManagementParametersRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::GetFirmwareManagementParametersRequest request;
  install_attributes_proxy_->GetFirmwareManagementParametersAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     GetFirmwareManagementParametersOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetFirmwareManagementParametersOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::GetFirmwareManagementParametersReply& reply) {
  cryptohome::BaseReply result;
  result.set_error(static_cast<cryptohome::CryptohomeErrorCode>(reply.error()));
  cryptohome::GetFirmwareManagementParametersReply* result_extension =
      result.MutableExtension(
          cryptohome::GetFirmwareManagementParametersReply::reply);
  result_extension->set_flags(reply.fwmp().flags());
  *result_extension->mutable_developer_key_hash() =
      reply.fwmp().developer_key_hash();
  ClearErrorIfNotSet(&result);
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::SetFirmwareManagementParameters(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::SetFirmwareManagementParametersRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::SetFirmwareManagementParametersRequest request;
  request.mutable_fwmp()->set_flags(in_request.flags());
  *request.mutable_fwmp()->mutable_developer_key_hash() =
      in_request.developer_key_hash();
  install_attributes_proxy_->SetFirmwareManagementParametersAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::SetFirmwareManagementParametersReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::RemoveFirmwareManagementParameters(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::RemoveFirmwareManagementParametersRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::RemoveFirmwareManagementParametersRequest request;
  install_attributes_proxy_->RemoveFirmwareManagementParametersAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::RemoveFirmwareManagementParametersReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::MigrateToDircrypto(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::MigrateToDircryptoRequest& in_migrate_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<>>(std::move(response));

  user_data_auth::StartMigrateToDircryptoRequest request;
  *request.mutable_account_id() = in_account_id;
  request.set_minimal_migration(in_migrate_request.minimal_migration());
  userdataauth_proxy_->StartMigrateToDircryptoAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::MigrateToDircryptoOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::MigrateToDircryptoOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<>> response,
    const user_data_auth::StartMigrateToDircryptoReply& reply) {
  if (reply.error() != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(WARNING) << "StartMigrateToDircryptoAsync() failed with error code "
                 << static_cast<int>(reply.error());
  }
  response->Return();
}

void LegacyCryptohomeInterfaceAdaptor::NeedsDircryptoMigration(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    const cryptohome::AccountIdentifier& in_account_id) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::NeedsDircryptoMigrationRequest request;
  *request.mutable_account_id() = in_account_id;
  userdataauth_proxy_->NeedsDircryptoMigrationAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::NeedsDircryptoMigrationOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::NeedsDircryptoMigrationOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::NeedsDircryptoMigrationReply& reply) {
  if (reply.error() != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    // There's an error, we should return an error.
    LOG(ERROR) << "NeedsDircryptoMigration returned "
               << static_cast<int>(reply.error());
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED,
                             "An error occurred on the UserDataAuth side when "
                             "proxying NeedsDircryptoMigration.");
    return;
  }
  response->Return(reply.needs_dircrypto_migration());
}

void LegacyCryptohomeInterfaceAdaptor::GetSupportedKeyPolicies(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetSupportedKeyPoliciesRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::GetSupportedKeyPoliciesRequest request;
  userdataauth_proxy_->GetSupportedKeyPoliciesAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::GetSupportedKeyPoliciesOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetSupportedKeyPoliciesOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::GetSupportedKeyPoliciesReply& reply) {
  cryptohome::BaseReply base_reply;
  cryptohome::GetSupportedKeyPoliciesReply* extension =
      base_reply.MutableExtension(
          cryptohome::GetSupportedKeyPoliciesReply::reply);

  extension->set_low_entropy_credentials(
      reply.low_entropy_credentials_supported());
  ClearErrorIfNotSet(&base_reply);
  response->Return(base_reply);
}

void LegacyCryptohomeInterfaceAdaptor::IsQuotaSupported(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::GetArcDiskFeaturesRequest request;
  arc_quota_proxy_->GetArcDiskFeaturesAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::IsQuotaSupportedOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::IsQuotaSupportedOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::GetArcDiskFeaturesReply& reply) {
  response->Return(reply.quota_supported());
}

void LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForUid(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int64_t>> response,
    uint32_t in_uid) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<int64_t>>(std::move(response));

  user_data_auth::GetCurrentSpaceForArcUidRequest request;
  request.set_uid(in_uid);
  arc_quota_proxy_->GetCurrentSpaceForArcUidAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForUidOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<int64_t>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForUidOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<int64_t>> response,
    const user_data_auth::GetCurrentSpaceForArcUidReply& reply) {
  response->Return(reply.cur_space());
}

void LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForGid(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int64_t>> response,
    uint32_t in_gid) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<int64_t>>(std::move(response));

  user_data_auth::GetCurrentSpaceForArcGidRequest request;
  request.set_gid(in_gid);
  arc_quota_proxy_->GetCurrentSpaceForArcGidAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForGidOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<int64_t>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForGidOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<int64_t>> response,
    const user_data_auth::GetCurrentSpaceForArcGidReply& reply) {
  response->Return(reply.cur_space());
}

void LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForProjectId(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int64_t>> response,
    uint32_t in_project_id) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<int64_t>>(std::move(response));

  user_data_auth::GetCurrentSpaceForArcProjectIdRequest request;
  request.set_project_id(in_project_id);
  arc_quota_proxy_->GetCurrentSpaceForArcProjectIdAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     GetCurrentSpaceForProjectIdOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<int64_t>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForProjectIdOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<int64_t>> response,
    const user_data_auth::GetCurrentSpaceForArcProjectIdReply& reply) {
  response->Return(reply.cur_space());
}

void LegacyCryptohomeInterfaceAdaptor::LockToSingleUserMountUntilReboot(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::LockToSingleUserMountUntilRebootRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::LockToSingleUserMountUntilRebootRequest request;
  if (in_request.has_account_id()) {
    *request.mutable_account_id() = in_request.account_id();
  }
  misc_proxy_->LockToSingleUserMountUntilRebootAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     LockToSingleUserMountUntilRebootOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::
    LockToSingleUserMountUntilRebootOnSuccess(
        std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>>
            response,
        const user_data_auth::LockToSingleUserMountUntilRebootReply& reply) {
  cryptohome::BaseReply result;
  cryptohome::LockToSingleUserMountUntilRebootReply* result_extension =
      result.MutableExtension(
          cryptohome::LockToSingleUserMountUntilRebootReply::reply);
  if (reply.error() == user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_READ_PCR) {
    result_extension->set_result(cryptohome::FAILED_TO_READ_PCR);
    result.set_error(cryptohome::CRYPTOHOME_ERROR_TPM_COMM_ERROR);
  } else if (reply.error() ==
             user_data_auth::CRYPTOHOME_ERROR_PCR_ALREADY_EXTENDED) {
    result_extension->set_result(cryptohome::PCR_ALREADY_EXTENDED);
  } else if (reply.error() ==
             user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_EXTEND_PCR) {
    result_extension->set_result(cryptohome::FAILED_TO_EXTEND_PCR);
    result.set_error(cryptohome::CRYPTOHOME_ERROR_TPM_COMM_ERROR);
  } else if (reply.error() == user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    result_extension->set_result(cryptohome::SUCCESS);
  } else {
    LOG(DFATAL) << "Invalid error code returned by "
                   "LockToSingleUserMountUntilReboot() in UserDataAuth";
    result.ClearExtension(
        cryptohome::LockToSingleUserMountUntilRebootReply::reply);
  }
  ClearErrorIfNotSet(&result);
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::GetRsuDeviceId(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetRsuDeviceIdRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::GetRsuDeviceIdRequest request;
  misc_proxy_->GetRsuDeviceIdAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::GetRsuDeviceIdOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::GetRsuDeviceIdOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::GetRsuDeviceIdReply& reply) {
  cryptohome::BaseReply result;
  result.set_error(static_cast<cryptohome::CryptohomeErrorCode>(reply.error()));
  if (reply.error() == user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    cryptohome::GetRsuDeviceIdReply* result_extension =
        result.MutableExtension(cryptohome::GetRsuDeviceIdReply::reply);
    *result_extension->mutable_rsu_device_id() = reply.rsu_device_id();
  }
  ClearErrorIfNotSet(&result);
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::CheckHealth(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::CheckHealthRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::CheckHealthRequest request;
  misc_proxy_->CheckHealthAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::CheckHealthOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared),
      kDefaultTimeout.InMilliseconds());
}

void LegacyCryptohomeInterfaceAdaptor::CheckHealthOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::CheckHealthReply& reply) {
  cryptohome::BaseReply result;
  CheckHealthReply* reply_extension =
      result.MutableExtension(CheckHealthReply::reply);
  reply_extension->set_requires_powerwash(reply.requires_powerwash());
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::OnDircryptoMigrationProgressSignal(
    const user_data_auth::DircryptoMigrationProgress& progress) {
  VirtualSendDircryptoMigrationProgressSignal(
      dircrypto_data_migrator::MigrationHelper::ConvertDircryptoMigrationStatus(
          progress.status()),
      progress.current_bytes(), progress.total_bytes());
}

void LegacyCryptohomeInterfaceAdaptor::OnLowDiskSpaceSignal(
    const user_data_auth::LowDiskSpace& payload) {
  VirtualSendLowDiskSpaceSignal(payload.disk_free_bytes());
}

void LegacyCryptohomeInterfaceAdaptor::OnOwnershipTakenSignal(
    const tpm_manager::OwnershipTakenSignal& /*payload*/) {
  // Since the ownership taken signal is only sent when the ownership is taken,
  // it implies `is_ready` and `is_owned`. We also hardcode
  // `was_owned_this_boot` to be `false`; it's not used anymore so we don't
  // bother maintaining it in tpm manager.
  SendTpmInitStatusSignal(/*is_ready=*/true, /*is_owned=*/true,
                          /*was_owned_this_boot=*/false);
}

void LegacyCryptohomeInterfaceAdaptor::OnSignalConnectedHandler(
    const std::string& interface, const std::string& signal, bool success) {
  if (!success) {
    LOG(ERROR)
        << "Failure to connect DBus signal in cryptohome-proxy, interface="
        << interface << ", signal=" << signal;
  }
}

// A helper function which maps an integer to a valid CertificateProfile.
attestation::CertificateProfile
LegacyCryptohomeInterfaceAdaptor::IntegerToCertificateProfile(
    int profile_value) {
  // The protobuf compiler generates the _IsValid function.
  if (!attestation::CertificateProfile_IsValid(profile_value)) {
    return attestation::CertificateProfile::ENTERPRISE_USER_CERTIFICATE;
  }
  return static_cast<attestation::CertificateProfile>(profile_value);
}

// A helper function which maps an integer to a valid ACAType.
base::Optional<attestation::ACAType>
LegacyCryptohomeInterfaceAdaptor::IntegerToACAType(int type) {
  if (!attestation::ACAType_IsValid(type)) {
    return base::nullopt;
  }
  return static_cast<attestation::ACAType>(type);
}

// A helper function which maps an integer to a valid VAType.
base::Optional<attestation::VAType>
LegacyCryptohomeInterfaceAdaptor::IntegerToVAType(int type) {
  if (!attestation::VAType_IsValid(type)) {
    return base::nullopt;
  }
  return static_cast<attestation::VAType>(type);
}

void LegacyCryptohomeInterfaceAdaptor::ClearErrorIfNotSet(
    cryptohome::BaseReply* reply) {
  if (reply->has_error() &&
      reply->error() == cryptohome::CRYPTOHOME_ERROR_NOT_SET) {
    reply->clear_error();
  }
}

}  // namespace cryptohome
