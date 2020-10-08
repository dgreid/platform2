// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Tpm

#include "cryptohome/tpm2_impl.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>
#include <base/numerics/safe_conversions.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <trunks/authorization_delegate.h>
#include <trunks/blob_parser.h>
#include <trunks/error_codes.h>
#include <trunks/policy_session.h>
#include <trunks/tpm_alerts.h>
#include <trunks/tpm_constants.h>
#include <trunks/trunks_dbus_proxy.h>
#include <trunks/trunks_factory.h>
#include <trunks/trunks_factory_impl.h>

#include "cryptohome/cryptolib.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using brillo::SecureBlob;
using trunks::GetErrorString;
using trunks::TPM_RC;
using trunks::TPM_RC_SUCCESS;
using trunks::TrunksFactory;

namespace cryptohome {

namespace {

// The upper bits that identify the layer that produced the response.
// These bits are always 0 for the hardware TPM response codes.
constexpr TPM_RC kResponseLayerMask = 0xFFFFF000;

// Cr50 vendor command error codes.
constexpr TPM_RC kVendorRC = 0x500;
constexpr TPM_RC kCr50ErrorNoSuchCommand = kVendorRC + 0x7F;

Tpm::TpmRetryAction ResultToRetryAction(TPM_RC result) {
  // For hardware TPM errors and TPM-equivalent response codes produced by
  // Resource Manager, use just the error number and strip everything else.
  if ((result & kResponseLayerMask) == 0 ||
      (result & kResponseLayerMask) == trunks::kResourceManagerTpmErrorBase) {
    result = trunks::GetFormatOneError(result & ~kResponseLayerMask);
  }
  Tpm::TpmRetryAction action;
  switch (result) {
    case trunks::TPM_RC_SUCCESS:
      action = Tpm::kTpmRetryNone;
      break;
    case trunks::TPM_RC_LOCKOUT:
      action = Tpm::kTpmRetryDefendLock;
      break;
    case trunks::TPM_RC_HANDLE:
    case trunks::TPM_RC_REFERENCE_H0:
    case trunks::TPM_RC_REFERENCE_H1:
    case trunks::TPM_RC_REFERENCE_H2:
    case trunks::TPM_RC_REFERENCE_H3:
    case trunks::TPM_RC_REFERENCE_H4:
    case trunks::TPM_RC_REFERENCE_H5:
    case trunks::TPM_RC_REFERENCE_H6:
      action = Tpm::kTpmRetryInvalidHandle;
      break;
    case trunks::TPM_RC_INITIALIZE:
    case trunks::TPM_RC_REBOOT:
      action = Tpm::kTpmRetryReboot;
      break;
    case trunks::TRUNKS_RC_WRITE_ERROR:
    case trunks::TRUNKS_RC_READ_ERROR:
    case trunks::SAPI_RC_NO_CONNECTION:
    case trunks::SAPI_RC_NO_RESPONSE_RECEIVED:
    case trunks::SAPI_RC_MALFORMED_RESPONSE:
      action = Tpm::kTpmRetryCommFailure;
      break;
    case trunks::TPM_RC_RETRY:
    case trunks::TPM_RC_NV_RATE:
      action = Tpm::kTpmRetryLater;
      break;
    default:
      action = Tpm::kTpmRetryFailNoRetry;
      break;
  }
  return action;
}

// Returns the total number of bits set in the first |size| elements from
// |array|.
int CountSetBits(const uint8_t* array, size_t size) {
  int res = 0;
  for (size_t i = 0; i < size; ++i) {
    for (int bit_position = 0; bit_position < 8; ++bit_position) {
      if ((array[i] & (1 << bit_position)) != 0) {
        ++res;
      }
    }
  }
  return res;
}

std::string OwnerDependencyEnumClassToString(
    TpmPersistentState::TpmOwnerDependency dependency) {
  switch (dependency) {
    case TpmPersistentState::TpmOwnerDependency::kInstallAttributes:
      return tpm_manager::kTpmOwnerDependency_Nvram;
    case TpmPersistentState::TpmOwnerDependency::kAttestation:
      return tpm_manager::kTpmOwnerDependency_Attestation;
    default:
      NOTREACHED() << __func__ << ": Unexpected enum class value: "
                   << static_cast<int>(dependency);
      return "";
  }
}

}  // namespace

// Keep it with sync to UMA enum list
// https://chromium.googlesource.com/chromium/src/+/master/tools/metrics/histograms/enums.xml
// These values are persisted to logs, and should therefore never be renumbered
// nor reused.
enum TpmAlerts {
  kCamoBreach = 1,
  kDmemParity = 2,
  kDrfParity = 3,
  kImemParity = 4,
  kPgmFault = 5,
  kCpuDIfBusError = 6,
  kCpuDIfUpdateWatchdog = 7,
  kCpuIIfBusError = 8,
  kCpuIIfUpdateWatchdog = 9,
  kCpuSIfBusError = 10,
  kCpuSIfUpdateWatchdog = 11,
  kDmaIfBusErr = 12,
  kDmaIfUpdateWatchdog = 13,
  kSpsIfBusErr = 14,
  kSpsIfUpdateWatchdog = 15,
  kUsbIfBusErr = 16,
  kUsbIfUpdateWatchdog = 17,
  kFuseDefaults = 18,
  kDiffFail = 19,
  kSoftwareAlert0 = 20,
  kSoftwareAlert1 = 21,
  kSoftwareAlert2 = 22,
  kSoftwareAlert3 = 23,
  kHearbitFail = 24,
  kProcOpcodeHash = 25,
  kSramParityScrub = 26,
  kAesExecCtrMax = 27,
  kAesHkey = 28,
  kCertLookup = 29,
  kFlashEntry = 30,
  kPw = 31,
  kShaExecCtrMax = 32,
  kShaFault = 33,
  kShaHkey = 34,
  kPmuBatteryMon = 35,
  kPmuWatchdog = 36,
  kRtcDead = 37,
  kTempMax = 38,
  kTempMaxDiff = 39,
  kTempMin = 40,
  kRngOutOfSpec = 41,
  kRngTimeout = 42,
  kVoltageError = 43,
  kXoJitteryTrim = 44,

