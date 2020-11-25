// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_PROXY_LEGACY_CRYPTOHOME_INTERFACE_ADAPTOR_H_
#define CRYPTOHOME_PROXY_LEGACY_CRYPTOHOME_INTERFACE_ADAPTOR_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <attestation/proto_bindings/interface.pb.h>
#include <attestation-client/attestation/dbus-proxies.h>
#include <base/atomic_sequence_num.h>
#include <base/location.h>
#include <base/optional.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "cryptohome/platform.h"
#include "cryptohome/rpc.pb.h"
#include "cryptohome/UserDataAuth.pb.h"
#include "dbus_adaptors/org.chromium.CryptohomeInterface.h"  // NOLINT(build/include_alpha)
#include "user_data_auth/dbus-proxies.h"
// The dbus_adaptor and proxy include must happen after the protobuf include

namespace cryptohome {

// This class is for holding a DBusMethodResponse in a std::shared_ptr, so that
// we can bind it to two separate callback.
template <typename... Types>
class SharedDBusMethodResponse {
 public:
  explicit SharedDBusMethodResponse(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<Types...>>
          response)
      : response_(std::move(response)) {}

  void ReplyWithError(const brillo::Error* error) {
    CHECK(response_) << "ReplyWithError() called after response has been sent";
    response_->ReplyWithError(error);
    response_.reset(nullptr);
  }

  void ReplyWithError(const base::Location& location,
                      const std::string& error_domain,
                      const std::string& error_code,
                      const std::string& error_message) {
    CHECK(response_) << "ReplyWithError() (4 parameter version) called after "
                        "response has been sent";
    response_->ReplyWithError(location, error_domain, error_code,
                              error_message);
    response_.reset(nullptr);
  }

  void Return(const Types&... return_values) {
    CHECK(response_) << "Return() called after response has been sent";
    response_->Return(return_values...);
    response_.reset(nullptr);
  }