  kTPMAlertNumBuckets,  // Must be the last entry.
};
static_assert(kTPMAlertNumBuckets <= trunks::kAlertsMaxSize + 1,
              "Number of UMA enums less than alerts set size");

// Maps alerts identifiers received from TMP firmware to UMA identifiers
const TpmAlerts h1AlertsMap[trunks::kH1AlertsSize] = {
    kCamoBreach,
    kDmemParity,
    kDrfParity,
    kImemParity,
    kPgmFault,
    kCpuDIfBusError,
    kCpuDIfUpdateWatchdog,
    kCpuIIfBusError,
    kCpuIIfUpdateWatchdog,
    kCpuSIfBusError,
    kCpuSIfUpdateWatchdog,
    kDmaIfBusErr,
    kDmaIfUpdateWatchdog,
    kSpsIfBusErr,
    kSpsIfUpdateWatchdog,
    kUsbIfBusErr,
    kUsbIfUpdateWatchdog,
    kFuseDefaults,
    kDiffFail,
    kSoftwareAlert0,
    kSoftwareAlert1,
    kSoftwareAlert2,
    kSoftwareAlert3,
    kHearbitFail,
    kProcOpcodeHash,
    kSramParityScrub,
    kAesExecCtrMax,
    kAesHkey,
    kCertLookup,
    kFlashEntry,
    kPw,
    kShaExecCtrMax,
    kShaFault,
    kShaHkey,
    kPmuBatteryMon,
    kPmuWatchdog,
    kRtcDead,
    kTempMax,
    kTempMaxDiff,
    kTempMin,
    kRngOutOfSpec,
    kRngTimeout,
    kVoltageError,
    kXoJitteryTrim,
};

Tpm2Impl::Tpm2Impl(TrunksFactory* factory,
                   tpm_manager::TpmManagerUtility* tpm_manager_utility)
    : tpm_manager_utility_(tpm_manager_utility),
      has_external_trunks_context_(true) {
  external_trunks_context_.factory = factory;
  external_trunks_context_.tpm_state = factory->GetTpmState();
  external_trunks_context_.tpm_utility = factory->GetTpmUtility();
}

bool Tpm2Impl::GetOwnerPassword(brillo::SecureBlob* owner_password) {
  if (IsOwned()) {
    *owner_password =
        brillo::SecureBlob(last_tpm_manager_data_.owner_password());
    if (owner_password->empty()) {
      LOG(WARNING) << __func__
                   << ": Trying to get owner password after it is cleared.";
    }
  } else {
    LOG(ERROR)
        << __func__
        << ": Cannot get owner password until TPM is confirmed to be owned.";
    owner_password->clear();
  }
  return !owner_password->empty();
}

void Tpm2Impl::SetOwnerPassword(const SecureBlob& /* owner_password */) {
  LOG(ERROR) << __func__ << ": Not implemented.";
}

bool Tpm2Impl::InitializeTpmManagerUtility() {
  if (!tpm_manager_utility_) {
    tpm_manager_utility_ = tpm_manager::TpmManagerUtility::GetSingleton();
    if (!tpm_manager_utility_) {
      LOG(ERROR) << __func__ << ": Failed to get TpmManagerUtility singleton!";
    }
  }
  return tpm_manager_utility_ && tpm_manager_utility_->Initialize();
}

bool Tpm2Impl::CacheTpmManagerStatus() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->GetTpmStatus(&is_enabled_, &is_owned_,
                                            &last_tpm_manager_data_);
}

bool Tpm2Impl::IsEnabled() {
  if (!is_enabled_) {
    if (!CacheTpmManagerStatus()) {
      LOG(ERROR) << __func__ << ": Failed to call |UpdateTpmStatus|.";
      return false;
    }
  }
  return is_enabled_;
}

void Tpm2Impl::SetIsEnabled(bool /* enabled */) {
  LOG(ERROR) << __func__ << ": Not implemented.";
}

bool Tpm2Impl::IsOwned() {
  if (!is_owned_) {
    if (!UpdateTpmStatus(RefreshType::REFRESH_IF_NEEDED)) {
      LOG(ERROR) << __func__ << ": Failed to call |UpdateTpmStatus|.";
      return false;
    }
  }
  return is_owned_;
}

bool Tpm2Impl::HasResetLockPermissions() {
  if (!UpdateTpmStatus(RefreshType::REFRESH_IF_NEEDED)) {
    LOG(ERROR) << __func__ << ": Failed to call |UpdateTpmStatus|.";
    return false;
  }
  bool has_reset_lock_permissions = true;
  if (last_tpm_manager_data_.owner_password().empty()) {
    if (last_tpm_manager_data_.lockout_password().empty() &&
        !last_tpm_manager_data_.has_owner_delegate()) {
      has_reset_lock_permissions = false;
    } else if (last_tpm_manager_data_.has_owner_delegate() &&
               !last_tpm_manager_data_.owner_delegate()
                    .has_reset_lock_permissions()) {
      has_reset_lock_permissions = false;
    }
  }
  return has_reset_lock_permissions;
}

void Tpm2Impl::SetIsOwned(bool /* owned */) {
  LOG(ERROR) << __func__ << ": Not implemented.";
}

bool Tpm2Impl::PerformEnabledOwnedCheck(bool* enabled, bool* owned) {
  if (!UpdateTpmStatus(RefreshType::REFRESH_IF_NEEDED)) {
    return false;
  }
  if (enabled) {
    *enabled = is_enabled_;
  }
  if (owned) {
    *owned = is_owned_;
  }
  return true;
}

bool Tpm2Impl::IsInitialized() {
  return UpdateTpmStatus(RefreshType::REFRESH_IF_NEEDED);
}

void Tpm2Impl::SetIsInitialized(bool done) {
  LOG(ERROR) << __func__ << ": Not implemented.";
}

bool Tpm2Impl::IsBeingOwned() {
  return is_being_owned_;
}

void Tpm2Impl::SetIsBeingOwned(bool value) {
  is_being_owned_ = value;
}

bool Tpm2Impl::GetRandomDataBlob(size_t length, brillo::Blob* data) {
  brillo::SecureBlob blob(length);
  if (!this->GetRandomDataSecureBlob(length, &blob)) {
    LOG(ERROR) << "GetRandomDataBlob failed";
    return false;
  }
  data->assign(blob.begin(), blob.end());
  return true;
}

bool Tpm2Impl::GetRandomDataSecureBlob(size_t length,
                                       brillo::SecureBlob* data) {
  CHECK(data);
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  std::string random_data;
  TPM_RC result =
      trunks->tpm_utility->GenerateRandom(length,
                                          nullptr,  // No authorization.
                                          &random_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting random data: " << GetErrorString(result);
    return false;
  }
  if (random_data.size() != length) {
    LOG(ERROR) << "Error getting random data: requested length " << length
               << ", received length " << random_data.size();
    return false;
  }
  data->assign(random_data.begin(), random_data.end());
  return true;
}

bool Tpm2Impl::GetAlertsData(Tpm::AlertsData* alerts) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return true;
  }

  trunks::TpmAlertsData trunks_alerts;
  TPM_RC result = trunks->tpm_utility->GetAlertsData(&trunks_alerts);
  if (result == trunks::TPM_RC_NO_SUCH_COMMAND) {
    LOG(INFO) << "TPM GetAlertsData vendor command is not implemented";
    return false;
  } else if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting alerts data: " << GetErrorString(result);
    memset(alerts, 0, sizeof(Tpm::AlertsData));
    return true;
  } else if (trunks_alerts.chip_family != trunks::kFamilyH1) {
    // Currently we support only H1 alerts
    LOG(ERROR) << "Unknown alerts family: " << trunks_alerts.chip_family;
    return false;
  }

  memset(alerts, 0, sizeof(Tpm::AlertsData));
  for (int i = 0; i < trunks_alerts.alerts_num; i++) {
    size_t uma_idx = h1AlertsMap[i];
    if (uma_idx <= 0 || uma_idx >= kTPMAlertNumBuckets) {
      LOG(ERROR) << "Alert index " << i << " maps into invalid UMA enum index "
                 << uma_idx;
    } else {
      alerts->counters[uma_idx] = trunks_alerts.counters[i];
    }
  }

  return true;
}

bool Tpm2Impl::DefineNvram(uint32_t index, size_t length, uint32_t flags) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  const bool write_define = flags & Tpm::kTpmNvramWriteDefine;
  const bool bind_to_pcr0 = flags & Tpm::kTpmNvramBindToPCR0;
  const bool firmware_readable = flags & Tpm::kTpmNvramFirmwareReadable;

  return tpm_manager_utility_->DefineSpace(index, length, write_define,
                                           bind_to_pcr0, firmware_readable);
}

bool Tpm2Impl::DestroyNvram(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->DestroySpace(index);
}

bool Tpm2Impl::WriteNvram(uint32_t index, const SecureBlob& blob) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  tpm_manager::WriteSpaceRequest request;
  request.set_index(index);
  request.set_data(blob.to_string());
  return tpm_manager_utility_->WriteSpace(index, blob.to_string(), false);
}

bool Tpm2Impl::ReadNvram(uint32_t index, SecureBlob* blob) {
  if (!InitializeTpmManagerUtility()) {
    return false;
  }

  std::string output;
  const bool result = tpm_manager_utility_->ReadSpace(index, false, &output);
  SecureBlob tmp(output);
  blob->swap(tmp);
  return result;
}

bool Tpm2Impl::IsNvramDefined(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  std::vector<uint32_t> spaces;
  if (!tpm_manager_utility_->ListSpaces(&spaces)) {
    return false;
  }
  for (uint32_t space : spaces) {
    if (index == space) {
      return true;
    }
  }
  return false;
}

bool Tpm2Impl::IsNvramLocked(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  uint32_t size;
  bool is_read_locked;
  bool is_write_locked;
  if (!tpm_manager_utility_->GetSpaceInfo(index, &size, &is_read_locked,
                                          &is_write_locked)) {
    return false;
  }
  return is_write_locked;
}

bool Tpm2Impl::WriteLockNvram(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->LockSpace(index);
}

unsigned int Tpm2Impl::GetNvramSize(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  uint32_t size;
  bool is_read_locked;
  bool is_write_locked;
  if (!tpm_manager_utility_->GetSpaceInfo(index, &size, &is_read_locked,
                                          &is_write_locked)) {
    return 0;
  }
  return size;
}

bool Tpm2Impl::IsEndorsementKeyAvailable() {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return true;
}

bool Tpm2Impl::CreateEndorsementKey() {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return false;
}

Tpm::TpmRetryAction Tpm2Impl::GetEndorsementPublicKey(
    SecureBlob* ek_public_key) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return Tpm::kTpmRetryFailNoRetry;
}

Tpm::TpmRetryAction Tpm2Impl::GetEndorsementPublicKeyWithDelegate(
    brillo::SecureBlob* ek_public_key,
    const brillo::Blob& delegate_blob,
    const brillo::Blob& delegate_secret) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return Tpm::kTpmRetryFailNoRetry;
}

bool Tpm2Impl::GetEndorsementCredential(SecureBlob* credential) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return false;
}

bool Tpm2Impl::MakeIdentity(SecureBlob* identity_public_key_der,
                            SecureBlob* identity_public_key,
                            SecureBlob* identity_key_blob,
                            SecureBlob* identity_binding,
                            SecureBlob* identity_label,
                            SecureBlob* pca_public_key,
                            SecureBlob* endorsement_credential,
                            SecureBlob* platform_credential,
                            SecureBlob* conformance_credential) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return false;
}

Tpm::QuotePcrResult Tpm2Impl::QuotePCR(uint32_t pcr_index,
                                       bool check_pcr_value,
                                       const SecureBlob& identity_key_blob,
                                       const SecureBlob& external_data,
                                       Blob* pcr_value,
                                       SecureBlob* quoted_data,
                                       SecureBlob* quote) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return Tpm::QuotePcrResult::kFailure;
}

bool Tpm2Impl::SealToPCR0(const brillo::SecureBlob& value,
                          brillo::SecureBlob* sealed_value) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  std::string policy_digest;
  TPM_RC result = trunks->tpm_utility->GetPolicyDigestForPcrValues(
      std::map<uint32_t, std::string>({{0, ""}}), false /* use_auth_value */,
      &policy_digest);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting policy digest: " << GetErrorString(result);
    return false;
  }
  std::unique_ptr<trunks::HmacSession> session =
      trunks->factory->GetHmacSession();
  if (trunks->tpm_utility->StartSession(session.get()) != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting hmac session.";
    return false;
  }
  std::string data_to_seal(value.begin(), value.end());
  std::string sealed_data;
  result = trunks->tpm_utility->SealData(data_to_seal, policy_digest, "",
                                         session->GetDelegate(), &sealed_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error sealing data to PCR0: " << GetErrorString(result);
    return false;
  }
  sealed_value->assign(sealed_data.begin(), sealed_data.end());
  return true;
}

bool Tpm2Impl::Unseal(const brillo::SecureBlob& sealed_value,
                      brillo::SecureBlob* value) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  std::unique_ptr<trunks::PolicySession> policy_session =
      trunks->factory->GetPolicySession();
  TPM_RC result = policy_session->StartUnboundSession(true, false);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting policy session: " << GetErrorString(result);
    return false;
  }
  result =
      policy_session->PolicyPCR(std::map<uint32_t, std::string>({{0, ""}}));
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error restricting policy to pcr 0: "
               << GetErrorString(result);
    return false;
  }
  std::string sealed_data(sealed_value.begin(), sealed_value.end());
  std::string unsealed_data;
  result = trunks->tpm_utility->UnsealData(
      sealed_data, policy_session->GetDelegate(), &unsealed_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error unsealing object: " << GetErrorString(result);
    return false;
  }
  value->assign(unsealed_data.begin(), unsealed_data.end());
  return true;
}

bool Tpm2Impl::CreateCertifiedKey(const SecureBlob& identity_key_blob,
                                  const SecureBlob& external_data,
                                  SecureBlob* certified_public_key,
                                  SecureBlob* certified_public_key_der,
                                  SecureBlob* certified_key_blob,
                                  SecureBlob* certified_key_info,
                                  SecureBlob* certified_key_proof) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return false;
}