 private:
  std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<Types...>> response_;
};

class LegacyCryptohomeInterfaceAdaptor
    : public org::chromium::CryptohomeInterfaceInterface,
      public org::chromium::CryptohomeInterfaceAdaptor {
 public:
  explicit LegacyCryptohomeInterfaceAdaptor(
      scoped_refptr<dbus::Bus> bus, brillo::dbus_utils::DBusObject* dbus_object)
      : org::chromium::CryptohomeInterfaceAdaptor(this),
        dbus_object_(dbus_object),
        default_attestation_proxy_(new org::chromium::AttestationProxy(bus)),
        attestation_proxy_(default_attestation_proxy_.get()),
        default_tpm_ownership_proxy_(new org::chromium::TpmManagerProxy(bus)),
        tpm_ownership_proxy_(default_tpm_ownership_proxy_.get()),
        default_tpm_nvram_proxy_(new org::chromium::TpmNvramProxy(bus)),
        tpm_nvram_proxy_(default_tpm_nvram_proxy_.get()),
        default_userdataauth_proxy_(
            new org::chromium::UserDataAuthInterfaceProxy(bus)),
        userdataauth_proxy_(default_userdataauth_proxy_.get()),
        default_arc_quota_proxy_(new org::chromium::ArcQuotaProxy(bus)),
        arc_quota_proxy_(default_arc_quota_proxy_.get()),
        default_pkcs11_proxy_(
            new org::chromium::CryptohomePkcs11InterfaceProxy(bus)),
        pkcs11_proxy_(default_pkcs11_proxy_.get()),
        default_install_attributes_proxy_(
            new org::chromium::InstallAttributesInterfaceProxy(bus)),
        install_attributes_proxy_(default_install_attributes_proxy_.get()),
        default_misc_proxy_(
            new org::chromium::CryptohomeMiscInterfaceProxy(bus)),
        misc_proxy_(default_misc_proxy_.get()),
        default_platform_(new Platform()),
        platform_(default_platform_.get()) {}

  void RegisterAsync();

  // The actual dbus methods
  void IsMounted(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>>
                     response) override;
  void IsMountedForUser(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool, bool>>
          response,
      const std::string& in_username) override;
  void ListKeysEx(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AccountIdentifier& in_account_id,
      const cryptohome::AuthorizationRequest& in_authorization_request,
      const cryptohome::ListKeysRequest& in_list_keys_request) override;
  void CheckKeyEx(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AccountIdentifier& in_account_id,
      const cryptohome::AuthorizationRequest& in_authorization_request,
      const cryptohome::CheckKeyRequest& in_check_key_request) override;
  void RemoveKeyEx(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AccountIdentifier& in_account_id,
      const cryptohome::AuthorizationRequest& in_authorization_request,
      const cryptohome::RemoveKeyRequest& in_remove_key_request) override;
  void MassRemoveKeys(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AccountIdentifier& in_account_id,
      const cryptohome::AuthorizationRequest& in_authorization_request,
      const cryptohome::MassRemoveKeysRequest& in_mass_remove_keys_request)
      override;
  void GetKeyDataEx(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AccountIdentifier& in_account_id,
      const cryptohome::AuthorizationRequest& in_authorization_request,
      const cryptohome::GetKeyDataRequest& in_get_key_data_request) override;
  void MigrateKeyEx(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AccountIdentifier& in_account,
      const cryptohome::AuthorizationRequest& in_authorization_request,
      const cryptohome::MigrateKeyRequest& in_migrate_request) override;
  void AddKeyEx(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AccountIdentifier& in_account_id,
      const cryptohome::AuthorizationRequest& in_authorization_request,
      const cryptohome::AddKeyRequest& in_add_key_request) override;
  void AddDataRestoreKey(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                             cryptohome::BaseReply>> response,
                         const cryptohome::AccountIdentifier& in_account_id,
                         const cryptohome::AuthorizationRequest&
                             in_authorization_request) override;
  void RemoveEx(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                    cryptohome::BaseReply>> response,
                const cryptohome::AccountIdentifier& in_account) override;
  void GetSystemSalt(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                         std::vector<uint8_t>>> response) override;
  void GetSanitizedUsername(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string>>
          response,
      const std::string& in_username) override;
  void MountEx(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                   cryptohome::BaseReply>> response,
               const cryptohome::AccountIdentifier& in_account_id,
               const cryptohome::AuthorizationRequest& in_authorization_request,
               const cryptohome::MountRequest& in_mount_request) override;
  void MountGuestEx(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                        cryptohome::BaseReply>> response,
                    const cryptohome::MountGuestRequest& in_request) override;
  void RenameCryptohome(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AccountIdentifier& in_cryptohome_id_from,
      const cryptohome::AccountIdentifier& in_cryptohome_id_to) override;
  void GetAccountDiskUsage(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AccountIdentifier& in_account_id) override;
  void UnmountEx(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                     cryptohome::BaseReply>> response,
                 const cryptohome::UnmountRequest& in_request) override;
  void UpdateCurrentUserActivityTimestamp(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
      int32_t in_time_shift_sec) override;
  void TpmIsReady(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>>
                      response) override;
  void TpmIsEnabled(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void TpmGetPassword(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string>>
          response) override;
  void TpmIsOwned(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>>
                      response) override;
  void TpmIsBeingOwned(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void TpmCanAttemptOwnership(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response)
      override;
  void TpmClearStoredPassword(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response)
      override;
  void TpmIsAttestationPrepared(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void TpmAttestationGetEnrollmentPreparationsEx(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AttestationGetEnrollmentPreparationsRequest& in_request)
      override;
  void TpmVerifyAttestationData(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      bool in_is_cros_core) override;
  void TpmVerifyEK(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      bool in_is_cros_core) override;
  void TpmAttestationCreateEnrollRequest(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          std::vector<uint8_t>>> response,
      int32_t in_pca_type) override;
  void AsyncTpmAttestationCreateEnrollRequest(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
      int32_t in_pca_type) override;
  void TpmAttestationEnroll(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      int32_t in_pca_type,
      const std::vector<uint8_t>& in_pca_response) override;
  void AsyncTpmAttestationEnroll(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
      int32_t in_pca_type,
      const std::vector<uint8_t>& in_pca_response) override;
  void TpmAttestationEnrollEx(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      int32_t in_pca_type,
      bool in_forced) override;
  void AsyncTpmAttestationEnrollEx(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
      int32_t in_pca_type,
      bool in_forced) override;
  void TpmAttestationCreateCertRequest(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          std::vector<uint8_t>>> response,
      int32_t in_pca_type,
      int32_t in_certificate_profile,
      const std::string& in_username,
      const std::string& in_request_origin) override;
  void AsyncTpmAttestationCreateCertRequest(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
      int32_t in_pca_type,
      int32_t in_certificate_profile,
      const std::string& in_username,
      const std::string& in_request_origin) override;
  void TpmAttestationFinishCertRequest(
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      const std::vector<uint8_t>& in_pca_response,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name) override;
  void AsyncTpmAttestationFinishCertRequest(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
      const std::vector<uint8_t>& in_pca_response,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name) override;
  void TpmAttestationGetCertificateEx(
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      int32_t in_certificate_profile,
      const std::string& in_username,
      const std::string& in_request_origin,
      int32_t in_pca_type,
      int32_t in_key_type,
      const std::string& in_key_name,
      bool in_forced,
      bool in_shall_trigger_enrollment) override;
  void AsyncTpmAttestationGetCertificateEx(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
      int32_t in_certificate_profile,
      const std::string& in_username,
      const std::string& in_request_origin,
      int32_t in_pca_type,
      int32_t in_key_type,
      const std::string& in_key_name,
      bool in_forced,
      bool in_shall_trigger_enrollment) override;
  void TpmIsAttestationEnrolled(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void TpmAttestationDoesKeyExist(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name) override;
  void TpmAttestationGetCertificate(
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name) override;
  void TpmAttestationGetPublicKey(
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name) override;
  void TpmAttestationGetEnrollmentId(
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      bool in_ignore_cache) override;

  // NOTE: Despite its name, it's an async method that will emit signal when
  // finished.
  void TpmAttestationRegisterKey(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name) override;

  void TpmAttestationSignEnterpriseChallenge(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name,
      const std::string& in_domain,
      const std::vector<uint8_t>& in_device_id,
      bool in_include_signed_public_key,
      const std::vector<uint8_t>& in_challenge) override;
  void TpmAttestationSignEnterpriseVaChallenge(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
      int32_t in_va_type,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name,
      const std::string& in_domain,
      const std::vector<uint8_t>& in_device_id,
      bool in_include_signed_public_key,
      const std::vector<uint8_t>& in_challenge) override;
  void TpmAttestationSignEnterpriseVaChallengeV2(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
      int32_t in_va_type,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name,
      const std::string& in_domain,
      const std::vector<uint8_t>& in_device_id,
      bool in_include_signed_public_key,
      const std::vector<uint8_t>& in_challenge,
      const std::string& in_key_name_for_spkac) override;

  // Helper for reducing code duplication between
  // TpmAttestationSignEnterpriseVaChallengeV2,
  // TpmAttestationSignEnterpriseVaChallenge and
  // TpmAttestationSignEnterpriseChallenge.
  void TpmAttestationSignEnterpriseVaChallengeV2Actual(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
      int32_t in_va_type,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name,
      const std::string& in_domain,
      const std::vector<uint8_t>& in_device_id,
      bool in_include_signed_public_key,
      const std::vector<uint8_t>& in_challenge,
      const base::Optional<std::string> in_key_name_for_spkac);

  void TpmAttestationSignSimpleChallenge(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name,
      const std::vector<uint8_t>& in_challenge) override;
  void TpmAttestationGetKeyPayload(
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name) override;
  void TpmAttestationSetKeyPayload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name,
      const std::vector<uint8_t>& in_payload) override;
  void TpmAttestationDeleteKeys(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_prefix) override;
  void TpmAttestationDeleteKey(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      bool in_is_user_specific,
      const std::string& in_username,
      const std::string& in_key_name) override;
  void TpmAttestationGetEK(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string, bool>>
          response) override;
  void TpmAttestationResetIdentity(
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      const std::string& in_reset_token) override;
  void TpmGetVersionStructured(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<uint32_t,
                                                             uint64_t,
                                                             uint32_t,
                                                             uint32_t,
                                                             uint64_t,
                                                             std::string>>
          response) override;
  void Pkcs11IsTpmTokenReady(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void Pkcs11GetTpmTokenInfo(
      std::unique_ptr<brillo::dbus_utils::
                          DBusMethodResponse<std::string, std::string, int32_t>>
          response) override;
  void Pkcs11GetTpmTokenInfoForUser(
      std::unique_ptr<brillo::dbus_utils::
                          DBusMethodResponse<std::string, std::string, int32_t>>
          response,
      const std::string& in_username) override;
  void Pkcs11Terminate(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
      const std::string& in_username) override;
  void GetStatusString(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string>>
          response) override;
  void InstallAttributesGet(
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      const std::string& in_name) override;
  void InstallAttributesSet(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      const std::string& in_name,
      const std::vector<uint8_t>& in_value) override;
  void InstallAttributesCount(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response)
      override;
  void InstallAttributesFinalize(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void InstallAttributesIsReady(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void InstallAttributesIsSecure(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void InstallAttributesIsInvalid(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void InstallAttributesIsFirstInstall(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void SignBootLockbox(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::SignBootLockboxRequest& in_request) override;
  void VerifyBootLockbox(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::VerifyBootLockboxRequest& in_request) override;
  void FinalizeBootLockbox(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::FinalizeBootLockboxRequest& in_request) override;
  void GetBootAttribute(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::GetBootAttributeRequest& in_request) override;
  void SetBootAttribute(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::SetBootAttributeRequest& in_request) override;
  void FlushAndSignBootAttributes(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::FlushAndSignBootAttributesRequest& in_request) override;
  void GetLoginStatus(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::GetLoginStatusRequest& in_request) override;
  void GetTpmStatus(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                        cryptohome::BaseReply>> response,
                    const cryptohome::GetTpmStatusRequest& in_request) override;
  void GetEndorsementInfo(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::GetEndorsementInfoRequest& in_request) override;
  void InitializeCastKey(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::InitializeCastKeyRequest& in_request) override;
  void StartFingerprintAuthSession(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AccountIdentifier& in_account_id,
      const cryptohome::StartFingerprintAuthSessionRequest& in_request)
      override;
  void EndFingerprintAuthSession(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::EndFingerprintAuthSessionRequest& in_request) override;
  void GetWebAuthnSecret(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AccountIdentifier& in_account_id,
      const cryptohome::GetWebAuthnSecretRequest& in_request) override;
  void GetFirmwareManagementParameters(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::GetFirmwareManagementParametersRequest& in_request)
      override;
  void SetFirmwareManagementParameters(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::SetFirmwareManagementParametersRequest& in_request)
      override;
  void RemoveFirmwareManagementParameters(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::RemoveFirmwareManagementParametersRequest& in_request)
      override;
  void MigrateToDircrypto(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
      const cryptohome::AccountIdentifier& in_account_id,
      const cryptohome::MigrateToDircryptoRequest& in_migrate_request) override;
  void NeedsDircryptoMigration(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      const cryptohome::AccountIdentifier& in_account_id) override;
  void GetSupportedKeyPolicies(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::GetSupportedKeyPoliciesRequest& in_request) override;
  void IsQuotaSupported(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void GetCurrentSpaceForUid(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int64_t>> response,
      uint32_t in_uid) override;
  void GetCurrentSpaceForGid(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int64_t>> response,
      uint32_t in_gid) override;
  void GetCurrentSpaceForProjectId(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int64_t>> response,
      uint32_t in_project_id) override;
  void SetProjectId(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      uint32_t in_project_id,
      int32_t in_parent_path,
      const std::string& in_child_path,
      const cryptohome::AccountIdentifier& in_account_id) override;
  void LockToSingleUserMountUntilReboot(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::LockToSingleUserMountUntilRebootRequest& in_request)
      override;
  void GetRsuDeviceId(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::GetRsuDeviceIdRequest& in_request) override;
  void CheckHealth(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                       cryptohome::BaseReply>> response,
                   const cryptohome::CheckHealthRequest& in_request) override;
  void StartAuthSession(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::BaseReply>> response,
      const cryptohome::AccountIdentifier& in_account_id,
      const cryptohome::StartAuthSessionRequest& in_request) override;

  // This is a public version of OnDircryptoMigrationProgressSignal() that
  // simply calls the protected version. This is only used for testing because
  // the unit test cannot call the real protected version.
  void OnDircryptoMigrationProgressSignalForTestingOnly(
      const user_data_auth::DircryptoMigrationProgress& progress) {
    OnDircryptoMigrationProgressSignal(progress);
  }

  // This is a public version of OnLowDiskSpaceSignal() that simply calls the
  // protected version. This is only used for testing because the unit test
  // cannot call the real protected version.
  void OnLowDiskSpaceSignalForTestingOnly(
      const user_data_auth::LowDiskSpace& payload) {
    OnLowDiskSpaceSignal(payload);
  }

 protected:
  // This constructor is reserved only for testing.
  LegacyCryptohomeInterfaceAdaptor(
      org::chromium::AttestationProxyInterface* attestation_proxy,
      org::chromium::TpmManagerProxyInterface* tpm_ownership_proxy,
      org::chromium::TpmNvramProxyInterface* tpm_nvram_proxy,
      org::chromium::UserDataAuthInterfaceProxyInterface* userdataauth_proxy,
      org::chromium::ArcQuotaProxyInterface* arc_quota_proxy,
      org::chromium::CryptohomePkcs11InterfaceProxyInterface* pkcs11_proxy,
      org::chromium::InstallAttributesInterfaceProxyInterface*
          install_attributes_proxy,
      org::chromium::CryptohomeMiscInterfaceProxyInterface* misc_proxy,
      cryptohome::Platform* platform)
      : org::chromium::CryptohomeInterfaceAdaptor(this),
        dbus_object_(nullptr),
        attestation_proxy_(attestation_proxy),
        tpm_ownership_proxy_(tpm_ownership_proxy),
        tpm_nvram_proxy_(tpm_nvram_proxy),
        userdataauth_proxy_(userdataauth_proxy),
        arc_quota_proxy_(arc_quota_proxy),
        pkcs11_proxy_(pkcs11_proxy),
        install_attributes_proxy_(install_attributes_proxy),
        misc_proxy_(misc_proxy),
        platform_(platform) {}
  LegacyCryptohomeInterfaceAdaptor(const LegacyCryptohomeInterfaceAdaptor&) =
      delete;
  LegacyCryptohomeInterfaceAdaptor& operator=(
      const LegacyCryptohomeInterfaceAdaptor&) = delete;

  // This is used in testing to be able to mock SendAsyncCallStatusSignal.
  virtual void VirtualSendAsyncCallStatusSignal(int32_t in_async_id,
                                                bool in_return_status,
                                                int32_t in_return_code) {
    SendAsyncCallStatusSignal(in_async_id, in_return_status, in_return_code);
  }

  // This is used in testing to be able to mock
  // SendAsyncCallStatusWithDataSignal.
  virtual void VirtualSendAsyncCallStatusWithDataSignal(
      int32_t in_async_id,
      bool in_return_status,
      const std::vector<uint8_t>& in_data) {
    SendAsyncCallStatusWithDataSignal(in_async_id, in_return_status, in_data);
  }

  // This is used in testing to be able to mock
  // SendDircryptoMigrationProgressSignal
  virtual void VirtualSendDircryptoMigrationProgressSignal(
      int32_t in_status, uint64_t in_current_bytes, uint64_t in_total_bytes) {
    SendDircryptoMigrationProgressSignal(in_status, in_current_bytes,
                                         in_total_bytes);
  }

  // This is used in testing to be able to mock SendLowDiskSpaceSignal
  virtual void VirtualSendLowDiskSpaceSignal(uint64_t disk_free_bytes) {
    SendLowDiskSpaceSignal(disk_free_bytes);
  }

  // Signal forwarders

  // Forwards DircryptoMigrationProgress Signal
  void OnDircryptoMigrationProgressSignal(
      const user_data_auth::DircryptoMigrationProgress& progress);

  // Forwards LowDiskSpace Signal
  void OnLowDiskSpaceSignal(const user_data_auth::LowDiskSpace& payload);

  // Forwards OwnershipTakenSignal from tpm manager.
  void OnOwnershipTakenSignal(const tpm_manager::OwnershipTakenSignal& payload);

  // Handles signal registration failure
  void OnSignalConnectedHandler(const std::string& interface,
                                const std::string& signal,
                                bool success);

 private:
  // Method used as callbacks once the call to the new interface returns
  // Note that OnSuccess in the method names below refers to a successful DBus
  // call, which may or may not be the same as the action being performed by the
  // underlying API is successful. Some of our APIs opt to reflect failure to
  // perform the action that the the API is supposed to do through protobuf
  // fields (such as using CryptohomeErrorCode).
  void IsMountedOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const user_data_auth::IsMountedReply& reply);
  void IsMountedForUserOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool, bool>> response,
      const user_data_auth::IsMountedReply& reply);
  void ListKeysExOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const user_data_auth::ListKeysReply& reply);
  void GetKeyDataOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const user_data_auth::GetKeyDataReply& reply);
  void AddDataRestoreKeyOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const user_data_auth::AddDataRestoreKeyReply& reply);
  void GetSystemSaltOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>>> response,
      const user_data_auth::GetSystemSaltReply& reply);
  void GetSanitizedUsernameOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::string>> response,
      const user_data_auth::GetSanitizedUsernameReply& reply);
  void MountExOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const user_data_auth::MountReply& reply);
  void GetAccountDiskUsageOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const user_data_auth::GetAccountDiskUsageReply& reply);
  void UpdateCurrentUserActivityTimestampOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<>> response,
      const user_data_auth::UpdateCurrentUserActivityTimestampReply& reply);
  void TpmIsReadyOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const tpm_manager::GetTpmStatusReply& reply);
  void TpmIsEnabledOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const tpm_manager::GetTpmStatusReply& reply);
  void TpmGetPasswordOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::string>> response,
      const tpm_manager::GetTpmStatusReply& reply);
  void TpmIsOwnedOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const tpm_manager::GetTpmStatusReply& reply);
  void TpmCanAttemptOwnershipOnSuccess(
      const tpm_manager::TakeOwnershipReply& reply);
  void TpmCanAttemptOwnershipOnFailure(brillo::Error* err);
  void TpmClearStoredPasswordOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<>> response,
      const tpm_manager::ClearStoredOwnerPasswordReply& reply);
  void TpmIsAttestationPreparedOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response_raw,
      const attestation::GetEnrollmentPreparationsReply& reply);
  void TpmAttestationGetEnrollmentPreparationsExOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const attestation::GetEnrollmentPreparationsReply& reply);
  void TpmVerifyAttestationDataOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const attestation::VerifyReply& reply);
  void TpmVerifyEKOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const attestation::VerifyReply& reply);
  void TpmAttestationCreateEnrollRequestOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>>> response,
      const attestation::CreateEnrollRequestReply& reply);
  void TpmAttestationEnrollSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const attestation::FinishEnrollReply& reply);
  void TpmAttestationCreateCertRequestOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>>> response,
      const attestation::CreateCertificateRequestReply& reply);
  void TpmAttestationFinishCertRequestOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      const attestation::FinishCertificateRequestReply& reply);
  void TpmIsAttestationEnrolledOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const attestation::GetStatusReply& reply);
  void TpmAttestationDoesKeyExistOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const attestation::GetKeyInfoReply& reply);
  void TpmAttestationGetCertificateOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      const attestation::GetKeyInfoReply& reply);
  void TpmAttestationGetPublicKeyOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      const attestation::GetKeyInfoReply& reply);
  void TpmAttestationGetEnrollmentIdOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      const attestation::GetEnrollmentIdReply& reply);
  void TpmAttestationGetKeyPayloadOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      const attestation::GetKeyInfoReply& reply);
  void TpmAttestationSetKeyPayloadOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const attestation::SetKeyPayloadReply& reply);
  void TpmAttestationDeleteKeysOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const attestation::DeleteKeysReply& reply);
  void TpmAttestationDeleteKeyOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const attestation::DeleteKeysReply& reply);
  void TpmAttestationGetEKOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::string, bool>> response,
      const attestation::GetEndorsementInfoReply& reply);
  void TpmAttestationResetIdentityOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      const attestation::ResetIdentityReply& reply);
  void TpmGetVersionStructuredOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<uint32_t,
                                               uint64_t,
                                               uint32_t,
                                               uint32_t,
                                               uint64_t,
                                               std::string>> response,
      const tpm_manager::GetVersionInfoReply& reply);
  void Pkcs11IsTpmTokenReadyOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const user_data_auth::Pkcs11IsTpmTokenReadyReply& reply);
  void Pkcs11GetTpmTokenInfoOnSuccess(
      std::shared_ptr<
          SharedDBusMethodResponse<std::string, std::string, int32_t>> response,
      const user_data_auth::Pkcs11GetTpmTokenInfoReply& reply);
  void Pkcs11TerminateOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<>> response,
      const user_data_auth::Pkcs11TerminateReply& reply);
  void GetStatusStringOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::string>> response,
      const user_data_auth::GetStatusStringReply& reply);
  void InstallAttributesGetOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
          response,
      const user_data_auth::InstallAttributesGetReply& reply);
  void InstallAttributesSetOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const user_data_auth::InstallAttributesSetReply& reply);
  void InstallAttributesCountOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<int32_t>> response,
      const user_data_auth::InstallAttributesGetStatusReply& reply);
  void InstallAttributesFinalizeOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const user_data_auth::InstallAttributesFinalizeReply& reply);
  void InstallAttributesIsReadyOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const user_data_auth::InstallAttributesGetStatusReply& reply);
  void InstallAttributesIsSecureOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const user_data_auth::InstallAttributesGetStatusReply& reply);
  void InstallAttributesIsInvalidOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const user_data_auth::InstallAttributesGetStatusReply& reply);
  void InstallAttributesIsFirstInstallOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const user_data_auth::InstallAttributesGetStatusReply& reply);
  void GetLoginStatusOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const user_data_auth::GetLoginStatusReply& reply);
  void GetTpmStatusOnStageOwnershipStatusDone(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const tpm_manager::GetTpmStatusReply& status_reply);
  void GetTpmStatusOnStageDictionaryAttackDone(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      BaseReply reply,
      const tpm_manager::GetDictionaryAttackInfoReply& da_reply);
  void GetTpmStatusOnStageInstallAttributesDone(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      BaseReply reply,
      const user_data_auth::InstallAttributesGetStatusReply&
          install_attr_reply);
  void GetTpmStatusOnStageAttestationDone(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      BaseReply reply,
      const attestation::GetStatusReply& attestation_reply);
  void GetEndorsementInfoOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const attestation::GetEndorsementInfoReply& reply);
  void GetFirmwareManagementParametersOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const user_data_auth::GetFirmwareManagementParametersReply& reply);
  void MigrateToDircryptoOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<>> response,
      const user_data_auth::StartMigrateToDircryptoReply& reply);
  void NeedsDircryptoMigrationOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const user_data_auth::NeedsDircryptoMigrationReply& reply);
  void GetSupportedKeyPoliciesOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const user_data_auth::GetSupportedKeyPoliciesReply& reply);
  void IsQuotaSupportedOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const user_data_auth::GetArcDiskFeaturesReply& reply);
  void GetCurrentSpaceForUidOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<int64_t>> response,
      const user_data_auth::GetCurrentSpaceForArcUidReply& reply);
  void GetCurrentSpaceForGidOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<int64_t>> response,
      const user_data_auth::GetCurrentSpaceForArcGidReply& reply);
  void GetCurrentSpaceForProjectIdOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<int64_t>> response,
      const user_data_auth::GetCurrentSpaceForArcProjectIdReply& reply);
  void SetProjectIdOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<bool>> response,
      const user_data_auth::SetProjectIdReply& reply);
  void LockToSingleUserMountUntilRebootOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const user_data_auth::LockToSingleUserMountUntilRebootReply& reply);
  void GetRsuDeviceIdOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const user_data_auth::GetRsuDeviceIdReply& reply);
  void CheckHealthOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const user_data_auth::CheckHealthReply& reply);
  void GetWebAuthnSecretOnSuccess(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const user_data_auth::GetWebAuthnSecretReply& reply);

  // This method forwards the error received from calling the new interface back
  // to the old interface
  template <typename... Types>
  void ForwardError(
      std::shared_ptr<SharedDBusMethodResponse<Types...>> response,
      brillo::Error* err) {
    response->ReplyWithError(err);
  }

  // Returns the next sequence ID for Async methods
  int NextSequence() {
    // AtomicSequenceNumber is zero-based, so increment so that the sequence
    // ids are one-based.
    return sequence_holder_.GetNext() + 1;
  }

  // This serves as the on_failure callback after calling the actual method
  // in attestationd.
  // Note that this is for the version of async call that have data result.
  template <typename ReplyProtoType>
  void AsyncForwardErrorWithData(const std::string& (ReplyProtoType::*func)()
                                     const,
                                 int async_id,
                                 brillo::Error* err) {
    // Error is ignored because there is no mechanism to forward the dbus
    // error through signal, and the current implementation in
    // service_distributed class handles the error by sending
    // STATUS_NOT_AVAILABLE instead, so we follow this behaviour.
    ReplyProtoType reply;
    reply.set_status(attestation::AttestationStatus::STATUS_NOT_AVAILABLE);
    AsyncReplyWithData(func, async_id, reply);
  }

  // This serves as the on_failure callback after calling the actual method
  // in attestationd
  // Note that this is for the version of async call that have no data result.
  template <typename ReplyProtoType>
  void AsyncForwardErrorWithNoData(int async_id, brillo::Error* err) {
    // Error is ignored because there is no mechanism to forward the dbus
    // error through signal, and the current implementation in
    // service_distributed class handles the error by sending
    // STATUS_NOT_AVAILABLE instead, so we follow this behaviour.
    ReplyProtoType reply;
    reply.set_status(attestation::AttestationStatus::STATUS_NOT_AVAILABLE);
    AsyncReplyWithNoData(async_id, reply);
  }

  // This serves as the on_success callback for async methods with data after
  // calling the actual method in attestationd.
  template <typename ReplyProtoType>
  void AsyncReplyWithData(const std::string& (ReplyProtoType::*func)() const,
                          int async_id,
                          const ReplyProtoType& reply) {
    std::string data_string = (reply.*func)();
    std::vector<uint8_t> data(data_string.begin(), data_string.end());
    bool return_status =
        reply.status() == attestation::AttestationStatus::STATUS_SUCCESS;
    VirtualSendAsyncCallStatusWithDataSignal(async_id, return_status, data);
  }

  // This serves as the on_success callback for async methods with no data after
  // calling the actual method in attestationd.
  template <typename ReplyProtoType>
  void AsyncReplyWithNoData(int async_id, const ReplyProtoType& reply) {
    bool return_status =
        reply.status() == attestation::AttestationStatus::STATUS_SUCCESS;
    VirtualSendAsyncCallStatusSignal(async_id, return_status, 0);
  }

  // This is a function that handles an async request received on the legacy
  // cryptohome interface. The code that calls this function resides in
  // the actual method handler, and it only needs to assemble the request
  // proto and pass it to this function, and this function will take care
  // of the rest.
  // Note that this version deals with async method calls that return byte array
  // data.
  template <typename RequestProtoType, typename ReplyProtoType>
  int HandleAsyncData(const std::string& (ReplyProtoType::*func)() const,
                      RequestProtoType request,
                      base::OnceCallback<void(
                          const RequestProtoType&,
                          const base::Callback<void(const ReplyProtoType&)>&,
                          const base::Callback<void(brillo::Error*)>&,
                          int)> target_method,
                      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) {
    int async_id = NextSequence();

    base::Callback<void(const ReplyProtoType&)> on_success = base::Bind(
        &LegacyCryptohomeInterfaceAdaptor::AsyncReplyWithData<ReplyProtoType>,
        base::Unretained(this), func, async_id);
    base::Callback<void(brillo::Error*)> on_failure =
        base::Bind(&LegacyCryptohomeInterfaceAdaptor::AsyncForwardErrorWithData<
                       ReplyProtoType>,
                   base::Unretained(this), func, async_id);
    std::move(target_method).Run(request, on_success, on_failure, timeout_ms);

    return async_id;
  }

  // This is a function that handles an async request received on the legacy
  // cryptohome interface. The code that calls this function resides in
  // the actual method handler, and it only needs to assemble the request
  // proto and pass it to this function, and this function will take care
  // of the rest.
  // Note that this version deals with async method calls that return only
  // status but not data.
  template <typename RequestProtoType, typename ReplyProtoType>
  int HandleAsyncStatus(
      RequestProtoType request,
      base::OnceCallback<
          void(const RequestProtoType&,
               const base::Callback<void(const ReplyProtoType&)>&,
               const base::Callback<void(brillo::Error*)>&,
               int)> target_method,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) {
    int async_id = NextSequence();

    base::Callback<void(const ReplyProtoType&)> on_success = base::Bind(
        &LegacyCryptohomeInterfaceAdaptor::AsyncReplyWithNoData<ReplyProtoType>,
        base::Unretained(this), async_id);
    base::Callback<void(brillo::Error*)> on_failure = base::Bind(
        &LegacyCryptohomeInterfaceAdaptor::AsyncForwardErrorWithNoData<
            ReplyProtoType>,
        base::Unretained(this), async_id);
    std::move(target_method).Run(request, on_success, on_failure, timeout_ms);

    return async_id;
  }

  // This method is used when the handler for a successful DBus call to the new
  // API only needs to forward the error code in the new API's proto to a
  // BaseReply type on the legacy API.
  template <typename ReplyProtoType>
  static void ForwardBaseReplyErrorCode(
      std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
      const ReplyProtoType& reply) {
    cryptohome::BaseReply base_reply;
    base_reply.set_error(
        static_cast<cryptohome::CryptohomeErrorCode>(reply.error()));
    ClearErrorIfNotSet(&base_reply);
    response->Return(base_reply);
  }

  // A helper function which maps an integer to a valid CertificateProfile.
  static attestation::CertificateProfile IntegerToCertificateProfile(
      int profile_value);

  // A helper function which maps an integer to a valid ACAType.
  static base::Optional<attestation::ACAType> IntegerToACAType(int type);

  // A helper function which maps an integer to a valid VAType.
  static base::Optional<attestation::VAType> IntegerToVAType(int type);

  // A helper function that clear |error| in a BaseReply if it's not set. This
  // is the default behaviour is the legacy interface and therefore should be
  // followed.
  static void ClearErrorIfNotSet(cryptohome::BaseReply* reply);

  // This is the DBus Object that this interface is registered to. Note that
  // this DBus Object is not owned by this class because multiple interfaces can
  // be associated with the same object.
  brillo::dbus_utils::DBusObject* dbus_object_;

  // Below are the DBus proxy objects that are used in this class. The default_*
  // versions are for holding an instance that is only used in actual,
  // non-testing setting. While the non default_* versions are the ones that's
  // actually used by the class. During testing, the non default_* versions will
  // be overridden with mocks for testing.
  // Note that this follows the convention in other cryptohome class such as
  // UserDataAuth class.
  std::unique_ptr<org::chromium::AttestationProxyInterface>
      default_attestation_proxy_;
  org::chromium::AttestationProxyInterface* attestation_proxy_;
  std::unique_ptr<org::chromium::TpmManagerProxyInterface>
      default_tpm_ownership_proxy_;
  org::chromium::TpmManagerProxyInterface* tpm_ownership_proxy_;
  std::unique_ptr<org::chromium::TpmNvramProxyInterface>
      default_tpm_nvram_proxy_;
  org::chromium::TpmNvramProxyInterface* tpm_nvram_proxy_;
  std::unique_ptr<org::chromium::UserDataAuthInterfaceProxyInterface>
      default_userdataauth_proxy_;
  org::chromium::UserDataAuthInterfaceProxyInterface* userdataauth_proxy_;
  std::unique_ptr<org::chromium::ArcQuotaProxyInterface>
      default_arc_quota_proxy_;
  org::chromium::ArcQuotaProxyInterface* arc_quota_proxy_;
  std::unique_ptr<org::chromium::CryptohomePkcs11InterfaceProxyInterface>
      default_pkcs11_proxy_;
  org::chromium::CryptohomePkcs11InterfaceProxyInterface* pkcs11_proxy_;
  std::unique_ptr<org::chromium::InstallAttributesInterfaceProxyInterface>
      default_install_attributes_proxy_;
  org::chromium::InstallAttributesInterfaceProxyInterface*
      install_attributes_proxy_;
  std::unique_ptr<org::chromium::CryptohomeMiscInterfaceProxyInterface>
      default_misc_proxy_;
  org::chromium::CryptohomeMiscInterfaceProxyInterface* misc_proxy_;

  // An atomic incrementing sequence for setting asynchronous call ids.
  base::AtomicSequenceNumber sequence_holder_;

  // The default platform object for accessing platform related functionalities
  std::unique_ptr<cryptohome::Platform> default_platform_;

  // The actual platform object used by this class, usually set to
  // default_platform_, but can be overridden for testing
  cryptohome::Platform* platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_PROXY_LEGACY_CRYPTOHOME_INTERFACE_ADAPTOR_H_