bool Tpm2Impl::CreateDelegate(const std::set<uint32_t>& bound_pcrs,
                              uint8_t delegate_family_label,
                              uint8_t delegate_label,
                              Blob* delegate_blob,
                              Blob* delegate_secret) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return false;
}

bool Tpm2Impl::ActivateIdentity(const brillo::Blob& delegate_blob,
                                const brillo::Blob& delegate_secret,
                                const SecureBlob& identity_key_blob,
                                const SecureBlob& encrypted_asym_ca,
                                const SecureBlob& encrypted_sym_ca,
                                SecureBlob* identity_credential) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return false;
}

bool Tpm2Impl::TakeOwnership(int /*max_timeout_tries*/,
                             const SecureBlob& /*owner_password*/) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  if (IsOwned()) {
    LOG(INFO) << __func__ << ": TPM is already owned.";
    return true;
  }
  return tpm_manager_utility_->TakeOwnership();
}

bool Tpm2Impl::InitializeSrk(const SecureBlob& owner_password) {
  // Not necessary, tpm_managerd takes care of all this.
  LOG(WARNING) << __func__ << ": Not implemented.";
  return true;
}

bool Tpm2Impl::ChangeOwnerPassword(const SecureBlob& previous_owner_password,
                                   const SecureBlob& owner_password) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return false;
}

bool Tpm2Impl::TestTpmAuth(const SecureBlob& owner_password) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return false;
}

bool Tpm2Impl::Sign(const SecureBlob& key_blob,
                    const SecureBlob& input,
                    uint32_t bound_pcr_index,
                    SecureBlob* signature) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  trunks::AuthorizationDelegate* delegate;
  std::unique_ptr<trunks::PolicySession> policy_session;
  std::unique_ptr<trunks::HmacSession> hmac_session;
  TPM_RC result;
  if (bound_pcr_index != kNotBoundToPCR) {
    policy_session = trunks->factory->GetPolicySession();
    result = policy_session->StartUnboundSession(true, false);
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error starting policy session: " << GetErrorString(result);
      return false;
    }
    result = policy_session->PolicyPCR(
        std::map<uint32_t, std::string>({{bound_pcr_index, ""}}));
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error creating PCR policy: " << GetErrorString(result);
      return false;
    }
    delegate = policy_session->GetDelegate();
  } else {
    hmac_session = trunks->factory->GetHmacSession();
    result = hmac_session->StartUnboundSession(true, true);
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error starting hmac session: " << GetErrorString(result);
      return false;
    }
    hmac_session->SetEntityAuthorizationValue("");
    delegate = hmac_session->GetDelegate();
  }

  ScopedKeyHandle handle;
  TpmRetryAction retry = LoadWrappedKey(key_blob, &handle);
  if (retry != kTpmRetryNone) {
    LOG(ERROR) << "Error loading pcr bound key.";
    return false;
  }
  std::string tpm_signature;
  result = trunks->tpm_utility->Sign(
      handle.value(), trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_SHA256,
      input.to_string(), true /* generate_hash */, delegate, &tpm_signature);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error signing: " << GetErrorString(result);
    return false;
  }
  signature->assign(tpm_signature.begin(), tpm_signature.end());
  return true;
}

bool Tpm2Impl::CreatePCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                                 AsymmetricKeyUsage key_type,
                                 SecureBlob* key_blob,
                                 SecureBlob* public_key_der,
                                 SecureBlob* creation_blob) {
  CHECK(key_blob) << "No key blob argument provided.";
  CHECK(creation_blob) << "No creation blob argument provided.";
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  std::string policy_digest;
  TPM_RC result = trunks->tpm_utility->GetPolicyDigestForPcrValues(
      pcr_map, false /* use_auth_value */, &policy_digest);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting policy digest: " << GetErrorString(result);
    return false;
  }
  std::vector<uint32_t> pcr_list;
  for (const auto& map_pair : pcr_map) {
    pcr_list.push_back(map_pair.first);
  }
  std::string tpm_key_blob;
  std::string tpm_creation_blob;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      trunks->factory->GetPasswordAuthorization("");
  result = trunks->tpm_utility->CreateRSAKeyPair(
      key_type, kDefaultTpmRsaModulusSize, kDefaultTpmPublicExponent,
      "",  // No authorization
      policy_digest,
      true,  // use_only_policy_authorization
      pcr_list, delegate.get(), &tpm_key_blob,
      &tpm_creation_blob /* No creation_blob */);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error creating a pcr bound key: " << GetErrorString(result);
    return false;
  }
  key_blob->assign(tpm_key_blob.begin(), tpm_key_blob.end());
  creation_blob->assign(tpm_creation_blob.begin(), tpm_creation_blob.end());

  // if |public_key_der| is present, create and assign it.
  if (public_key_der) {
    trunks::TPM2B_PUBLIC public_data;
    trunks::TPM2B_PRIVATE private_data;
    if (!trunks->factory->GetBlobParser()->ParseKeyBlob(
            key_blob->to_string(), &public_data, &private_data)) {
      return false;
    }
    if (!PublicAreaToPublicKeyDER(public_data.public_area, public_key_der)) {
      return false;
    }
  }
  return true;
}

bool Tpm2Impl::VerifyPCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                                 const SecureBlob& key_blob,
                                 const SecureBlob& creation_blob) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  // First we verify that the PCR were in a known good state at the time of
  // Key creation.
  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPM2B_DIGEST creation_hash;
  trunks::TPMT_TK_CREATION creation_ticket;
  if (!trunks->factory->GetBlobParser()->ParseCreationBlob(
          creation_blob.to_string(), &creation_data, &creation_hash,
          &creation_ticket)) {
    LOG(ERROR) << "Error parsing creation_blob.";
    return false;
  }
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  if (pcr_select.count != 1) {
    LOG(ERROR) << "Creation data missing creation PCR value.";
    return false;
  }
  if (pcr_select.pcr_selections[0].hash != trunks::TPM_ALG_SHA256) {
    LOG(ERROR) << "Creation PCR extended with wrong hash algorithm.";
    return false;
  }
  uint8_t* pcr_selections = pcr_select.pcr_selections[0].pcr_select;
  if (pcr_map.size() != CountSetBits(pcr_selections, PCR_SELECT_MIN)) {
    LOG(ERROR) << "Incorrect creation PCR specified.";
    return false;
  }
  std::string concatenated_pcr_values;
  for (const auto& map_pair : pcr_map) {
    uint32_t pcr_index = map_pair.first;
    const std::string pcr_value = map_pair.second;
    if (pcr_index >= 8 * PCR_SELECT_MIN ||
        (pcr_selections[pcr_index / 8] & (1 << (pcr_index % 8))) == 0) {
      LOG(ERROR) << "Incorrect creation PCR specified.";
      return false;
    }
    concatenated_pcr_values += pcr_value;
  }
  Blob expected_pcr_digest =
      CryptoLib::Sha256(BlobFromString(concatenated_pcr_values));
  if (creation_data.creation_data.pcr_digest.size !=
      expected_pcr_digest.size()) {
    LOG(ERROR) << "Incorrect PCR digest size.";
    return false;
  }
  if (memcmp(creation_data.creation_data.pcr_digest.buffer,
             expected_pcr_digest.data(), expected_pcr_digest.size()) != 0) {
    LOG(ERROR) << "Incorrect PCR digest value.";
    return false;
  }
  // Then we certify that the key was created by the TPM.
  ScopedKeyHandle scoped_handle;
  if (LoadWrappedKey(key_blob, &scoped_handle) != Tpm::kTpmRetryNone) {
    return false;
  }
  TPM_RC result = trunks->tpm_utility->CertifyCreation(
      scoped_handle.value(), creation_blob.to_string());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error certifying that key was created by TPM: "
               << trunks::GetErrorString(result);
    return false;
  }
  // Finally we verify that the key's policy_digest is the expected value.
  std::unique_ptr<trunks::PolicySession> trial_session =
      trunks->factory->GetTrialSession();
  result = trial_session->StartUnboundSession(true, true);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting a trial session: " << GetErrorString(result);
    return false;
  }
  result = trial_session->PolicyPCR(pcr_map);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error restricting trial policy to pcr value: "
               << GetErrorString(result);
    return false;
  }
  std::string policy_digest;
  result = trial_session->GetDigest(&policy_digest);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting policy digest: " << GetErrorString(result);
    return false;
  }
  trunks::TPMT_PUBLIC public_area;
  result = trunks->tpm_utility->GetKeyPublicArea(scoped_handle.value(),
                                                 &public_area);
  if (result != TPM_RC_SUCCESS) {
    return false;
  }
  if (public_area.auth_policy.size != policy_digest.size()) {
    LOG(ERROR) << "Key auth policy and policy digest are of different length."
               << public_area.auth_policy.size << "," << policy_digest.size();
    return false;
  } else if (memcmp(public_area.auth_policy.buffer, policy_digest.data(),
                    policy_digest.size()) != 0) {
    LOG(ERROR) << "Key auth policy is different from policy digest.";
    return false;
  } else if (public_area.object_attributes & trunks::kUserWithAuth) {
    LOG(ERROR) << "Key authorization is not restricted to policy.";
    return false;
  }
  return true;
}

bool Tpm2Impl::ExtendPCR(uint32_t pcr_index, const Blob& extension) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      trunks->factory->GetPasswordAuthorization("");
  TPM_RC result = trunks->tpm_utility->ExtendPCR(
      pcr_index, BlobToString(extension), delegate.get());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error extending PCR: " << GetErrorString(result);
    return false;
  }
  return true;
}

bool Tpm2Impl::ReadPCR(uint32_t pcr_index, Blob* pcr_value) {
  CHECK(pcr_value);
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  std::string pcr_digest;
  TPM_RC result = trunks->tpm_utility->ReadPCR(pcr_index, &pcr_digest);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error reading from PCR: " << GetErrorString(result);
    return false;
  }
  *pcr_value = BlobFromString(pcr_digest);
  return true;
}

bool Tpm2Impl::WrapRsaKey(const SecureBlob& public_modulus,
                          const SecureBlob& prime_factor,
                          SecureBlob* wrapped_key) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  std::string key_blob;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      trunks->factory->GetPasswordAuthorization("");
  TPM_RC result = trunks->tpm_utility->ImportRSAKey(
      trunks::TpmUtility::kDecryptKey, public_modulus.to_string(),
      kDefaultTpmPublicExponent, prime_factor.to_string(),
      "",  // No authorization,
      delegate.get(), &key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error creating SRK wrapped key: " << GetErrorString(result);
    return false;
  }
  wrapped_key->assign(key_blob.begin(), key_blob.end());
  return true;
}

Tpm::TpmRetryAction Tpm2Impl::LoadWrappedKey(const SecureBlob& wrapped_key,
                                             ScopedKeyHandle* key_handle) {
  CHECK(key_handle);
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return Tpm::kTpmRetryFailNoRetry;
  }
  trunks::TPM_HANDLE handle;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      trunks->factory->GetPasswordAuthorization("");
  TPM_RC result = trunks->tpm_utility->LoadKey(wrapped_key.to_string(),
                                               delegate.get(), &handle);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error loading SRK wrapped key: " << GetErrorString(result);
    return ResultToRetryAction(result);
  }
  key_handle->reset(this, handle);
  return Tpm::kTpmRetryNone;
}

bool Tpm2Impl::LegacyLoadCryptohomeKey(ScopedKeyHandle* key_handle,
                                       SecureBlob* key_blob) {
  // This doesn't apply to devices with TPM 2.0.
  return false;
}

void Tpm2Impl::CloseHandle(TpmKeyHandle key_handle) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return;
  }
  TPM_RC result =
      trunks->factory->GetTpm()->FlushContextSync(key_handle, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(WARNING) << "Error flushing tpm handle " << key_handle << ": "
                 << GetErrorString(result);
  }
}

Tpm::TpmRetryAction Tpm2Impl::EncryptBlob(TpmKeyHandle key_handle,
                                          const SecureBlob& plaintext,
                                          const SecureBlob& key,
                                          SecureBlob* ciphertext) {
  CHECK(ciphertext);
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return Tpm::kTpmRetryFailNoRetry;
  }
  std::string tpm_ciphertext;
  TPM_RC result = trunks->tpm_utility->AsymmetricEncrypt(
      key_handle, trunks::TPM_ALG_OAEP, trunks::TPM_ALG_SHA256,
      plaintext.to_string(), nullptr, &tpm_ciphertext);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error encrypting plaintext: " << GetErrorString(result);
    return ResultToRetryAction(result);
  }
  if (!CryptoLib::ObscureRSAMessage(SecureBlob(tpm_ciphertext), key,
                                    ciphertext)) {
    LOG(ERROR) << "Error obscuring tpm encrypted blob.";
    return Tpm::kTpmRetryFailNoRetry;
  }
  return Tpm::kTpmRetryNone;
}

Tpm::TpmRetryAction Tpm2Impl::DecryptBlob(
    TpmKeyHandle key_handle,
    const SecureBlob& ciphertext,
    const SecureBlob& key,
    const std::map<uint32_t, std::string>& pcr_map,
    SecureBlob* plaintext) {
  CHECK(plaintext);
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return Tpm::kTpmRetryFailNoRetry;
  }
  SecureBlob local_data;
  if (!CryptoLib::UnobscureRSAMessage(ciphertext, key, &local_data)) {
    LOG(ERROR) << "Error unobscureing message.";
    return Tpm::kTpmRetryFailNoRetry;
  }
  trunks::AuthorizationDelegate* delegate;
  std::unique_ptr<trunks::PolicySession> policy_session;
  std::unique_ptr<trunks::AuthorizationDelegate> default_delegate;
  if (!pcr_map.empty()) {
    policy_session = trunks->factory->GetPolicySession();
    TPM_RC result = policy_session->StartUnboundSession(true, true);
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error starting policy session: " << GetErrorString(result);
      return Tpm::kTpmRetryFailNoRetry;
    }
    result = policy_session->PolicyPCR(pcr_map);
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error creating PCR policy: " << GetErrorString(result);
      return Tpm::kTpmRetryFailNoRetry;
    }
    delegate = policy_session->GetDelegate();
  } else {
    default_delegate = trunks->factory->GetPasswordAuthorization("");
    delegate = default_delegate.get();
  }

  std::string tpm_plaintext;
  TPM_RC result = trunks->tpm_utility->AsymmetricDecrypt(
      key_handle, trunks::TPM_ALG_OAEP, trunks::TPM_ALG_SHA256,
      local_data.to_string(), delegate, &tpm_plaintext);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error decrypting plaintext: " << GetErrorString(result);
    return ResultToRetryAction(result);
  }
  plaintext->assign(tpm_plaintext.begin(), tpm_plaintext.end());
  return Tpm::kTpmRetryNone;
}

Tpm::TpmRetryAction Tpm2Impl::SealToPcrWithAuthorization(
    TpmKeyHandle key_handle,
    const SecureBlob& plaintext,
    const SecureBlob& auth_blob,
    const std::map<uint32_t, std::string>& pcr_map,
    SecureBlob* sealed_data) {
  // Get the auth_value.
  std::string auth_value;
  if (!GetAuthValue(key_handle, auth_blob, &auth_value)) {
    return Tpm::kTpmRetryFailNoRetry;
  }

  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return Tpm::kTpmRetryFailNoRetry;
  }

  // Get the policy digest for PCR.
  std::string policy_digest;
  TPM_RC result = trunks->tpm_utility->GetPolicyDigestForPcrValues(
      pcr_map, true /* use_auth_value */, &policy_digest);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting policy digest: " << GetErrorString(result);
    return ResultToRetryAction(result);
  }

  std::unique_ptr<trunks::HmacSession> session =
      trunks->factory->GetHmacSession();
  result = session->StartUnboundSession(true, true);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting hmac session: " << GetErrorString(result);
    return ResultToRetryAction(result);
  }

  std::string sealed_str;
  result = trunks->tpm_utility->SealData(plaintext.to_string(), policy_digest,
                                         auth_value, session->GetDelegate(),
                                         &sealed_str);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error sealing data to PCR with authorization: "
               << GetErrorString(result);
    return ResultToRetryAction(result);
  }
  sealed_data->assign(sealed_str.begin(), sealed_str.end());

  return Tpm::kTpmRetryNone;
}

Tpm::TpmRetryAction Tpm2Impl::UnsealWithAuthorization(
    TpmKeyHandle key_handle,
    const SecureBlob& sealed_data,
    const SecureBlob& auth_blob,
    const std::map<uint32_t, std::string>& pcr_map,
    SecureBlob* plaintext) {
  std::string auth_value;
  if (!GetAuthValue(key_handle, auth_blob, &auth_value)) {
    return Tpm::kTpmRetryFailNoRetry;
  }

  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return Tpm::kTpmRetryFailNoRetry;
  }

  std::unique_ptr<trunks::PolicySession> policy_session =
      trunks->factory->GetPolicySession();
  // Use unsalted session here, to unseal faster.
  TPM_RC result = policy_session->StartUnboundSession(false, false);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting policy session: " << GetErrorString(result);
    return ResultToRetryAction(result);
  }
  result = policy_session->PolicyAuthValue();
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": Error setting session to use auth_value: "
               << GetErrorString(result);
    return ResultToRetryAction(result);
  }
  result = policy_session->PolicyPCR(pcr_map);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error in PolicyPCR: " << GetErrorString(result);
    return ResultToRetryAction(result);
  }
  policy_session->SetEntityAuthorizationValue(auth_value);
  std::string unsealed_data;
  result = trunks->tpm_utility->UnsealData(
      sealed_data.to_string(), policy_session->GetDelegate(), &unsealed_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error unsealing data with authorization: "
               << GetErrorString(result);
    return ResultToRetryAction(result);
  }
  plaintext->assign(unsealed_data.begin(), unsealed_data.end());

  return Tpm::kTpmRetryNone;
}

Tpm::TpmRetryAction Tpm2Impl::GetPublicKeyHash(TpmKeyHandle key_handle,
                                               SecureBlob* hash) {
  CHECK(hash);
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return Tpm::kTpmRetryFailNoRetry;
  }
  trunks::TPMT_PUBLIC public_data;
  TPM_RC result =
      trunks->tpm_utility->GetKeyPublicArea(key_handle, &public_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting key public area: " << GetErrorString(result);
    return ResultToRetryAction(result);
  }
  std::string public_modulus =
      trunks::StringFrom_TPM2B_PUBLIC_KEY_RSA(public_data.unique.rsa);
  *hash = CryptoLib::Sha256(SecureBlob(public_modulus));
  return Tpm::kTpmRetryNone;
}

void Tpm2Impl::GetStatus(TpmKeyHandle key, TpmStatusInfo* status) {
  memset(status, 0, sizeof(TpmStatusInfo));
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return;
  }
  status->this_instance_has_context = true;
  status->this_instance_has_key_handle = (key != 0);
  status->last_tpm_error = trunks->tpm_state->Initialize();
  if (status->last_tpm_error != TPM_RC_SUCCESS) {
    return;
  }
  status->can_connect = true;
  trunks::TPMT_PUBLIC public_srk;
  status->last_tpm_error = trunks->tpm_utility->GetKeyPublicArea(
      trunks::kStorageRootKey, &public_srk);
  if (status->last_tpm_error != TPM_RC_SUCCESS) {
    return;
  }
  status->can_load_srk = true;
  status->can_load_srk_public_key = true;
  status->srk_vulnerable_roca = false;

  // Check the Cryptohome key by using what we have been told.
  status->has_cryptohome_key = (key != 0);

  if (status->has_cryptohome_key) {
    // Check encryption (we don't care about the contents, just whether or not
    // there was an error)
    SecureBlob data(16);
    SecureBlob password(16);
    SecureBlob salt(8);
    SecureBlob data_out(16);
    memset(data.data(), 'A', data.size());
    memset(password.data(), 'B', password.size());
    memset(salt.data(), 'C', salt.size());
    memset(data_out.data(), 'D', data_out.size());
    SecureBlob aes_key;
    CryptoLib::PasskeyToAesKey(password, salt, 13, &aes_key, NULL);
    if (EncryptBlob(key, data, aes_key, &data_out) != kTpmRetryNone) {
      return;
    }
    status->can_encrypt = true;

    // Check decryption (we don't care about the contents, just whether or not
    // there was an error)
    if (DecryptBlob(key, data_out, aes_key, std::map<uint32_t, std::string>(),
                    &data) != kTpmRetryNone) {
      return;
    }
    status->can_decrypt = true;
  }
}

base::Optional<bool> Tpm2Impl::IsSrkRocaVulnerable() {
  // This doesn't apply to devices with TPM 2.0.
  return false;
}

bool Tpm2Impl::GetDictionaryAttackInfo(int* counter,
                                       int* threshold,
                                       bool* lockout,
                                       int* seconds_remaining) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->GetDictionaryAttackInfo(
      counter, threshold, lockout, seconds_remaining);
}

bool Tpm2Impl::ResetDictionaryAttackMitigation(
    const Blob& /* delegate_blob */, const Blob& /* delegate_secret */) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->ResetDictionaryAttackLock();
}

void Tpm2Impl::DeclareTpmFirmwareStable() {
  TrunksClientContext* trunks;
  if (!fw_declared_stable_ && GetTrunksContext(&trunks)) {
    TPM_RC res = trunks->tpm_utility->DeclareTpmFirmwareStable();
    fw_declared_stable_ = (res == TPM_RC_SUCCESS);
  }
}

bool Tpm2Impl::GetTrunksContext(TrunksClientContext** trunks) {
  if (has_external_trunks_context_) {
    *trunks = &external_trunks_context_;
    return true;
  }
  base::PlatformThreadId thread_id = base::PlatformThread::CurrentId();
  if (trunks_contexts_.count(thread_id) == 0) {
    std::unique_ptr<TrunksClientContext> new_context(new TrunksClientContext);
    new_context->factory_impl = std::make_unique<trunks::TrunksFactoryImpl>();
    if (!new_context->factory_impl->Initialize()) {
      LOG(ERROR) << "Failed to initialize trunks factory.";
      return false;
    }
    new_context->factory = new_context->factory_impl.get();
    new_context->tpm_state = new_context->factory->GetTpmState();
    new_context->tpm_utility = new_context->factory->GetTpmUtility();
    trunks_contexts_[thread_id] = std::move(new_context);
  }
  *trunks = trunks_contexts_[thread_id].get();
  return true;
}

bool Tpm2Impl::LoadPublicKeyFromSpki(
    const Blob& public_key_spki_der,
    AsymmetricKeyUsage key_type,
    trunks::TPM_ALG_ID scheme,
    trunks::TPM_ALG_ID hash_alg,
    trunks::AuthorizationDelegate* session_delegate,
    ScopedKeyHandle* key_handle) {
  // Parse the SPKI.
  const unsigned char* asn1_ptr = public_key_spki_der.data();
  const crypto::ScopedEVP_PKEY pkey(
      d2i_PUBKEY(nullptr, &asn1_ptr, public_key_spki_der.size()));
  if (!pkey) {
    LOG(ERROR) << "Error parsing Subject Public Key Info DER";
    return false;
  }
  const crypto::ScopedRSA rsa(EVP_PKEY_get1_RSA(pkey.get()));
  if (!rsa) {
    LOG(ERROR) << "Error: non-RSA key was supplied";
    return false;
  }
  SecureBlob key_modulus(RSA_size(rsa.get()));
  const BIGNUM* n;
  const BIGNUM* e;
  RSA_get0_key(rsa.get(), &n, &e, nullptr);
  if (BN_bn2bin(n, key_modulus.data()) != key_modulus.size()) {
    LOG(ERROR) << "Error extracting public key modulus";
    return false;
  }
  constexpr BN_ULONG kInvalidBnWord = ~static_cast<BN_ULONG>(0);
  const BN_ULONG exponent_word = BN_get_word(e);
  if (exponent_word == kInvalidBnWord ||
      !base::IsValueInRangeForNumericType<uint32_t>(exponent_word)) {
    LOG(ERROR) << "Error extracting public key exponent";
    return false;
  }
  const uint32_t key_exponent = static_cast<uint32_t>(exponent_word);
  // Load the key into the TPM.
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks))
    return false;
  trunks::TPM_HANDLE key_handle_raw = 0;
  TPM_RC tpm_result = trunks->tpm_utility->LoadRSAPublicKey(
      key_type, scheme, hash_alg, key_modulus.to_string(), key_exponent,
      session_delegate, &key_handle_raw);
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error loading public key: " << GetErrorString(tpm_result);
    return false;
  }
  key_handle->reset(this, key_handle_raw);
  return true;
}

void Tpm2Impl::HandleOwnershipTakenEvent() {
  // Not necessary, tpm_manager client takes care of this.
}

bool Tpm2Impl::PublicAreaToPublicKeyDER(const trunks::TPMT_PUBLIC& public_area,
                                        brillo::SecureBlob* public_key_der) {
  crypto::ScopedRSA rsa(RSA_new());
  crypto::ScopedBIGNUM e(BN_new()), n(BN_new());
  if (!rsa || !e || !n) {
    LOG(ERROR) << "Failed to allocate RSA or BIGNUM for public key.";
    return false;
  }
  if (!BN_set_word(e.get(), kDefaultTpmPublicExponent) ||
      !BN_bin2bn(public_area.unique.rsa.buffer, public_area.unique.rsa.size,
                 n.get()) ||
      !RSA_set0_key(rsa.get(), n.release(), e.release(), nullptr)) {
    LOG(ERROR) << "Failed to set up RSA.";
    return false;
  }
  int der_length = i2d_RSAPublicKey(rsa.get(), nullptr);
  if (der_length < 0) {
    LOG(ERROR) << "Failed to get DER-encoded public key length.";
    return false;
  }
  public_key_der->resize(der_length);
  unsigned char* der_buffer = public_key_der->data();
  der_length = i2d_RSAPublicKey(rsa.get(), &der_buffer);
  if (der_length < 0) {
    LOG(ERROR) << "Failed to DER-encode public key.";
    return false;
  }
  return true;
}

bool Tpm2Impl::GetAuthValue(TpmKeyHandle key_handle,
                            const SecureBlob& pass_blob,
                            std::string* auth_value) {
  if (pass_blob.size() != kDefaultTpmRsaModulusSize / 8) {
    return false;
  }

  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }

  // To guarantee that pass_blob is lower that public key modulus, just set the
  // first byte to 0.
  std::string value_to_decrypt = pass_blob.to_string();
  value_to_decrypt[0] = 0;
  std::string decrypted_value;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      trunks->factory->GetPasswordAuthorization("");
  TPM_RC result = result = trunks->tpm_utility->AsymmetricDecrypt(
      key_handle, trunks::TPM_ALG_NULL, trunks::TPM_ALG_NULL, value_to_decrypt,
      delegate.get(), &decrypted_value);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error decrypting pass_blob: " << GetErrorString(result);
    return false;
  }
  *auth_value = CryptoLib::Sha256(SecureBlob(decrypted_value)).to_string();

  return true;
}

bool Tpm2Impl::UpdateTpmStatus(RefreshType refresh_type) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }

  bool is_successful = false;
  bool has_received = false;

  // Repeats data copy into |last_tpm_manager_data_|; reasonable trade-off due
  // to low ROI to avoid that.
  const bool is_connected = tpm_manager_utility_->GetOwnershipTakenSignalStatus(
      &is_successful, &has_received, &last_tpm_manager_data_);

  // When we need explicitly query tpm status either because the signal is not
  // ready for any reason, or because the signal is not received yet so we need
  // to run it once in case the signal is sent by tpm_manager before already.
  if (refresh_type == RefreshType::FORCE_REFRESH || !is_connected ||
      !is_successful || (!has_received && shall_cache_tpm_manager_status_)) {
    // Retains |shall_cache_tpm_manager_status_| to be |true| if the signal
    // cannot be relied on (yet). Actually |!is_successful| suffices to update
    // |shall_cache_tpm_manager_status_|; by design, uses the redundancy just to
    // avoid confusion.
    shall_cache_tpm_manager_status_ &= (!is_connected || !is_successful);
    return CacheTpmManagerStatus();
  } else if (has_received) {
    is_enabled_ = true;
    is_owned_ = true;
  }
  return true;
}

bool Tpm2Impl::RemoveOwnerDependency(
    TpmPersistentState::TpmOwnerDependency dependency) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->RemoveOwnerDependency(
      OwnerDependencyEnumClassToString(dependency));
}

bool Tpm2Impl::ClearStoredPassword() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->ClearStoredOwnerPassword();
}

bool Tpm2Impl::GetVersionInfo(TpmVersionInfo* version_info) {
  if (!version_info) {
    LOG(ERROR) << __func__ << "version_info is not initialized.";
    return false;
  }

  // Version info on a device never changes. Returns from cache directly if we
  // have the cache.
  if (version_info_) {
    *version_info = *version_info_;
    return true;
  }

  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }

  if (!tpm_manager_utility_->GetVersionInfo(
          &version_info->family, &version_info->spec_level,
          &version_info->manufacturer, &version_info->tpm_model,
          &version_info->firmware_version, &version_info->vendor_specific)) {
    LOG(ERROR) << __func__ << ": failed to get version info from tpm_manager.";
    return false;
  }

  version_info_ = *version_info;
  return true;
}

bool Tpm2Impl::GetIFXFieldUpgradeInfo(IFXFieldUpgradeInfo* info) {
  return false;
}

bool Tpm2Impl::SetUserType(Tpm::UserType type) {
  VLOG(1) << __func__ << ": " << static_cast<int>(cur_user_type_) << " -> "
          << static_cast<int>(type);
  if (cur_user_type_ == UserType::NonOwner || cur_user_type_ == type) {
    // It's not possible to transition from NonOwner to Owner, so don't even
    // try. Also, if we are already in the desired state, no reason to
    // repeat sending the command.
    VLOG(1) << "ManageCCDPwd not needed";
    return true;
  }
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    LOG(WARNING) << "Failed to obtain trunks context to set user type";
    cur_user_type_ = Tpm::UserType::Unknown;
    return false;
  }
  TPM_RC res =
      trunks->tpm_utility->ManageCCDPwd(type == UserType::Owner ? true : false);
  if (res != TPM_RC_SUCCESS) {
    cur_user_type_ = Tpm::UserType::Unknown;
    if (res == kCr50ErrorNoSuchCommand) {
      // In case we deal with Cr50 revision that doesn't support the command
      // yet. We will still keep trying on each login.
      LOG(WARNING) << "ManageCCDPwd is not supported";
      return true;
    } else if (type == UserType::Owner) {
      // If we fail to transition to Owner, that's fine. Worst thing that
      // happens in this case: the owner user fails to set a CCD password until
      // reboot. We only need to make sure that we don't let users sign in if
      // setting NonOwner state fails.
      LOG(WARNING) << "ManageCCDPwd(Owner) failed, ignoring: error=0x"
                   << std::hex << res;
      return true;
    } else {
      LOG(ERROR) << "ManageCCDPwd(NonOwner) failed: error=0x" << std::hex
                 << res;
      return false;
    }
  }
  VLOG(1) << "ManageCCDPwd succeeded";
  cur_user_type_ = type;
  return true;
}

bool Tpm2Impl::GetRsuDeviceId(std::string* device_id) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  return trunks->tpm_utility->GetRsuDeviceId(device_id) == TPM_RC_SUCCESS;
}

LECredentialBackend* Tpm2Impl::GetLECredentialBackend() {
#if USE_PINWEAVER
  return &le_credential_backend_;
#else
  return nullptr;
#endif
}

SignatureSealingBackend* Tpm2Impl::GetSignatureSealingBackend() {
  return &signature_sealing_backend_;
}

bool Tpm2Impl::GetDelegate(brillo::Blob* /*blob*/,
                           brillo::Blob* /*secret*/,
                           bool* has_reset_lock_permissions) {
  LOG(WARNING) << __func__ << ": No-ops to |blob| and |secret|.";
  *has_reset_lock_permissions = true;
  return true;
}

bool Tpm2Impl::DoesUseTpmManager() {
  return true;
}

bool Tpm2Impl::IsCurrentPCR0ValueValid() {
  return true;
}

base::Optional<bool> Tpm2Impl::IsDelegateBoundToPcr() {
  return false;
}

bool Tpm2Impl::DelegateCanResetDACounter() {
  return true;
}

std::map<uint32_t, std::string> Tpm2Impl::GetPcrMap(
    const std::string& obfuscated_username, bool use_extended_pcr) const {
  std::map<uint32_t, std::string> pcr_map;
  if (use_extended_pcr) {
    brillo::SecureBlob starting_value(SHA256_DIGEST_LENGTH, 0);
    brillo::SecureBlob digest_value =
        CryptoLib::Sha256(brillo::SecureBlob::Combine(
            starting_value,
            CryptoLib::Sha256(brillo::SecureBlob(obfuscated_username))));
    pcr_map[kTpmSingleUserPCR] = digest_value.to_string();
  } else {
    pcr_map[kTpmSingleUserPCR] = std::string(SHA256_DIGEST_LENGTH, 0);
  }

  return pcr_map;
}

}  // namespace cryptohome
