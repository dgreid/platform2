// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Tpm

#include "cryptohome/tpm_impl.h"

#include <algorithm>

#include <arpa/inet.h>
#include <base/memory/scoped_ptr.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <base/values.h>
#include <crypto/scoped_openssl_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>  // NOLINT(build/include_alpha) - needs tss.h

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"

using base::PlatformThread;
using brillo::SecureBlob;
using trousers::ScopedTssContext;
using trousers::ScopedTssKey;
using trousers::ScopedTssMemory;
using trousers::ScopedTssNvStore;
using trousers::ScopedTssObject;
using trousers::ScopedTssPcrs;
using trousers::ScopedTssPolicy;

namespace {

typedef scoped_ptr<BYTE, base::FreeDeleter> ScopedByteArray;

// The DER encoding of SHA-256 DigestInfo as defined in PKCS #1.
const unsigned char kSha256DigestInfo[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03,
    0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
};

// This is the well known UUID present in TPM1.2 implemenations. It is used
// to load the cryptohome key into a TPM1.2 in a legacy path.
const TSS_UUID kCryptohomeWellKnownUuid = {0x0203040b, 0, 0, 0, 0,
                                           {0, 9, 8, 1, 0, 3}};

cryptohome::Tpm::TpmRetryAction ResultToRetryAction(TSS_RESULT result) {
  cryptohome::Tpm::TpmRetryAction status = cryptohome::Tpm::kTpmRetryFatal;
  cryptohome::ReportTpmResult(result);
  switch (ERROR_CODE(result)) {
    case ERROR_CODE(TSS_SUCCESS):
      status = cryptohome::Tpm::kTpmRetryNone;
      break;
    case ERROR_CODE(TSS_E_COMM_FAILURE):
      LOG(ERROR) << "Communications failure with the TPM.";
      ReportCryptohomeError(cryptohome::kTssCommunicationFailure);
      status = cryptohome::Tpm::kTpmRetryCommFailure;
      break;
    case ERROR_CODE(TSS_E_INVALID_HANDLE):
      LOG(ERROR) << "Invalid handle to the TPM.";
      ReportCryptohomeError(cryptohome::kTssInvalidHandle);
      status = cryptohome::Tpm::kTpmRetryInvalidHandle;
      break;
    case ERROR_CODE(TCS_E_KM_LOADFAILED):
      LOG(ERROR) << "Key load failed; problem with parent key authorization.";
      ReportCryptohomeError(cryptohome::kTcsKeyLoadFailed);
      status = cryptohome::Tpm::kTpmRetryLoadFail;
      break;
    case ERROR_CODE(TPM_E_DEFEND_LOCK_RUNNING):
      LOG(ERROR) << "The TPM is defending itself against possible dictionary "
                 << "attacks.";
      ReportCryptohomeError(cryptohome::kTpmDefendLockRunning);
      status = cryptohome::Tpm::kTpmRetryDefendLock;
      break;
    // This error code occurs when the TPM is in an error state.
    case ERROR_CODE(TPM_E_FAIL):
      status = cryptohome::Tpm::kTpmRetryReboot;
      ReportCryptohomeError(cryptohome::kTpmFail);
      LOG(ERROR) << "The TPM returned TPM_E_FAIL.  A reboot is required.";
      break;
    default:
      status = cryptohome::Tpm::kTpmRetryFailNoRetry;
      break;
  }
  return status;
}

}  // namespace

namespace cryptohome {

#define TPM_LOG(severity, result) \
  LOG(severity) << "TPM error 0x" << std::hex << result \
                << " (" << Trspi_Error_String(result) << "): "

const unsigned char kDefaultSrkAuth[] = { };
const unsigned int kDefaultTpmRsaKeyBits = 2048;
const unsigned int kDefaultTpmRsaKeyFlag = TSS_KEY_SIZE_2048;
const unsigned int kDefaultDiscardableWrapPasswordLength = 32;

const char* kWellKnownSrkTmp = "1234567890";
const unsigned int kTpmConnectRetries = 10;
const unsigned int kTpmConnectIntervalMs = 100;
const unsigned int kTpmBootPCR = 0;
const unsigned int kTpmPCRLocality = 1;
const int kDelegateSecretSize = 20;
const int kDelegateFamilyLabel = 1;
const int kDelegateEntryLabel = 2;
const size_t kPCRExtensionSize = 20;  // SHA-1 digest size.

// This error is returned when an attempt is made to use the SRK but it does not
// yet exist because the TPM has not been owned.
const TSS_RESULT kKeyNotFoundError = (TSS_E_PS_KEY_NOTFOUND | TSS_LAYER_TCS);

TpmImpl::TpmImpl()
    : initialized_(false),
      srk_auth_(kDefaultSrkAuth, kDefaultSrkAuth + sizeof(kDefaultSrkAuth)),
      owner_password_(),
      password_sync_lock_(),
      is_disabled_(true),
      is_owned_(false),
      is_being_owned_(false) {
  TSS_HCONTEXT context_handle = ConnectContext();
  if (context_handle) {
    tpm_context_.reset(0, context_handle);
  }
}

TpmImpl::~TpmImpl() { }

TSS_HCONTEXT TpmImpl::ConnectContext() {
  TSS_RESULT result;
  TSS_HCONTEXT context_handle = 0;
  if (!OpenAndConnectTpm(&context_handle, &result)) {
    return 0;
  }
  return context_handle;
}

bool TpmImpl::ConnectContextAsOwner(TSS_HCONTEXT* context, TSS_HTPM* tpm) {
  *context = 0;
  *tpm = 0;
  if (owner_password_.size() == 0) {
    LOG(ERROR) << "ConnectContextAsOwner requires an owner password";
    return false;
  }

  if (!is_owned_ || is_being_owned_) {
    LOG(ERROR) << "ConnectContextAsOwner: TPM is unowned or still being owned";
    return false;
  }

  if ((*context = ConnectContext()) == 0) {
    LOG(ERROR) << "ConnectContextAsOwner: Could not open the TPM";
    return false;
  }

  if (!GetTpmWithAuth(*context, owner_password_, tpm)) {
    LOG(ERROR) << "ConnectContextAsOwner: failed to authorize as the owner";
    Tspi_Context_Close(*context);
    *context = 0;
    *tpm = 0;
    return false;
  }
  return true;
}

bool TpmImpl::ConnectContextAsUser(TSS_HCONTEXT* context, TSS_HTPM* tpm) {
  *context = 0;
  *tpm = 0;
  if ((*context = ConnectContext()) == 0) {
    LOG(ERROR) << "ConnectContextAsUser: Could not open the TPM";
    return false;
  }
  if (!GetTpm(*context, tpm)) {
    LOG(ERROR) << "ConnectContextAsUser: failed to get a TPM object";
    Tspi_Context_Close(*context);
    *context = 0;
    *tpm = 0;
    return false;
  }
  return true;
}

bool TpmImpl::ConnectContextAsDelegate(const SecureBlob& delegate_blob,
                                       const SecureBlob& delegate_secret,
                                       TSS_HCONTEXT* context, TSS_HTPM* tpm) {
  *context = 0;
  *tpm = 0;
  if (!is_owned_ || is_being_owned_) {
    LOG(ERROR) << "ConnectContextAsDelegate: TPM is unowned.";
    return false;
  }
  if ((*context = ConnectContext()) == 0) {
    LOG(ERROR) << "ConnectContextAsDelegate: Could not open the TPM.";
    return false;
  }
  if (!GetTpmWithDelegation(*context, delegate_blob, delegate_secret, tpm)) {
    LOG(ERROR) << "ConnectContextAsDelegate: Failed to authorize.";
    Tspi_Context_Close(*context);
    *context = 0;
    *tpm = 0;
    return false;
  }
  return true;
}

void TpmImpl::GetStatus(TpmKeyHandle key_handle,
                        TpmStatusInfo* status) {
  memset(status, 0, sizeof(TpmStatusInfo));
  status->this_instance_has_context = (tpm_context_.value() != 0);
  status->this_instance_has_key_handle = (key_handle != 0);
  ScopedTssContext context_handle;
  // Check if we can connect
  TSS_RESULT result;
  if (!OpenAndConnectTpm(context_handle.ptr(), &result)) {
    status->last_tpm_error = result;
    return;
  }
  status->can_connect = true;

  // Check the Storage Root Key
  ScopedTssKey srk_handle(context_handle);
  if (!LoadSrk(context_handle, srk_handle.ptr(), &result)) {
    status->last_tpm_error = result;
    return;
  }
  status->can_load_srk = true;

  // Check the SRK public key
  unsigned int size_n;
  ScopedTssMemory public_srk(context_handle);
  if (TPM_ERROR(result = Tspi_Key_GetPubKey(srk_handle, &size_n,
                                            public_srk.ptr()))) {
    status->last_tpm_error = result;
    return;
  }
  status->can_load_srk_public_key = true;

  // Check the Cryptohome key by using what we have been told.
  status->has_cryptohome_key = (tpm_context_.value() != 0) && (key_handle != 0);

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
    SecureBlob key;
    CryptoLib::PasskeyToAesKey(password, salt, 13, &key, NULL);
    if (EncryptBlob(key_handle, data, key, &data_out) != kTpmRetryNone) {
      return;
    }
    status->can_encrypt = true;

    // Check decryption (we don't care about the contents, just whether or not
    // there was an error)
    if (DecryptBlob(key_handle, data_out, key, &data) != kTpmRetryNone) {
      return;
    }
    status->can_decrypt = true;
  }
}

bool TpmImpl::GetDictionaryAttackInfo(int* counter,
                                      int* threshold,
                                      bool* lockout,
                                      int* seconds_remaining) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << __func__ << ": Failed to connect to the TPM.";
    return false;
  }
  brillo::Blob capability_data;
  if (!GetCapability(context_handle,
                     tpm_handle,
                     TSS_TPMCAP_DA_LOGIC,
                     TPM_ET_KEYHANDLE,
                     &capability_data,
                     NULL)) {
    LOG(ERROR) << __func__ << ": Failed to query DA_LOGIC capability.";
    return false;
  }
  if (static_cast<UINT16>(capability_data[1]) == TPM_TAG_DA_INFO) {
    TPM_DA_INFO da_info;
    UINT64 offset = 0;
    Trspi_UnloadBlob_DA_INFO(&offset, capability_data.data(), &da_info);
    VLOG(1) << "DA_INFO for TPM_ET_KEYHANDLE:";
    VLOG(1) << "  Active: " << static_cast<int>(da_info.state);
    VLOG(1) << "  Counter: " << da_info.currentCount;
    VLOG(1) << "  Threshold: " << da_info.thresholdCount;
    VLOG(1) << "  Action: " << da_info.actionAtThreshold.actions;
    VLOG(1) << "  Action Value: " << da_info.actionDependValue;
    VLOG(1) << "  Vendor Data Size: " << da_info.vendorDataSize;
    if (da_info.vendorDataSize > 0) {
      VLOG(1) << "  Vendor Data: "
              << base::HexEncode(da_info.vendorData,
                                 da_info.vendorDataSize);
    }
    *counter = da_info.currentCount;
    *threshold = da_info.thresholdCount;
    *lockout = (da_info.state == TPM_DA_STATE_ACTIVE);
    *seconds_remaining = da_info.actionDependValue;
    free(da_info.vendorData);
  } else {
    LOG(WARNING) << __func__ << ": Cannot read counter.";
  }
  // For Infineon, pull the counter out of vendor-specific data, and check if it
  // matches the value in DA_INFO.
  UINT32 manufacturer;
  if (!GetCapability(context_handle,
                     tpm_handle,
                     TSS_TPMCAP_PROPERTY,
                     TSS_TPMCAP_PROP_MANUFACTURER,
                     NULL,
                     &manufacturer)) {
    LOG(ERROR) << __func__ << ": Failed to query TSS_TPMCAP_PROP_MANUFACTURER.";
    return false;
  }
  const UINT32 kInfineon = 0x49465800;
  if (manufacturer == kInfineon) {
    brillo::Blob capability_data;
    if (!GetCapability(context_handle,
                       tpm_handle,
                       TSS_TPMCAP_MFR,
                       0x00000802,  // Opaque vendor-specific bits.
                       &capability_data,
                       NULL)) {
      LOG(ERROR) << __func__ << ": Failed to query MFR capability.";
      return false;
    }
    const size_t kInfineonCounterOffset = 9;
    if (capability_data.size() > kInfineonCounterOffset) {
      if (*counter != capability_data[kInfineonCounterOffset]) {
        LOG(WARNING) << __func__ << ": Counter mismatch: " << *counter << " vs "
                     << capability_data[kInfineonCounterOffset];
        *counter = std::max(*counter, static_cast<int>(
            capability_data[kInfineonCounterOffset]));
      }
      VLOG(1) << __func__ << ": " << counter;
    } else {
      LOG(WARNING) << __func__ << ": Cannot read counter.";
    }
  }
  return true;
}

bool TpmImpl::ResetDictionaryAttackMitigation(
    const brillo::SecureBlob& delegate_blob,
    const brillo::SecureBlob& delegate_secret) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsDelegate(delegate_blob, delegate_secret,
                                context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << __func__ << ": Failed to connect to the TPM.";
    return false;
  }
  TSS_RESULT result = Tspi_TPM_SetStatus(tpm_handle,
                                         TSS_TPMSTATUS_RESETLOCK,
                                         true /* Will be ignored. */);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to reset lock.";
    return false;
  }
  LOG(WARNING) << "Dictionary attack mitigation has been reset.";
  return true;
}

bool TpmImpl::IsTransient(TpmRetryAction retry_action) {
  bool transient = false;
  switch (retry_action) {
    case kTpmRetryCommFailure:
    case kTpmRetryInvalidHandle:
    case kTpmRetryDefendLock:
    // TODO(fes): We're considering this a transient failure for now
    case kTpmRetryLoadFail:
    case kTpmRetryFatal:
      transient = true;
      break;
    default:
      break;
  }
  return transient;
}

bool TpmImpl::OpenAndConnectTpm(TSS_HCONTEXT* context_handle,
                                TSS_RESULT* result) {
  TSS_RESULT local_result;
  ScopedTssContext local_context_handle;
  if (TPM_ERROR(local_result = Tspi_Context_Create(
                                                local_context_handle.ptr()))) {
    TPM_LOG(ERROR, local_result) << "Error calling Tspi_Context_Create";
    if (result)
      *result = local_result;
    return false;
  }

  for (unsigned int i = 0; i < kTpmConnectRetries; i++) {
    if (TPM_ERROR(local_result = Tspi_Context_Connect(local_context_handle,
                                                      NULL))) {
      // If there was a communications failure, try sleeping a bit here--it may
      // be that tcsd is still starting
      if (ERROR_CODE(local_result) == TSS_E_COMM_FAILURE) {
        PlatformThread::Sleep(
            base::TimeDelta::FromMilliseconds(kTpmConnectIntervalMs));
      } else {
        TPM_LOG(ERROR, local_result) << "Error calling Tspi_Context_Connect";
        if (result)
          *result = local_result;
        return false;
      }
    } else {
      break;
    }
  }
  if (TPM_ERROR(local_result)) {
    TPM_LOG(ERROR, local_result) << "Error calling Tspi_Context_Connect";
    if (result)
      *result = local_result;
    return false;
  }

  *context_handle = local_context_handle.release();
  if (result)
    *result = local_result;
  return (*context_handle != 0);
}

Tpm::TpmRetryAction TpmImpl::GetPublicKeyHash(TpmKeyHandle key_handle,
                                              SecureBlob* hash) {
  TSS_RESULT result = TSS_SUCCESS;
  SecureBlob pubkey;
  if (!GetPublicKeyBlob(tpm_context_.value(), key_handle, &pubkey, &result)) {
    return ResultToRetryAction(result);
  }
  *hash = CryptoLib::Sha1(pubkey);
  return kTpmRetryNone;
}

Tpm::TpmRetryAction TpmImpl::EncryptBlob(TpmKeyHandle key_handle,
                                         const SecureBlob& plaintext,
                                         const SecureBlob& key,
                                         SecureBlob* ciphertext) {
  TSS_RESULT result = TSS_SUCCESS;
  TSS_FLAG init_flags = TSS_ENCDATA_SEAL;
  ScopedTssKey enc_handle(tpm_context_.value());
  if (TPM_ERROR(result = Tspi_Context_CreateObject(tpm_context_.value(),
                                                   TSS_OBJECT_TYPE_ENCDATA,
                                                   init_flags,
                                                   enc_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Context_CreateObject";
    return ResultToRetryAction(result);
  }

  // TODO(fes): Check RSA key modulus size, return an error or block input

  if (TPM_ERROR(result = Tspi_Data_Bind(
      enc_handle,
      key_handle,
      plaintext.size(),
      const_cast<BYTE*>(plaintext.data())))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Data_Bind";
    return ResultToRetryAction(result);
  }

  SecureBlob enc_data_blob;
  if (!GetDataAttribute(tpm_context_.value(),
                        enc_handle,
                        TSS_TSPATTRIB_ENCDATA_BLOB,
                        TSS_TSPATTRIB_ENCDATABLOB_BLOB,
                        &enc_data_blob)) {
    LOG(ERROR) << __func__ << ": Failed to read encrypted blob.";
    return kTpmRetryFailNoRetry;
  }
  if (!CryptoLib::ObscureRSAMessage(enc_data_blob, key, ciphertext)) {
    LOG(ERROR) << "Error obscuring message.";
    return kTpmRetryFailNoRetry;
  }
  return kTpmRetryNone;
}

Tpm::TpmRetryAction TpmImpl::DecryptBlob(TpmKeyHandle key_handle,
                                         const SecureBlob& ciphertext,
                                         const SecureBlob& key,
                                         SecureBlob* plaintext) {
  TSS_RESULT result = TSS_SUCCESS;
  SecureBlob local_data;
  if (!CryptoLib::UnobscureRSAMessage(ciphertext, key, &local_data)) {
    LOG(ERROR) << "Error unobscureing message.";
    return kTpmRetryFailNoRetry;
  }

  TSS_FLAG init_flags = TSS_ENCDATA_SEAL;
  ScopedTssKey enc_handle(tpm_context_.value());
  if (TPM_ERROR(result = Tspi_Context_CreateObject(tpm_context_.value(),
                                                   TSS_OBJECT_TYPE_ENCDATA,
                                                   init_flags,
                                                   enc_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Context_CreateObject";
    return ResultToRetryAction(result);
  }

  if (TPM_ERROR(result = Tspi_SetAttribData(enc_handle,
                                            TSS_TSPATTRIB_ENCDATA_BLOB,
                                            TSS_TSPATTRIB_ENCDATABLOB_BLOB,
                                            local_data.size(),
                                            local_data.data()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_SetAttribData";
    return ResultToRetryAction(result);
  }

  ScopedTssMemory dec_data(tpm_context_.value());
  UINT32 dec_data_length = 0;
  if (TPM_ERROR(result = Tspi_Data_Unbind(enc_handle, key_handle,
                                          &dec_data_length, dec_data.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Data_Unbind";
    return ResultToRetryAction(result);
  }

  plaintext->resize(dec_data_length);
  memcpy(plaintext->data(), dec_data.value(), dec_data_length);
  brillo::SecureMemset(dec_data.value(), 0, dec_data_length);

  return kTpmRetryNone;
}

bool TpmImpl::GetPublicKeyBlob(TSS_HCONTEXT context_handle,
                               TSS_HKEY key_handle,
                               SecureBlob* data_out,
                               TSS_RESULT* result) const {
  *result = TSS_SUCCESS;
  ScopedTssMemory blob(context_handle);
  UINT32 blob_size;
  if (TPM_ERROR(*result = Tspi_Key_GetPubKey(key_handle, &blob_size,
                                             blob.ptr()))) {
    TPM_LOG(ERROR, *result) << "Error calling Tspi_Key_GetPubKey";
    return false;
  }

  SecureBlob local_data(blob_size);
  memcpy(local_data.data(), blob.value(), blob_size);
  brillo::SecureMemset(blob.value(), 0, blob_size);
  data_out->swap(local_data);
  return true;
}

bool TpmImpl::LoadSrk(TSS_HCONTEXT context_handle, TSS_HKEY* srk_handle,
                      TSS_RESULT* result) const {
  *result = TSS_SUCCESS;

  // Load the Storage Root Key
  TSS_UUID SRK_UUID = TSS_UUID_SRK;
  ScopedTssKey local_srk_handle(context_handle);
  if (TPM_ERROR(*result = Tspi_Context_LoadKeyByUUID(context_handle,
                                                     TSS_PS_TYPE_SYSTEM,
                                                     SRK_UUID,
                                                     local_srk_handle.ptr()))) {
    return false;
  }

  // Check if the SRK wants a password
  UINT32 srk_authusage;
  if (TPM_ERROR(*result = Tspi_GetAttribUint32(local_srk_handle,
                                               TSS_TSPATTRIB_KEY_INFO,
                                               TSS_TSPATTRIB_KEYINFO_AUTHUSAGE,
                                               &srk_authusage))) {
    return false;
  }

  // Give it the password if needed
  if (srk_authusage) {
    TSS_HPOLICY srk_usage_policy;
    if (TPM_ERROR(*result = Tspi_GetPolicyObject(local_srk_handle,
                                                 TSS_POLICY_USAGE,
                                                 &srk_usage_policy))) {
      return false;
    }

    *result = Tspi_Policy_SetSecret(srk_usage_policy, TSS_SECRET_MODE_PLAIN,
                                    srk_auth_.size(),
                                    const_cast<BYTE*>(srk_auth_.data()));
    if (TPM_ERROR(*result)) {
      return false;
    }
  }

  *srk_handle = local_srk_handle.release();
  return true;
}

bool TpmImpl::CreateEndorsementKey() {
  TSS_RESULT result;
  TSS_HTPM tpm_handle;
  if (!GetTpm(tpm_context_.value(), &tpm_handle)) {
    return false;
  }

  ScopedTssKey local_key_handle(tpm_context_.value());
  TSS_FLAG init_flags = TSS_KEY_TYPE_LEGACY | TSS_KEY_SIZE_2048;
  if (TPM_ERROR(result = Tspi_Context_CreateObject(tpm_context_.value(),
                                                   TSS_OBJECT_TYPE_RSAKEY,
                                                   init_flags,
                                                   local_key_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Context_CreateObject";
    return false;
  }

  if (TPM_ERROR(result = Tspi_TPM_CreateEndorsementKey(tpm_handle,
                                                       local_key_handle,
                                                       NULL))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_TPM_CreateEndorsementKey";
    return false;
  }

  return true;
}

bool TpmImpl::IsEndorsementKeyAvailable() {
  TSS_RESULT result;
  TSS_HTPM tpm_handle;
  if (!GetTpm(tpm_context_.value(), &tpm_handle)) {
    return false;
  }

  ScopedTssKey local_key_handle(tpm_context_.value());
  if (TPM_ERROR(result = Tspi_TPM_GetPubEndorsementKey(tpm_handle, false, NULL,
                                                    local_key_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_TPM_GetPubEndorsementKey";
    return false;
  }

  return true;
}

bool TpmImpl::TakeOwnership(int max_timeout_tries,
                            const SecureBlob& owner_password) {
  TSS_RESULT result;
  TSS_HTPM tpm_handle;
  if (!GetTpmWithAuth(tpm_context_.value(), owner_password, &tpm_handle)) {
    return false;
  }

  ScopedTssKey srk_handle(tpm_context_.value());
  TSS_FLAG init_flags = TSS_KEY_TSP_SRK | TSS_KEY_AUTHORIZATION;
  if (TPM_ERROR(result = Tspi_Context_CreateObject(tpm_context_.value(),
                                          TSS_OBJECT_TYPE_RSAKEY,
                                          init_flags, srk_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Context_CreateObject";
    return false;
  }

  TSS_HPOLICY srk_usage_policy;
  if (TPM_ERROR(result = Tspi_GetPolicyObject(srk_handle, TSS_POLICY_USAGE,
                                              &srk_usage_policy))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_GetPolicyObject";
    return false;
  }

  if (TPM_ERROR(result = Tspi_Policy_SetSecret(srk_usage_policy,
      TSS_SECRET_MODE_PLAIN,
      strlen(kWellKnownSrkTmp),
      const_cast<BYTE *>(reinterpret_cast<const BYTE *>(kWellKnownSrkTmp))))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Policy_SetSecret";
    return false;
  }

  int retry_count = 0;
  do {
    result = Tspi_TPM_TakeOwnership(tpm_handle, srk_handle, 0);
    retry_count++;
  } while (((result == TDDL_E_TIMEOUT) ||
            (result == (TSS_LAYER_TDDL | TDDL_E_TIMEOUT)) ||
            (result == (TSS_LAYER_TDDL | TDDL_E_IOERROR))) &&
           (retry_count < max_timeout_tries));

  if (result) {
    TPM_LOG(ERROR, result)
        << "Error calling Tspi_TPM_TakeOwnership, attempts: " << retry_count;
    return false;
  }

  return true;
}

bool TpmImpl::InitializeSrk(const SecureBlob& owner_password) {
  if (!ZeroSrkPassword(tpm_context_.value(), owner_password)) {
    LOG(ERROR) << "Error Zero-ing SRK password.";
    return false;
  }
  if (!UnrestrictSrk(tpm_context_.value(), owner_password)) {
    LOG(ERROR) << "Error unrestricting SRK.";
    return false;
  }
  return true;
}

bool TpmImpl::ZeroSrkPassword(TSS_HCONTEXT context_handle,
                              const SecureBlob& owner_password) {
  TSS_RESULT result;
  TSS_HTPM tpm_handle;
  if (!GetTpmWithAuth(context_handle, owner_password, &tpm_handle)) {
    return false;
  }

  ScopedTssKey srk_handle(context_handle);
  TSS_UUID SRK_UUID = TSS_UUID_SRK;
  if (TPM_ERROR(result = Tspi_Context_LoadKeyByUUID(context_handle,
                                                    TSS_PS_TYPE_SYSTEM,
                                                    SRK_UUID,
                                                    srk_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Context_LoadKeyByUUID";
    return false;
  }

  ScopedTssPolicy policy_handle(context_handle);
  if (TPM_ERROR(result = Tspi_Context_CreateObject(context_handle,
                                                   TSS_OBJECT_TYPE_POLICY,
                                                   TSS_POLICY_USAGE,
                                                   policy_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Context_CreateObject";
    return false;
  }
  BYTE new_password[0];
  if (TPM_ERROR(result = Tspi_Policy_SetSecret(policy_handle,
                                               TSS_SECRET_MODE_PLAIN,
                                               0,
                                               new_password))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Policy_SetSecret";
    return false;
  }

  if (TPM_ERROR(result = Tspi_ChangeAuth(srk_handle,
                                         tpm_handle,
                                         policy_handle))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_ChangeAuth";
    return false;
  }

  return true;
}

bool TpmImpl::UnrestrictSrk(TSS_HCONTEXT context_handle,
                            const SecureBlob& owner_password) {
  TSS_RESULT result;
  TSS_HTPM tpm_handle;
  if (!GetTpmWithAuth(context_handle, owner_password, &tpm_handle)) {
    return false;
  }

  TSS_BOOL current_status = false;

  if (TPM_ERROR(result = Tspi_TPM_GetStatus(tpm_handle,
                                            TSS_TPMSTATUS_DISABLEPUBSRKREAD,
                                            &current_status))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_TPM_GetStatus";
    return false;
  }

  // If it is currently owner auth (true), set it to SRK auth
  if (current_status) {
    if (TPM_ERROR(result = Tspi_TPM_SetStatus(tpm_handle,
                                              TSS_TPMSTATUS_DISABLEPUBSRKREAD,
                                              false))) {
      TPM_LOG(ERROR, result) << "Error calling Tspi_TPM_SetStatus";
      return false;
    }
  }

  return true;
}

bool TpmImpl::ChangeOwnerPassword(const SecureBlob& previous_owner_password,
                                  const SecureBlob& owner_password) {
  TSS_RESULT result;
  TSS_HTPM tpm_handle;
  if (!GetTpmWithAuth(tpm_context_.value(),
                      previous_owner_password,
                      &tpm_handle)) {
    return false;
  }

  ScopedTssPolicy policy_handle(tpm_context_.value());
  if (TPM_ERROR(result = Tspi_Context_CreateObject(tpm_context_.value(),
                                                   TSS_OBJECT_TYPE_POLICY,
                                                   TSS_POLICY_USAGE,
                                                   policy_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Context_CreateObject";
    return false;
  }

  if (TPM_ERROR(result = Tspi_Policy_SetSecret(policy_handle,
      TSS_SECRET_MODE_PLAIN,
      owner_password.size(),
      const_cast<BYTE *>(owner_password.data())))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Policy_SetSecret";
    return false;
  }

  if (TPM_ERROR(result = Tspi_ChangeAuth(tpm_handle, 0, policy_handle))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_ChangeAuth";
    return false;
  }

  return true;
}

bool TpmImpl::GetTpm(TSS_HCONTEXT context_handle, TSS_HTPM* tpm_handle) {
  TSS_RESULT result;
  TSS_HTPM local_tpm_handle;
  if (TPM_ERROR(result = Tspi_Context_GetTpmObject(context_handle,
                                                   &local_tpm_handle))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Context_GetTpmObject";
    return false;
  }
  *tpm_handle = local_tpm_handle;
  return true;
}

bool TpmImpl::GetTpmWithAuth(TSS_HCONTEXT context_handle,
                             const SecureBlob& owner_password,
                             TSS_HTPM* tpm_handle) {
  TSS_RESULT result;
  TSS_HTPM local_tpm_handle;
  if (!GetTpm(context_handle, &local_tpm_handle)) {
    return false;
  }

  TSS_HPOLICY tpm_usage_policy;
  if (TPM_ERROR(result = Tspi_GetPolicyObject(local_tpm_handle,
                                              TSS_POLICY_USAGE,
                                              &tpm_usage_policy))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_GetPolicyObject";
    return false;
  }

  if (TPM_ERROR(result = Tspi_Policy_SetSecret(
      tpm_usage_policy,
      TSS_SECRET_MODE_PLAIN,
      owner_password.size(),
      const_cast<BYTE*>(owner_password.data())))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Policy_SetSecret";
    return false;
  }

  *tpm_handle = local_tpm_handle;
  return true;
}

bool TpmImpl::GetTpmWithDelegation(TSS_HCONTEXT context_handle,
                                   const SecureBlob& delegate_blob,
                                   const SecureBlob& delegate_secret,
                                   TSS_HTPM* tpm_handle) {
  TSS_HTPM local_tpm_handle;
  if (!GetTpm(context_handle, &local_tpm_handle)) {
    return false;
  }

  TSS_RESULT result;
  TSS_HPOLICY tpm_usage_policy;
  if (TPM_ERROR(result = Tspi_GetPolicyObject(local_tpm_handle,
                                              TSS_POLICY_USAGE,
                                              &tpm_usage_policy))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_GetPolicyObject";
    return false;
  }

  BYTE* secret_buffer = const_cast<BYTE*>(delegate_secret.data());
  if (TPM_ERROR(result = Tspi_Policy_SetSecret(tpm_usage_policy,
                                               TSS_SECRET_MODE_PLAIN,
                                               delegate_secret.size(),
                                               secret_buffer))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Policy_SetSecret";
    return false;
  }

  if (TPM_ERROR(result = Tspi_SetAttribData(
      tpm_usage_policy,
      TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
      TSS_TSPATTRIB_POLDEL_OWNERBLOB,
      delegate_blob.size(),
      const_cast<BYTE*>(delegate_blob.data())))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_SetAttribData";
    return false;
  }

  *tpm_handle = local_tpm_handle;
  return true;
}

bool TpmImpl::TestTpmAuth(const brillo::SecureBlob& owner_password) {
  TSS_HTPM tpm_handle;
  if (!GetTpmWithAuth(tpm_context_.value(), owner_password, &tpm_handle)) {
    LOG(ERROR) << "Error getting Tpm with supplied owner password.";
    return false;
  }

  // Call Tspi_TPM_GetStatus to test the authentication
  TSS_RESULT result;
  TSS_BOOL current_status = false;
  if (TPM_ERROR(result = Tspi_TPM_GetStatus(tpm_handle,
                                            TSS_TPMSTATUS_DISABLED,
                                            &current_status))) {
    return false;
  }
  return true;
}

bool TpmImpl::GetOwnerPassword(brillo::Blob* owner_password) {
  bool result = false;
  if (password_sync_lock_.Try()) {
    if (owner_password_.size() != 0) {
      owner_password->assign(owner_password_.begin(), owner_password_.end());
      result = true;
    }
    password_sync_lock_.Release();
  }
  return result;
}

bool TpmImpl::GetRandomData(size_t length, brillo::Blob* data) {
  ScopedTssContext context_handle;
  if ((*(context_handle.ptr()) = ConnectContext()) == 0) {
    LOG(ERROR) << "Could not open the TPM";
    return false;
  }

  TSS_HTPM tpm_handle;
  if (!GetTpm(context_handle, &tpm_handle)) {
    LOG(ERROR) << "Could not get a handle to the TPM.";
    return false;
  }

  TSS_RESULT result;
  SecureBlob random(length);
  ScopedTssMemory tpm_data(context_handle);
  result = Tspi_TPM_GetRandom(tpm_handle, random.size(), tpm_data.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not get random data from the TPM";
    return false;
  }
  memcpy(random.data(), tpm_data.value(), random.size());
  brillo::SecureMemset(tpm_data.value(), 0, random.size());
  data->swap(random);
  return true;
}

bool TpmImpl::DestroyNvram(uint32_t index) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsOwner(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "Could not open the TPM";
    return false;
  }

  if (!IsNvramDefinedForContext(context_handle, tpm_handle, index)) {
    LOG(INFO) << "NVRAM index is already undefined.";
    return true;
  }

  // Create an NVRAM store object handle.
  TSS_RESULT result;
  ScopedTssNvStore nv_handle(context_handle);
  result = Tspi_Context_CreateObject(context_handle,
                                     TSS_OBJECT_TYPE_NV,
                                     0,
                                     nv_handle.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not acquire an NVRAM object handle";
    return false;
  }

  result = Tspi_SetAttribUint32(nv_handle, TSS_TSPATTRIB_NV_INDEX,
                                0, index);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not set index on NVRAM object: " << index;
    return false;
  }

  if ((result = Tspi_NV_ReleaseSpace(nv_handle))) {
    TPM_LOG(ERROR, result) << "Could not release NVRAM space: " << index;
    return false;
  }

  return true;
}

// TODO(wad) Make this a wrapper around a generic DefineNvram().
bool TpmImpl::DefineLockOnceNvram(uint32_t index, size_t length) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsOwner(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "DefineLockOnceNvram failed to acquire authorization.";
    return false;
  }
  // Create a PCR object handle.
  TSS_RESULT result;
  ScopedTssPcrs pcrs_handle(context_handle);
  if (TPM_ERROR(result = Tspi_Context_CreateObject(context_handle,
                                                   TSS_OBJECT_TYPE_PCRS,
                                                   TSS_PCRS_STRUCT_INFO_SHORT,
                                                   pcrs_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Could not acquire PCR object handle";
    return false;
  }
  // Read PCR0.
  UINT32 pcr_len;
  ScopedTssMemory pcr_value(context_handle);
  if (TPM_ERROR(result = Tspi_TPM_PcrRead(tpm_handle, kTpmBootPCR,
                                          &pcr_len, pcr_value.ptr()))) {
    TPM_LOG(ERROR, result) << "Could not read PCR0 value";
    return false;
  }
  // Include PCR0 value in PcrComposite.
  if (TPM_ERROR(result = Tspi_PcrComposite_SetPcrValue(pcrs_handle,
                                                       kTpmBootPCR,
                                                       pcr_len,
                                                       pcr_value.value()))) {
    TPM_LOG(ERROR, result) << "Could not set value for PCR0 in PCR handle";
    return false;
  }
  // Set locality.
  if (TPM_ERROR(result = Tspi_PcrComposite_SetPcrLocality(pcrs_handle,
                                                          kTpmPCRLocality))) {
    TPM_LOG(ERROR, result) << "Could not set locality for PCR0 in PCR handle";
    return false;
  }

  // Create an NVRAM store object handle.
  ScopedTssNvStore nv_handle(context_handle);
  result = Tspi_Context_CreateObject(context_handle,
                                     TSS_OBJECT_TYPE_NV,
                                     0,
                                     nv_handle.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not acquire an NVRAM object handle";
    return false;
  }

  result = Tspi_SetAttribUint32(nv_handle, TSS_TSPATTRIB_NV_INDEX,
                                0, index);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not set index on NVRAM object: " << index;
    return false;
  }

  result = Tspi_SetAttribUint32(nv_handle, TSS_TSPATTRIB_NV_DATASIZE,
                                0, length);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not set size on NVRAM object: " << length;
    return false;
  }

  // Do not restrict to owner auth for write - just ensure it is lockable.
  result = Tspi_SetAttribUint32(nv_handle, TSS_TSPATTRIB_NV_PERMISSIONS,
                                0, TPM_NV_PER_WRITEDEFINE);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not set PER_WRITEDEFINE on NVRAM object";
    return false;
  }

  if (TPM_ERROR(result = Tspi_NV_DefineSpace(nv_handle,
                                             pcrs_handle, pcrs_handle))) {
    TPM_LOG(ERROR, result) << "Could not define NVRAM space: " << index;
    return false;
  }

  return true;
}

bool TpmImpl::IsNvramDefined(uint32_t index) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "Could not connect to the TPM";
    return false;
  }
  return IsNvramDefinedForContext(context_handle, tpm_handle, index);
}

bool TpmImpl::IsNvramDefinedForContext(TSS_HCONTEXT context_handle,
                                       TSS_HTPM tpm_handle,
                                       uint32_t index) {
  TSS_RESULT result;
  UINT32 nv_list_data_length = 0;
  ScopedTssMemory nv_list_data(context_handle);
  if (TPM_ERROR(result = Tspi_TPM_GetCapability(tpm_handle,
                                                TSS_TPMCAP_NV_LIST,
                                                0,
                                                NULL,
                                                &nv_list_data_length,
                                                nv_list_data.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_TPM_GetCapability";
    return false;
  }

  // Walk the list and check if the index exists.
  UINT32* nv_list = reinterpret_cast<UINT32*>(nv_list_data.value());
  UINT32 nv_list_length = nv_list_data_length / sizeof(UINT32);
  index = htonl(index);  // TPM data is network byte order.
  for (UINT32 i = 0; i < nv_list_length; ++i) {
    // TODO(wad) add a NvramList method.
    if (index == nv_list[i])
      return true;
  }
  return false;
}

unsigned int TpmImpl::GetNvramSize(uint32_t index) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "Could not connect to the TPM";
    return 0;
  }

  return GetNvramSizeForContext(context_handle, tpm_handle, index);
}

unsigned int TpmImpl::GetNvramSizeForContext(TSS_HCONTEXT context_handle,
                                             TSS_HTPM tpm_handle,
                                             uint32_t index) {
  unsigned int count = 0;
  TSS_RESULT result;

  UINT32 nv_index_data_length = 0;
  ScopedTssMemory nv_index_data(context_handle);
  if (TPM_ERROR(result = Tspi_TPM_GetCapability(tpm_handle,
                                                TSS_TPMCAP_NV_INDEX,
                                                sizeof(index),
                                                reinterpret_cast<BYTE*>(&index),
                                                &nv_index_data_length,
                                                nv_index_data.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_TPM_GetCapability";
    return count;
  }
  if (nv_index_data_length == 0) {
    return count;
  }
  // TPM_NV_DATA_PUBLIC->dataSize is the last element in the struct.
  // Since packing the struct still doesn't eliminate inconsistencies between
  // the API and the hardware, this is the safest way to extract the data.
  uint32_t* nv_data_public = reinterpret_cast<uint32_t*>(
                               nv_index_data.value() + nv_index_data_length -
                               sizeof(UINT32));
  return htonl(*nv_data_public);
}

bool TpmImpl::IsNvramLocked(uint32_t index) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "Could not connect to the TPM";
    return 0;
  }

  return IsNvramLockedForContext(context_handle, tpm_handle, index);
}

bool TpmImpl::IsNvramLockedForContext(TSS_HCONTEXT context_handle,
                                      TSS_HTPM tpm_handle,
                                      uint32_t index) {
  unsigned int count = 0;
  TSS_RESULT result;
  UINT32 nv_index_data_length = 0;
  ScopedTssMemory nv_index_data(context_handle);
  if (TPM_ERROR(result = Tspi_TPM_GetCapability(tpm_handle,
                                                TSS_TPMCAP_NV_INDEX,
                                                sizeof(index),
                                                reinterpret_cast<BYTE*>(&index),
                                                &nv_index_data_length,
                                                nv_index_data.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_TPM_GetCapability";
    return count;
  }
  if (nv_index_data_length < sizeof(UINT32) + sizeof(TPM_BOOL)) {
    return count;
  }
  // TPM_NV_DATA_PUBLIC->bWriteDefine is the second to last element in the
  // struct.  Since packing the struct still doesn't eliminate inconsistencies
  // between the API and the hardware, this is the safest way to extract the
  // data.
  // TODO(wad) share data with GetNvramSize() to avoid extra calls.
  uint32_t* nv_data_public = reinterpret_cast<uint32_t*>(
                               nv_index_data.value() + nv_index_data_length -
                               (sizeof(UINT32) + sizeof(TPM_BOOL)));
  return (*nv_data_public != 0);
}

bool TpmImpl::ReadNvram(uint32_t index, SecureBlob* blob) {
  // TODO(wad) longer term, add support for checking when a space is restricted
  //           and needs an authenticated handle.
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "Could not connect to the TPM";
    return false;
  }
  return ReadNvramForContext(context_handle, tpm_handle, 0, index, blob);
}

bool TpmImpl::ReadNvramForContext(TSS_HCONTEXT context_handle,
                                  TSS_HTPM tpm_handle,
                                  TSS_HPOLICY policy_handle,
                                  uint32_t index,
                                  SecureBlob* blob) {
  if (!IsNvramDefinedForContext(context_handle, tpm_handle, index)) {
    LOG(ERROR) << "Cannot read from non-existent NVRAM space.";
    return false;
  }

  // Create an NVRAM store object handle.
  TSS_RESULT result;
  ScopedTssNvStore nv_handle(context_handle);
  result = Tspi_Context_CreateObject(context_handle,
                                     TSS_OBJECT_TYPE_NV,
                                     0,
                                     nv_handle.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not acquire an NVRAM object handle";
    return false;
  }
  result = Tspi_SetAttribUint32(nv_handle, TSS_TSPATTRIB_NV_INDEX,
                                0, index);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not set index on NVRAM object: " << index;
    return false;
  }

  if (policy_handle) {
    result = Tspi_Policy_AssignToObject(policy_handle, nv_handle);
    if (TPM_ERROR(result)) {
      TPM_LOG(ERROR, result) << "Could not set NVRAM object policy.";
      return false;
    }
  }

  UINT32 size = GetNvramSizeForContext(context_handle, tpm_handle, index);
  if (size == 0) {
    LOG(ERROR) << "NvramSize is too small.";
    // TODO(wad) get attrs so we can explore more
    return false;
  }
  blob->resize(size);

  // Read from NVRAM in conservatively small chunks. This is a limitation of the
  // TPM that is left for the application layer to deal with. The maximum size
  // that is supported here can vary between vendors / models, so we'll be
  // conservative. FWIW, the Infineon chips seem to handle up to 1024.
  const UINT32 kMaxDataSize = 128;
  UINT32 offset = 0;
  while (offset < size) {
    UINT32 chunk_size = size - offset;
    if (chunk_size > kMaxDataSize)
      chunk_size = kMaxDataSize;
    ScopedTssMemory space_data(context_handle);
    if ((result = Tspi_NV_ReadValue(nv_handle, offset, &chunk_size,
                                    space_data.ptr()))) {
      TPM_LOG(ERROR, result) << "Could not read from NVRAM space: " << index;
      return false;
    }
    if (!space_data.value()) {
      LOG(ERROR) << "No data read from NVRAM space: " << index;
      return false;
    }
    CHECK(offset + chunk_size <= blob->size());
    unsigned char* buffer = blob->data() + offset;
    memcpy(buffer, space_data.value(), chunk_size);
    offset += chunk_size;
  }
  return true;
}

bool TpmImpl::WriteNvram(uint32_t index, const SecureBlob& blob) {
  // TODO(wad) longer term, add support for checking when a space is restricted
  //           and needs an authenticated handle.
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "Could not connect to the TPM";
    return 0;
  }

  // Create an NVRAM store object handle.
  TSS_RESULT result;
  ScopedTssNvStore nv_handle(context_handle);
  result = Tspi_Context_CreateObject(context_handle,
                                     TSS_OBJECT_TYPE_NV,
                                     0,
                                     nv_handle.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not acquire an NVRAM object handle";
    return false;
  }

  result = Tspi_SetAttribUint32(nv_handle, TSS_TSPATTRIB_NV_INDEX,
                                0, index);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not set index on NVRAM object: " << index;
    return false;
  }

  scoped_ptr<BYTE[]> nv_data(new BYTE[blob.size()]);
  memcpy(nv_data.get(), blob.data(), blob.size());
  result = Tspi_NV_WriteValue(nv_handle, 0, blob.size(), nv_data.get());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not write to NVRAM space: " << index;
    return false;
  }

  return true;
}

bool TpmImpl::PerformEnabledOwnedCheck(bool* enabled, bool* owned) {
  *enabled = false;
  *owned = false;

  trousers::ScopedTssContext context(ConnectContext());
  if (!context) {
    return false;
  }

  TSS_HCONTEXT context_handle = context.context();
  TSS_RESULT result;
  TSS_HTPM tpm_handle;

  if (TPM_ERROR(result = Tspi_Context_GetTpmObject(context_handle,
                                                   &tpm_handle))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Context_GetTpmObject";
    return false;
  }

  UINT32 sub_cap = TSS_TPMCAP_PROP_OWNER;
  UINT32 cap_length = 0;
  trousers::ScopedTssMemory cap(context_handle);
  if (TPM_ERROR(result = Tspi_TPM_GetCapability(tpm_handle, TSS_TPMCAP_PROPERTY,
                                       sizeof(sub_cap),
                                       reinterpret_cast<BYTE*>(&sub_cap),
                                       &cap_length, cap.ptr())) == 0) {
    if (cap_length >= (sizeof(TSS_BOOL))) {
      *enabled = true;
      *owned = ((*(reinterpret_cast<TSS_BOOL*>(cap.value()))) != 0);
    }
  } else if (ERROR_CODE(result) == TPM_E_DISABLED) {
    *enabled = false;
  }

  return true;
}

bool TpmImpl::GetEndorsementPublicKey(SecureBlob* ek_public_key) {
  // Connect to the TPM as the owner if owned, user otherwise.
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (is_owned_) {
    if (!ConnectContextAsOwner(context_handle.ptr(), &tpm_handle)) {
      LOG(ERROR) << "GetEndorsementPublicKey: Could not connect to the TPM.";
      return false;
    }
  } else {
    if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
      LOG(ERROR) << "GetEndorsementPublicKey: Could not connect to the TPM.";
      return false;
    }
  }
  // Get a handle to the EK public key.
  ScopedTssKey ek_public_key_object(context_handle);
  TSS_RESULT result = Tspi_TPM_GetPubEndorsementKey(tpm_handle, is_owned_, NULL,
                                                    ek_public_key_object.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "GetEndorsementPublicKey: Failed to get key.";
    return false;
  }
  // Get the public key in TPM_PUBKEY form.
  SecureBlob ek_public_key_blob;
  if (!GetDataAttribute(context_handle,
                        ek_public_key_object,
                        TSS_TSPATTRIB_KEY_BLOB,
                        TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY,
                        &ek_public_key_blob)) {
    LOG(ERROR) << __func__ << ": Failed to read public key.";
    return false;
  }
  // Get the public key in DER encoded form.
  if (!ConvertPublicKeyToDER(ek_public_key_blob, ek_public_key)) {
    return false;
  }
  return true;
}

bool TpmImpl::GetEndorsementCredential(SecureBlob* credential) {
  // Connect to the TPM as the owner.
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsOwner(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "GetEndorsementCredential: Could not connect to the TPM.";
    return false;
  }

  // Use the owner secret to authorize reading the blob.
  ScopedTssPolicy policy_handle(context_handle);
  TSS_RESULT result;
  result = Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_POLICY,
                                     TSS_POLICY_USAGE, policy_handle.ptr());
  if (TPM_ERROR(result)) {
    LOG(ERROR) << "GetEndorsementCredential: Could not create policy.";
    return false;
  }
  result = Tspi_Policy_SetSecret(policy_handle, TSS_SECRET_MODE_PLAIN,
                                 owner_password_.size(),
                                 static_cast<BYTE*>(owner_password_.data()));
  if (TPM_ERROR(result)) {
    LOG(ERROR) << "GetEndorsementCredential: Could not set owner secret.";
    return false;
  }

  // Read the EK cert from NVRAM.
  SecureBlob nvram_value;
  if (!ReadNvramForContext(context_handle, tpm_handle, policy_handle,
                           TSS_NV_DEFINED | TPM_NV_INDEX_EKCert,
                           &nvram_value)) {
    LOG(ERROR) << "GetEndorsementCredential: Failed to read NVRAM.";
    return false;
  }

  // Sanity check the contents of the data and extract the X.509 certificate.
  // We are expecting data in the form of a TCG_PCCLIENT_STORED_CERT with an
  // embedded TCG_FULL_CERT. Details can be found in the TCG PC Specific
  // Implementation Specification v1.21 section 7.4.
  const uint8_t kStoredCertHeader[] = {0x10, 0x01, 0x00};
  const uint8_t kFullCertHeader[] = {0x10, 0x02};
  const size_t kTotalHeaderBytes = 7;
  const size_t kStoredCertHeaderOffset = 0;
  const size_t kFullCertLengthOffset = 3;
  const size_t kFullCertHeaderOffset = 5;
  if (nvram_value.size() < kTotalHeaderBytes) {
    LOG(ERROR) << "Malformed EK certificate: Bad header.";
    return false;
  }
  if (memcmp(kStoredCertHeader,
             &nvram_value[kStoredCertHeaderOffset],
             arraysize(kStoredCertHeader)) != 0) {
    LOG(ERROR) << "Malformed EK certificate: Bad PCCLIENT_STORED_CERT.";
    return false;
  }
  if (memcmp(kFullCertHeader,
             &nvram_value[kFullCertHeaderOffset],
             arraysize(kFullCertHeader)) != 0) {
    LOG(ERROR) << "Malformed EK certificate: Bad PCCLIENT_FULL_CERT.";
    return false;
  }
  // The size value is represented by two bytes in network order.
  size_t full_cert_size = (nvram_value[kFullCertLengthOffset] << 8) |
                          nvram_value[kFullCertLengthOffset + 1];
  if (full_cert_size + kFullCertHeaderOffset > nvram_value.size()) {
    LOG(ERROR) << "Malformed EK certificate: Bad size.";
    return false;
  }
  // The X.509 certificate follows the header bytes.
  size_t full_cert_end = kTotalHeaderBytes + full_cert_size -
                         arraysize(kFullCertHeader);
  credential->assign(nvram_value.begin() + kTotalHeaderBytes,
                     nvram_value.begin() + full_cert_end);
  return true;
}

bool TpmImpl::DecryptIdentityRequest(RSA* pca_key,
                                     const SecureBlob& request,
                                     SecureBlob* identity_binding,
                                     SecureBlob* endorsement_credential,
                                     SecureBlob* platform_credential,
                                     SecureBlob* conformance_credential) {
  // Parse the serialized TPM_IDENTITY_REQ structure.
  UINT64 offset = 0;
  BYTE* buffer = const_cast<BYTE*>(request.data());
  TPM_IDENTITY_REQ request_parsed;
  TSS_RESULT result = Trspi_UnloadBlob_IDENTITY_REQ(&offset, buffer,
                                                    &request_parsed);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Failed to parse identity request.";
    return false;
  }
  ScopedByteArray scoped_asym_blob(request_parsed.asymBlob);
  ScopedByteArray scoped_sym_blob(request_parsed.symBlob);

  // Decrypt the symmetric key.
  unsigned char key_buffer[kDefaultTpmRsaKeyBits / 8];
  int key_length = RSA_private_decrypt(request_parsed.asymSize,
                                       request_parsed.asymBlob,
                                       key_buffer, pca_key, RSA_PKCS1_PADDING);
  if (key_length == -1) {
    LOG(ERROR) << "Failed to decrypt identity request key.";
    return false;
  }
  TPM_SYMMETRIC_KEY symmetric_key;
  offset = 0;
  result = Trspi_UnloadBlob_SYMMETRIC_KEY(&offset, key_buffer, &symmetric_key);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Failed to parse symmetric key.";
    return false;
  }
  ScopedByteArray scoped_sym_key(symmetric_key.data);

  // Decrypt the request with the symmetric key.
  SecureBlob proof_serial;
  proof_serial.resize(request_parsed.symSize);
  UINT32 proof_serial_length = proof_serial.size();
  result = Trspi_SymDecrypt(symmetric_key.algId, TPM_ES_SYM_CBC_PKCS5PAD,
                            symmetric_key.data, NULL,
                            request_parsed.symBlob, request_parsed.symSize,
                            proof_serial.data(), &proof_serial_length);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Failed to decrypt identity request.";
    return false;
  }

  // Parse the serialized TPM_IDENTITY_PROOF structure.
  TPM_IDENTITY_PROOF proof;
  offset = 0;
  result = Trspi_UnloadBlob_IDENTITY_PROOF(&offset, proof_serial.data(),
                                           &proof);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Failed to parse identity proof.";
    return false;
  }
  ScopedByteArray scoped_label(proof.labelArea);
  ScopedByteArray scoped_binding(proof.identityBinding);
  ScopedByteArray scoped_endorsement(proof.endorsementCredential);
  ScopedByteArray scoped_platform(proof.platformCredential);
  ScopedByteArray scoped_conformance(proof.conformanceCredential);
  ScopedByteArray scoped_key(proof.identityKey.pubKey.key);
  ScopedByteArray scoped_parms(proof.identityKey.algorithmParms.parms);

  identity_binding->assign(&proof.identityBinding[0],
                           &proof.identityBinding[proof.identityBindingSize]);
  brillo::SecureMemset(proof.identityBinding, 0, proof.identityBindingSize);
  endorsement_credential->assign(
      &proof.endorsementCredential[0],
      &proof.endorsementCredential[proof.endorsementSize]);
  brillo::SecureMemset(proof.endorsementCredential, 0, proof.endorsementSize);
  platform_credential->assign(&proof.platformCredential[0],
                              &proof.platformCredential[proof.platformSize]);
  brillo::SecureMemset(proof.platformCredential, 0, proof.platformSize);
  conformance_credential->assign(
      &proof.conformanceCredential[0],
      &proof.conformanceCredential[proof.conformanceSize]);
  brillo::SecureMemset(proof.conformanceCredential, 0, proof.conformanceSize);
  return true;
}

bool TpmImpl::ConvertPublicKeyToDER(const SecureBlob& public_key,
                                    SecureBlob* public_key_der) {
  // Parse the serialized TPM_PUBKEY.
  UINT64 offset = 0;
  BYTE* buffer = const_cast<BYTE*>(public_key.data());
  TPM_PUBKEY parsed;
  TSS_RESULT result = Trspi_UnloadBlob_PUBKEY(&offset, buffer, &parsed);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Failed to parse TPM_PUBKEY.";
    return false;
  }
  ScopedByteArray scoped_key(parsed.pubKey.key);
  ScopedByteArray scoped_parms(parsed.algorithmParms.parms);
  TPM_RSA_KEY_PARMS* parms =
      reinterpret_cast<TPM_RSA_KEY_PARMS*>(parsed.algorithmParms.parms);
  crypto::ScopedRSA rsa(RSA_new());
  CHECK(rsa.get());
  // Get the public exponent.
  if (parms->exponentSize == 0) {
    rsa.get()->e = BN_new();
    CHECK(rsa.get()->e);
    BN_set_word(rsa.get()->e, kWellKnownExponent);
  } else {
    rsa.get()->e = BN_bin2bn(parms->exponent, parms->exponentSize, NULL);
    CHECK(rsa.get()->e);
  }
  // Get the modulus.
  rsa.get()->n = BN_bin2bn(parsed.pubKey.key, parsed.pubKey.keyLength, NULL);
  CHECK(rsa.get()->n);

  // DER encode.
  int der_length = i2d_RSAPublicKey(rsa.get(), NULL);
  if (der_length < 0) {
    LOG(ERROR) << "Failed to DER-encode public key.";
    return false;
  }
  public_key_der->resize(der_length);
  unsigned char* der_buffer = public_key_der->data();
  der_length = i2d_RSAPublicKey(rsa.get(), &der_buffer);
  if (der_length < 0) {
    LOG(ERROR) << "Failed to DER-encode public key.";
    return false;
  }
  public_key_der->resize(der_length);
  return true;
}

bool TpmImpl::MakeIdentity(SecureBlob* identity_public_key_der,
                           SecureBlob* identity_public_key,
                           SecureBlob* identity_key_blob,
                           SecureBlob* identity_binding,
                           SecureBlob* identity_label,
                           SecureBlob* pca_public_key,
                           SecureBlob* endorsement_credential,
                           SecureBlob* platform_credential,
                           SecureBlob* conformance_credential) {
  CHECK(identity_public_key_der && identity_public_key && identity_key_blob &&
        identity_binding && identity_label && pca_public_key &&
        endorsement_credential && platform_credential &&
        conformance_credential);
  // Connect to the TPM as the owner.
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsOwner(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "MakeIdentity: Could not connect to the TPM.";
    return false;
  }

  // Load the Storage Root Key.
  TSS_RESULT result;
  ScopedTssKey srk_handle(context_handle);
  if (!LoadSrk(context_handle, srk_handle.ptr(), &result)) {
    TPM_LOG(INFO, result) << "MakeIdentity: Cannot load SRK.";
    return false;
  }

  crypto::ScopedRSA fake_pca_key(RSA_generate_key(kDefaultTpmRsaKeyBits,
                                                  kWellKnownExponent,
                                                  NULL,
                                                  NULL));
  if (!fake_pca_key.get()) {
    LOG(ERROR) << "MakeIdentity: Failed to generate local key pair.";
    return false;
  }
  unsigned char modulus_buffer[kDefaultTpmRsaKeyBits/8];
  BN_bn2bin(fake_pca_key.get()->n, modulus_buffer);

  // Create a TSS object for the fake PCA public key.
  ScopedTssKey pca_public_key_object(context_handle);
  UINT32 pca_key_flags = kDefaultTpmRsaKeyFlag |
                         TSS_KEY_TYPE_LEGACY |
                         TSS_KEY_MIGRATABLE;
  result = Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_RSAKEY,
                                     pca_key_flags,
                                     pca_public_key_object.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "MakeIdentity: Cannot create PCA public key.";
    return false;
  }
  result = Tspi_SetAttribData(pca_public_key_object,
                              TSS_TSPATTRIB_RSAKEY_INFO,
                              TSS_TSPATTRIB_KEYINFO_RSA_MODULUS,
                              arraysize(modulus_buffer), modulus_buffer);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "MakeIdentity: Cannot create PCA public key 2.";
    return false;
  }
  result = Tspi_SetAttribUint32(pca_public_key_object,
                                TSS_TSPATTRIB_KEY_INFO,
                                TSS_TSPATTRIB_KEYINFO_ENCSCHEME,
                                TSS_ES_RSAESPKCSV15);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "MakeIdentity: Cannot create PCA public key 3.";
    return false;
  }

  // Get the fake PCA public key in serialized TPM_PUBKEY form.
  if (!GetDataAttribute(context_handle,
                        pca_public_key_object,
                        TSS_TSPATTRIB_KEY_BLOB,
                        TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY,
                        pca_public_key)) {
    LOG(ERROR) << __func__ << ": Failed to read public key.";
    return false;
  }

  // Construct an arbitrary unicode label.
  const char* label_text = "ChromeOS_AIK_1BJNAMQDR4RH44F4ET2KPAOMJMO043K1";
  BYTE* label_ascii =
      const_cast<BYTE*>(reinterpret_cast<const BYTE*>(label_text));
  unsigned int label_size = strlen(label_text);
  ScopedByteArray label(Trspi_Native_To_UNICODE(label_ascii, &label_size));
  if (!label.get()) {
    LOG(ERROR) << "MakeIdentity: Failed to create AIK label.";
    return false;
  }
  identity_label->assign(&label.get()[0],
                         &label.get()[label_size]);

  // Initialize a key object to hold the new identity key.
  ScopedTssKey identity_key(context_handle);
  UINT32 identity_key_flags = kDefaultTpmRsaKeyFlag |
                              TSS_KEY_TYPE_IDENTITY |
                              TSS_KEY_VOLATILE |
                              TSS_KEY_NOT_MIGRATABLE;
  result = Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_RSAKEY,
                                     identity_key_flags, identity_key.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "MakeIdentity: Failed to create key object.";
    return false;
  }

  // Create the identity and receive the request intended for the PCA.
  UINT32 request_length = 0;
  ScopedTssMemory request(context_handle);
  result = Tspi_TPM_CollateIdentityRequest(tpm_handle, srk_handle,
                                           pca_public_key_object,
                                           label_size, label.get(),
                                           identity_key, TSS_ALG_3DES,
                                           &request_length, request.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "MakeIdentity: Failed to make identity.";
    return false;
  }

  // Decrypt and parse the identity request.
  SecureBlob request_blob(request.value(), request.value() + request_length);
  if (!DecryptIdentityRequest(fake_pca_key.get(), request_blob,
                              identity_binding, endorsement_credential,
                              platform_credential, conformance_credential)) {
    LOG(ERROR) << "MakeIdentity: Failed to decrypt the identity request.";
    return false;
  }
  brillo::SecureMemset(request.value(), 0, request_length);

  // We need the endorsement credential. If CollateIdentityRequest does not
  // provide it, read it manually.
  if (endorsement_credential->size() == 0 &&
      !GetEndorsementCredential(endorsement_credential)) {
    LOG(ERROR) << "MakeIdentity: Failed to get endorsement credential.";
    return false;
  }

  // Get the AIK public key.
  if (!GetDataAttribute(context_handle,
                        identity_key,
                        TSS_TSPATTRIB_KEY_BLOB,
                        TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY,
                        identity_public_key)) {
    LOG(ERROR) << __func__ << ": Failed to read public key.";
    return false;
  }
  if (!ConvertPublicKeyToDER(*identity_public_key, identity_public_key_der)) {
    return false;
  }

  // Get the AIK blob so we can load it later.
  if (!GetDataAttribute(context_handle,
                        identity_key,
                        TSS_TSPATTRIB_KEY_BLOB,
                        TSS_TSPATTRIB_KEYBLOB_BLOB,
                        identity_key_blob)) {
    LOG(ERROR) << __func__ << ": Failed to read key blob.";
    return false;
  }
  return true;
}

bool TpmImpl::QuotePCR(int pcr_index,
                       const SecureBlob& identity_key_blob,
                       const SecureBlob& external_data,
                       SecureBlob* pcr_value,
                       SecureBlob* quoted_data,
                       SecureBlob* quote) {
  CHECK(pcr_value && quoted_data && quote);
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "QuotePCR: Failed to connect to the TPM.";
    return false;
  }
  // Load the Storage Root Key.
  TSS_RESULT result;
  ScopedTssKey srk_handle(context_handle);
  if (!LoadSrk(context_handle, srk_handle.ptr(), &result)) {
    TPM_LOG(INFO, result) << "QuotePCR: Failed to load SRK.";
    return false;
  }
  // Load the AIK (which is wrapped by the SRK).
  ScopedTssKey identity_key(context_handle);
  result = Tspi_Context_LoadKeyByBlob(
      context_handle,
      srk_handle,
      identity_key_blob.size(),
      const_cast<BYTE*>(identity_key_blob.data()),
      identity_key.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "QuotePCR: Failed to load AIK.";
    return false;
  }

  // Create a PCRS object and select the index.
  ScopedTssPcrs pcrs(context_handle);
  result = Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_PCRS,
                                     TSS_PCRS_STRUCT_INFO, pcrs.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "QuotePCR: Failed to create PCRS object.";
    return false;
  }
  result = Tspi_PcrComposite_SelectPcrIndex(pcrs, pcr_index);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "QuotePCR: Failed to select PCR.";
    return false;
  }
  // Generate the quote.
  TSS_VALIDATION validation;
  memset(&validation, 0, sizeof(validation));
  validation.ulExternalDataLength = external_data.size();
  validation.rgbExternalData = const_cast<BYTE*>(external_data.data());
  result = Tspi_TPM_Quote(tpm_handle, identity_key, pcrs, &validation);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "QuotePCR: Failed to generate quote.";
    return false;
  }
  ScopedTssMemory scoped_quoted_data(0, validation.rgbData);
  ScopedTssMemory scoped_quote(0, validation.rgbValidationData);

  // Get the PCR value that was quoted.
  ScopedTssMemory pcr_value_buffer;
  UINT32 pcr_value_length = 0;
  result = Tspi_PcrComposite_GetPcrValue(pcrs, pcr_index, &pcr_value_length,
                                         pcr_value_buffer.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "QuotePCR: Failed to get PCR value.";
    return false;
  }
  pcr_value->assign(&pcr_value_buffer.value()[0],
                    &pcr_value_buffer.value()[pcr_value_length]);
  // Get the data that was quoted.
  quoted_data->assign(&validation.rgbData[0],
                      &validation.rgbData[validation.ulDataLength]);
  // Get the quote.
  quote->assign(
      &validation.rgbValidationData[0],
      &validation.rgbValidationData[validation.ulValidationDataLength]);
  return true;
}

bool TpmImpl::SealToPCR0(const brillo::Blob& value,
                         brillo::Blob* sealed_value) {
  CHECK(sealed_value);
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "SealToPCR0: Failed to connect to the TPM.";
    return false;
  }
  // Load the Storage Root Key.
  TSS_RESULT result;
  ScopedTssKey srk_handle(context_handle);
  if (!LoadSrk(context_handle, srk_handle.ptr(), &result)) {
    TPM_LOG(INFO, result) << "SealToPCR0: Failed to load SRK.";
    return false;
  }

  // Check the SRK public key
  unsigned int size_n = 0;
  ScopedTssMemory public_srk(context_handle);
  if (TPM_ERROR(result = Tspi_Key_GetPubKey(srk_handle, &size_n,
                                            public_srk.ptr()))) {
    TPM_LOG(ERROR, result) << "SealToPCR0: Unable to get the SRK public key";
    return false;
  }

  // Create a PCRS object which holds the value of PCR0.
  ScopedTssPcrs pcrs_handle(context_handle);
  if (TPM_ERROR(result = Tspi_Context_CreateObject(context_handle,
                                                   TSS_OBJECT_TYPE_PCRS,
                                                   TSS_PCRS_STRUCT_INFO,
                                                   pcrs_handle.ptr()))) {
    TPM_LOG(ERROR, result)
        << "SealToPCR0: Error calling Tspi_Context_CreateObject";
    return false;
  }

  // Create a ENCDATA object to receive the sealed data.
  UINT32 pcr_len = 0;
  ScopedTssMemory pcr_value(context_handle);
  Tspi_TPM_PcrRead(tpm_handle, 0, &pcr_len, pcr_value.ptr());
  Tspi_PcrComposite_SetPcrValue(pcrs_handle, 0, pcr_len, pcr_value.value());

  ScopedTssKey enc_handle(context_handle);
  if (TPM_ERROR(result = Tspi_Context_CreateObject(context_handle,
                                                   TSS_OBJECT_TYPE_ENCDATA,
                                                   TSS_ENCDATA_SEAL,
                                                   enc_handle.ptr()))) {
    TPM_LOG(ERROR, result)
        << "SealToPCR0: Error calling Tspi_Context_CreateObject";
    return false;
  }

  // Seal the given value with the SRK.
  if (TPM_ERROR(result = Tspi_Data_Seal(
      enc_handle,
      srk_handle,
      value.size(),
      const_cast<BYTE*>(value.data()),
      pcrs_handle))) {
    TPM_LOG(ERROR, result) << "SealToPCR0: Error calling Tspi_Data_Seal";
    return false;
  }

  // Extract the sealed value.
  ScopedTssMemory enc_data(context_handle);
  UINT32 enc_data_length = 0;
  if (TPM_ERROR(result = Tspi_GetAttribData(enc_handle,
                                            TSS_TSPATTRIB_ENCDATA_BLOB,
                                            TSS_TSPATTRIB_ENCDATABLOB_BLOB,
                                            &enc_data_length,
                                            enc_data.ptr()))) {
    TPM_LOG(ERROR, result) << "SealToPCR0: Error calling Tspi_GetAttribData";
    return false;
  }
  sealed_value->assign(&enc_data.value()[0],
                       &enc_data.value()[enc_data_length]);
  return true;
}

bool TpmImpl::Unseal(const brillo::Blob& sealed_value,
                     brillo::Blob* value) {
  CHECK(value);
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "Unseal: Failed to connect to the TPM.";
    return false;
  }
  // Load the Storage Root Key.
  TSS_RESULT result;
  ScopedTssKey srk_handle(context_handle);
  if (!LoadSrk(context_handle, srk_handle.ptr(), &result)) {
    TPM_LOG(INFO, result) << "Unseal: Failed to load SRK.";
    return false;
  }

  // Create an ENCDATA object with the sealed value.
  ScopedTssKey enc_handle(context_handle);
  if (TPM_ERROR(result = Tspi_Context_CreateObject(context_handle,
                                                   TSS_OBJECT_TYPE_ENCDATA,
                                                   TSS_ENCDATA_SEAL,
                                                   enc_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Unseal: Error calling Tspi_Context_CreateObject";
    return false;
  }

  if (TPM_ERROR(result = Tspi_SetAttribData(enc_handle,
      TSS_TSPATTRIB_ENCDATA_BLOB,
      TSS_TSPATTRIB_ENCDATABLOB_BLOB,
      sealed_value.size(),
      const_cast<BYTE*>(sealed_value.data())))) {
    TPM_LOG(ERROR, result) << "Unseal: Error calling Tspi_SetAttribData";
    return false;
  }

  // Unseal using the SRK.
  ScopedTssMemory dec_data(context_handle);
  UINT32 dec_data_length = 0;
  if (TPM_ERROR(result = Tspi_Data_Unseal(enc_handle,
                                          srk_handle,
                                          &dec_data_length,
                                          dec_data.ptr()))) {
    TPM_LOG(ERROR, result) << "Unseal: Error calling Tspi_Data_Unseal";
    return false;
  }
  value->assign(&dec_data.value()[0], &dec_data.value()[dec_data_length]);
  brillo::SecureMemset(dec_data.value(), 0, dec_data_length);
  return true;
}

bool TpmImpl::CreateCertifiedKey(const SecureBlob& identity_key_blob,
                                 const SecureBlob& external_data,
                                 SecureBlob* certified_public_key,
                                 SecureBlob* certified_public_key_der,
                                 SecureBlob* certified_key_blob,
                                 SecureBlob* certified_key_info,
                                 SecureBlob* certified_key_proof) {
  CHECK(certified_public_key && certified_key_blob && certified_key_info &&
        certified_key_proof);
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "CreateCertifiedKey: Failed to connect to the TPM.";
    return false;
  }

  // Load the Storage Root Key.
  TSS_RESULT result;
  ScopedTssKey srk_handle(context_handle);
  if (!LoadSrk(context_handle, srk_handle.ptr(), &result)) {
    TPM_LOG(INFO, result) << "CreateCertifiedKey: Failed to load SRK.";
    return false;
  }

  // Load the AIK (which is wrapped by the SRK).
  ScopedTssKey identity_key(context_handle);
  result = Tspi_Context_LoadKeyByBlob(
      context_handle,
      srk_handle,
      identity_key_blob.size(),
      const_cast<BYTE*>(identity_key_blob.data()),
      identity_key.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateCertifiedKey: Failed to load AIK.";
    return false;
  }

  // Create a non-migratable signing key.
  ScopedTssKey signing_key(context_handle);
  UINT32 init_flags = TSS_KEY_TYPE_SIGNING |
                      TSS_KEY_NOT_MIGRATABLE |
                      TSS_KEY_VOLATILE |
                      kDefaultTpmRsaKeyFlag;
  result = Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_RSAKEY,
                                     init_flags, signing_key.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateCertifiedKey: Failed to create object.";
    return false;
  }
  result = Tspi_SetAttribUint32(signing_key,
                                TSS_TSPATTRIB_KEY_INFO,
                                TSS_TSPATTRIB_KEYINFO_SIGSCHEME,
                                TSS_SS_RSASSAPKCS1V15_DER);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateCertifiedKey: Failed to set signature "
                           << "scheme.";
    return false;
  }
  result = Tspi_Key_CreateKey(signing_key, srk_handle, 0);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateCertifiedKey: Failed to create key.";
    return false;
  }
  result = Tspi_Key_LoadKey(signing_key, srk_handle);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateCertifiedKey: Failed to load key.";
    return false;
  }

  // Certify the signing key.
  TSS_VALIDATION validation;
  memset(&validation, 0, sizeof(validation));
  validation.ulExternalDataLength = external_data.size();
  validation.rgbExternalData =
      const_cast<BYTE*>(external_data.data());
  result = Tspi_Key_CertifyKey(signing_key, identity_key, &validation);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateCertifiedKey: Failed to certify key.";
    return false;
  }
  ScopedTssMemory scoped_certified_data(0, validation.rgbData);
  ScopedTssMemory scoped_proof(0, validation.rgbValidationData);

  // Get the certified public key.
  if (!GetDataAttribute(context_handle,
                        signing_key,
                        TSS_TSPATTRIB_KEY_BLOB,
                        TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY,
                        certified_public_key)) {
    LOG(ERROR) << "CreateCertifiedKey: Failed to read public key.";
    return false;
  }
  if (!ConvertPublicKeyToDER(*certified_public_key, certified_public_key_der)) {
    return false;
  }

  // Get the certified key blob so we can load it later.
  if (!GetDataAttribute(context_handle,
                        signing_key,
                        TSS_TSPATTRIB_KEY_BLOB,
                        TSS_TSPATTRIB_KEYBLOB_BLOB,
                        certified_key_blob)) {
    LOG(ERROR) << "CreateCertifiedKey: Failed to read key blob.";
    return false;
  }

  // Get the data that was certified.
  certified_key_info->assign(&validation.rgbData[0],
                             &validation.rgbData[validation.ulDataLength]);

  // Get the certification proof.
  certified_key_proof->assign(
      &validation.rgbValidationData[0],
      &validation.rgbValidationData[validation.ulValidationDataLength]);
  return true;
}

bool TpmImpl::CreateDelegate(const SecureBlob& identity_key_blob,
                             SecureBlob* delegate_blob,
                             SecureBlob* delegate_secret) {
  CHECK(delegate_blob && delegate_secret);

  // Connect to the TPM as the owner.
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsOwner(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "CreateDelegate: Could not connect to the TPM.";
    return false;
  }

  // Load the Storage Root Key.
  TSS_RESULT result;
  ScopedTssKey srk_handle(context_handle);
  if (!LoadSrk(context_handle, srk_handle.ptr(), &result)) {
    TPM_LOG(INFO, result) << "CreateDelegate: Failed to load SRK.";
    return false;
  }

  // Load the AIK (which is wrapped by the SRK).
  ScopedTssKey identity_key(context_handle);
  result = Tspi_Context_LoadKeyByBlob(
      context_handle,
      srk_handle,
      identity_key_blob.size(),
      const_cast<BYTE*>(identity_key_blob.data()),
      identity_key.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateDelegate: Failed to load AIK.";
    return false;
  }

  // Generate a delegate secret.
  if (!GetRandomData(kDelegateSecretSize, delegate_secret)) {
    return false;
  }

  // Create an owner delegation policy.
  ScopedTssPolicy policy(context_handle);
  result = Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_POLICY,
                                     TSS_POLICY_USAGE, policy.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateDelegate: Failed to create policy.";
    return false;
  }
  result = Tspi_Policy_SetSecret(policy, TSS_SECRET_MODE_PLAIN,
                                 delegate_secret->size(),
                                 delegate_secret->data());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateDelegate: Failed to set policy secret.";
    return false;
  }
  result = Tspi_SetAttribUint32(policy, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                                TSS_TSPATTRIB_POLDEL_TYPE,
                                TSS_DELEGATIONTYPE_OWNER);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateDelegate: Failed to set delegation type.";
    return false;
  }
  // These are the privileged operations we will allow the delegate to perform.
  const UINT32 permissions = TPM_DELEGATE_ActivateIdentity |
                             TPM_DELEGATE_DAA_Join |
                             TPM_DELEGATE_DAA_Sign |
                             TPM_DELEGATE_ResetLockValue;
  result = Tspi_SetAttribUint32(policy, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                                TSS_TSPATTRIB_POLDEL_PER1, permissions);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateDelegate: Failed to set permissions.";
    return false;
  }
  result = Tspi_SetAttribUint32(policy, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                                TSS_TSPATTRIB_POLDEL_PER2, 0);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateDelegate: Failed to set permissions.";
    return false;
  }

  // Create a delegation family.
  ScopedTssObject<TSS_HDELFAMILY> family(context_handle);
  result = Tspi_TPM_Delegate_AddFamily(tpm_handle, kDelegateFamilyLabel,
                                       family.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateDelegate: Failed to create family.";
    return false;
  }

  // Create the delegation.
  result = Tspi_TPM_Delegate_CreateDelegation(tpm_handle, kDelegateEntryLabel,
                                              0, 0, family, policy);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateDelegate: Failed to create delegation.";
    return false;
  }

  // Enable the delegation family.
  result = Tspi_SetAttribUint32(family, TSS_TSPATTRIB_DELFAMILY_STATE,
                                TSS_TSPATTRIB_DELFAMILYSTATE_ENABLED, TRUE);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "CreateDelegate: Failed to enable family.";
    return false;
  }

  // Save the delegation blob for later.
  if (!GetDataAttribute(context_handle,
                        policy,
                        TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                        TSS_TSPATTRIB_POLDEL_OWNERBLOB,
                        delegate_blob)) {
    LOG(ERROR) << "CreateDelegate: Failed to get delegate blob.";
    return false;
  }
  return true;
}

bool TpmImpl::ActivateIdentity(const SecureBlob& delegate_blob,
                               const SecureBlob& delegate_secret,
                               const SecureBlob& identity_key_blob,
                               const SecureBlob& encrypted_asym_ca,
                               const SecureBlob& encrypted_sym_ca,
                               SecureBlob* identity_credential) {
  CHECK(identity_credential);

  // Connect to the TPM as the owner delegate.
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsDelegate(delegate_blob, delegate_secret,
                                context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "ActivateIdentity: Could not connect to the TPM.";
    return false;
  }

  // Load the Storage Root Key.
  TSS_RESULT result;
  ScopedTssKey srk_handle(context_handle);
  if (!LoadSrk(context_handle, srk_handle.ptr(), &result)) {
    TPM_LOG(INFO, result) << "ActivateIdentity: Failed to load SRK.";
    return false;
  }

  // Load the AIK (which is wrapped by the SRK).
  ScopedTssKey identity_key(context_handle);
  result = Tspi_Context_LoadKeyByBlob(
      context_handle,
      srk_handle,
      identity_key_blob.size(),
      const_cast<BYTE*>(identity_key_blob.data()),
      identity_key.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "ActivateIdentity: Failed to load AIK.";
    return false;
  }

  BYTE* encrypted_asym_ca_buffer = const_cast<BYTE*>(encrypted_asym_ca.data());
  BYTE* encrypted_sym_ca_buffer = const_cast<BYTE*>(encrypted_sym_ca.data());
  UINT32 credential_length = 0;
  ScopedTssMemory credential_buffer(context_handle);
  result = Tspi_TPM_ActivateIdentity(tpm_handle, identity_key,
                                     encrypted_asym_ca.size(),
                                     encrypted_asym_ca_buffer,
                                     encrypted_sym_ca.size(),
                                     encrypted_sym_ca_buffer,
                                     &credential_length,
                                     credential_buffer.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "ActivateIdentity: Failed to activate identity.";
    return false;
  }
  identity_credential->assign(&credential_buffer.value()[0],
                              &credential_buffer.value()[credential_length]);
  brillo::SecureMemset(credential_buffer.value(), 0, credential_length);
  return true;
}

bool TpmImpl::Sign(const SecureBlob& key_blob,
                   const SecureBlob& input,
                   int bound_pcr_index,
                   SecureBlob* signature) {
  CHECK(signature);
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "Sign: Failed to connect to the TPM.";
    return false;
  }

  // Load the Storage Root Key.
  TSS_RESULT result;
  ScopedTssKey srk_handle(context_handle);
  if (!LoadSrk(context_handle, srk_handle.ptr(), &result)) {
    TPM_LOG(INFO, result) << "Sign: Failed to load SRK.";
    return false;
  }

  // Load the key (which should be wrapped by the SRK).
  ScopedTssKey key_handle(context_handle);
  result = Tspi_Context_LoadKeyByBlob(
      context_handle,
      srk_handle,
      key_blob.size(),
      const_cast<BYTE*>(key_blob.data()),
      key_handle.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Sign: Failed to load key.";
    return false;
  }

  // Create a hash object to hold the input.
  ScopedTssObject<TSS_HHASH> hash_handle(context_handle);
  result = Tspi_Context_CreateObject(context_handle,
                                     TSS_OBJECT_TYPE_HASH,
                                     TSS_HASH_OTHER,
                                     hash_handle.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Sign: Failed to create hash object.";
    return false;
  }

  // Create the DER encoded input.
  SecureBlob der_header(std::begin(kSha256DigestInfo),
                        std::end(kSha256DigestInfo));
  SecureBlob der_encoded_input = SecureBlob::Combine(
      der_header,
      CryptoLib::Sha256(input));

  // Don't hash anything, just push the input data into the hash object.
  result = Tspi_Hash_SetHashValue(
      hash_handle,
      der_encoded_input.size(),
      const_cast<BYTE*>(der_encoded_input.data()));
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Sign: Failed to set hash data.";
    return false;
  }

  UINT32 length = 0;
  ScopedTssMemory buffer(context_handle);
  result = Tspi_Hash_Sign(hash_handle, key_handle, &length, buffer.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Sign: Failed to generate signature.";
    return false;
  }
  SecureBlob tmp(buffer.value(), buffer.value() + length);
  brillo::SecureMemset(buffer.value(), 0, length);
  signature->swap(tmp);
  return true;
}

bool TpmImpl::CreatePCRBoundKey(int pcr_index,
                                const brillo::SecureBlob& pcr_value,
                                brillo::SecureBlob* key_blob,
                                brillo::SecureBlob* public_key_der,
                                brillo::SecureBlob* creation_blob) {
  CHECK(creation_blob) << "Error no creation_blob.";
  creation_blob->clear();
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << __func__ << ": Failed to connect to the TPM.";
    return false;
  }

  // Load the Storage Root Key.
  TSS_RESULT result;
  ScopedTssKey srk_handle(context_handle);
  if (!LoadSrk(context_handle, srk_handle.ptr(), &result)) {
    TPM_LOG(INFO, result) << __func__ << ": Failed to load SRK.";
    return false;
  }

  // Create a PCRS object to hold pcr_index and pcr_value.
  ScopedTssPcrs pcrs(context_handle);
  result = Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_PCRS,
                                     TSS_PCRS_STRUCT_INFO, pcrs.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to create PCRS object.";
    return false;
  }
  BYTE* pcr_value_buffer = const_cast<BYTE*>(pcr_value.data());
  result = Tspi_PcrComposite_SetPcrValue(pcrs, pcr_index, pcr_value.size(),
                                         pcr_value_buffer);

  // Create a non-migratable signing key restricted to |pcrs|.
  ScopedTssKey signing_key(context_handle);
  UINT32 init_flags = TSS_KEY_TYPE_SIGNING |
                      TSS_KEY_NOT_MIGRATABLE |
                      TSS_KEY_VOLATILE |
                      kDefaultTpmRsaKeyFlag;
  result = Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_RSAKEY,
                                     init_flags, signing_key.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to create object.";
    return false;
  }
  result = Tspi_SetAttribUint32(signing_key,
                                TSS_TSPATTRIB_KEY_INFO,
                                TSS_TSPATTRIB_KEYINFO_SIGSCHEME,
                                TSS_SS_RSASSAPKCS1V15_DER);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to set signature scheme.";
    return false;
  }
  result = Tspi_Key_CreateKey(signing_key, srk_handle, pcrs);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to create key.";
    return false;
  }
  result = Tspi_Key_LoadKey(signing_key, srk_handle);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to load key.";
    return false;
  }

  // Get the public key.
  SecureBlob public_key;
  if (!GetDataAttribute(context_handle,
                        signing_key,
                        TSS_TSPATTRIB_KEY_BLOB,
                        TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY,
                        &public_key)) {
    LOG(ERROR) << __func__ << ": Failed to read public key.";
    return false;
  }
  if (!ConvertPublicKeyToDER(public_key, public_key_der)) {
    return false;
  }

  // Get the key blob so we can load it later.
  if (!GetDataAttribute(context_handle,
                        signing_key,
                        TSS_TSPATTRIB_KEY_BLOB,
                        TSS_TSPATTRIB_KEYBLOB_BLOB,
                        key_blob)) {
    LOG(ERROR) << __func__ << ": Failed to read key blob.";
    return false;
  }
  return true;
}

bool TpmImpl::VerifyPCRBoundKey(int pcr_index,
                                const brillo::SecureBlob& pcr_value,
                                const brillo::SecureBlob& key_blob,
                                const brillo::SecureBlob& creation_blob) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << __func__ << ": Failed to connect to the TPM.";
    return false;
  }

  TSS_RESULT result;
  ScopedTssKey srk_handle(context_handle);
  if (!LoadSrk(context_handle, srk_handle.ptr(), &result)) {
    TPM_LOG(INFO, result) << __func__ << ": Failed to load SRK.";
    return false;
  }

  ScopedTssKey key(context_handle);
  result = Tspi_Context_LoadKeyByBlob(
      context_handle,
      srk_handle,
      key_blob.size(),
      const_cast<BYTE*>(key_blob.data()),
      key.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to load key.";
    return false;
  }

  // Check that |pcr_index| is selected.
  SecureBlob pcr_selection_blob;
  if (!GetDataAttribute(context_handle, key,
                        TSS_TSPATTRIB_KEY_PCR,
                        TSS_TSPATTRIB_KEYPCR_SELECTION,
                        &pcr_selection_blob)) {
    LOG(ERROR) << __func__ << ": Failed to read PCR selection for key.";
    return false;
  }
  UINT64 trspi_offset = 0;
  TPM_PCR_SELECTION pcr_selection;
  Trspi_UnloadBlob_PCR_SELECTION(&trspi_offset,
                                 pcr_selection_blob.data(),
                                 &pcr_selection);
  if (!pcr_selection.pcrSelect) {
    LOG(ERROR) << __func__ << ": No PCR selected.";
    return false;
  }
  SecureBlob pcr_bitmap(pcr_selection.pcrSelect,
                        pcr_selection.pcrSelect + pcr_selection.sizeOfSelect);
  free(pcr_selection.pcrSelect);
  size_t offset = pcr_index / 8;
  unsigned char mask = 1 << (pcr_index % 8);
  if (pcr_bitmap.size() <= offset || (pcr_bitmap[offset] & mask) == 0) {
    LOG(ERROR) << __func__ << ": Invalid PCR selection.";
    return false;
  }

  // Compute the PCR composite hash we're expecting.  Basically, we want to do
  // the equivalent of hashing a TPM_PCR_COMPOSITE structure.
  trspi_offset = 0;
  UINT32 pcr_value_length = pcr_value.size();
  SecureBlob pcr_value_length_blob(sizeof(UINT32));
  Trspi_LoadBlob_UINT32(&trspi_offset,
                        pcr_value_length,
                        pcr_value_length_blob.data());
  SecureBlob pcr_hash = CryptoLib::Sha1(SecureBlob::Combine(
      SecureBlob::Combine(
      pcr_selection_blob,
      pcr_value_length_blob),
      pcr_value));

  // Check that the PCR value matches the key creation PCR value.
  SecureBlob pcr_at_creation;
  if (!GetDataAttribute(context_handle, key,
                        TSS_TSPATTRIB_KEY_PCR,
                        TSS_TSPATTRIB_KEYPCR_DIGEST_ATCREATION,
                        &pcr_at_creation)) {
    LOG(ERROR) << __func__ << ": Failed to read PCR value at key creation.";
    return false;
  }

  if (pcr_at_creation != pcr_hash) {
    LOG(ERROR) << __func__ << ": Invalid key creation PCR.";
    return false;
  }

  // Check that the PCR value matches the PCR value required to use the key.
  SecureBlob pcr_at_release;
  if (!GetDataAttribute(context_handle, key,
                        TSS_TSPATTRIB_KEY_PCR,
                        TSS_TSPATTRIB_KEYPCR_DIGEST_ATRELEASE,
                        &pcr_at_release)) {
    LOG(ERROR) << __func__ << ": Failed to read PCR value for key usage.";
    return false;
  }
  if (pcr_at_release != pcr_hash) {
    LOG(ERROR) << __func__ << ": Invalid key usage PCR.";
    return false;
  }
  return true;
}

bool TpmImpl::ExtendPCR(int pcr_index, const brillo::SecureBlob& extension) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << __func__ << ": Failed to connect to the TPM.";
    return false;
  }
  CHECK_EQ(extension.size(), kPCRExtensionSize);
  SecureBlob mutable_extension = extension;
  UINT32 new_pcr_value_length = 0;
  ScopedTssMemory new_pcr_value(context_handle);
  TSS_RESULT result = Tspi_TPM_PcrExtend(tpm_handle,
                                         pcr_index,
                                         extension.size(),
                                         mutable_extension.data(),
                                         NULL,
                                         &new_pcr_value_length,
                                         new_pcr_value.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to extend PCR "
                           << pcr_index;
    return false;
  }
  return true;
}

bool TpmImpl::ReadPCR(int pcr_index, brillo::SecureBlob* pcr_value) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << __func__ << ": Failed to connect to the TPM.";
    return false;
  }
  UINT32 pcr_len = 0;
  ScopedTssMemory pcr_value_buffer(context_handle);
  TSS_RESULT result = Tspi_TPM_PcrRead(tpm_handle, pcr_index,
                                       &pcr_len, pcr_value_buffer.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not read PCR0 value";
    return false;
  }
  SecureBlob tmp(pcr_value_buffer.value(), pcr_value_buffer.value() + pcr_len);
  pcr_value->swap(tmp);
  return true;
}

bool TpmImpl::GetDataAttribute(TSS_HCONTEXT context,
                               TSS_HOBJECT object,
                               TSS_FLAG flag,
                               TSS_FLAG sub_flag,
                               SecureBlob* data) const {
  UINT32 length = 0;
  ScopedTssMemory buf(context);
  TSS_RESULT result = Tspi_GetAttribData(object, flag, sub_flag, &length,
                                         buf.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << "Failed to read object attribute.";
    return false;
  }
  SecureBlob tmp(buf.value(), buf.value() + length);
  brillo::SecureMemset(buf.value(), 0, length);
  data->swap(tmp);
  return true;
}

bool TpmImpl::GetCapability(TSS_HCONTEXT context_handle,
                            TSS_HTPM tpm_handle,
                            UINT32 capability,
                            UINT32 sub_capability,
                            brillo::Blob* data,
                            UINT32* value) const {
  UINT32 length = 0;
  ScopedTssMemory buf(context_handle);
  TSS_RESULT result = Tspi_TPM_GetCapability(
      tpm_handle,
      capability,
      sizeof(UINT32),
      reinterpret_cast<BYTE*>(&sub_capability),
      &length,
      buf.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to get capability.";
    return false;
  }
  if (data) {
    data->assign(buf.value(), buf.value() + length);
  }
  if (value) {
    if (length != sizeof(UINT32)) {
      return false;
    }
    UINT64 offset = 0;
    Trspi_UnloadBlob_UINT32(&offset, value, buf.value());
  }
  return true;
}

void TpmImpl::SetOwnerPassword(const brillo::SecureBlob& owner_password) {
  base::AutoLock lock(password_sync_lock_);
  owner_password_.assign(owner_password.begin(), owner_password.end());
}

bool TpmImpl::WrapRsaKey(const SecureBlob& public_modulus,
                         const SecureBlob& prime_factor,
                         SecureBlob* wrapped_key) {
  TSS_RESULT result;
  // Load the Storage Root Key
  trousers::ScopedTssKey srk_handle(tpm_context_.value());
  if (!LoadSrk(tpm_context_.value(), srk_handle.ptr(), &result)) {
    if (result != kKeyNotFoundError) {
      TPM_LOG(INFO, result) << "WrapRsaKey: Cannot load SRK";
    }
    return false;
  }

  // Make sure we can get the public key for the SRK.  If not, then the TPM
  // is not available.
  unsigned int size_n;
  trousers::ScopedTssMemory public_srk(tpm_context_.value());
  if (TPM_ERROR(result = Tspi_Key_GetPubKey(srk_handle, &size_n,
                                            public_srk.ptr()))) {
    TPM_LOG(INFO, result) << "WrapRsaKey: Cannot load SRK pub key";
    return false;
  }

  // Create the key object
  TSS_FLAG init_flags = TSS_KEY_TYPE_LEGACY | TSS_KEY_VOLATILE | \
                        TSS_KEY_MIGRATABLE | kDefaultTpmRsaKeyFlag;
  trousers::ScopedTssKey local_key_handle(tpm_context_.value());
  if (TPM_ERROR(result = Tspi_Context_CreateObject(tpm_context_.value(),
                                                   TSS_OBJECT_TYPE_RSAKEY,
                                                   init_flags,
                                                   local_key_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_Context_CreateObject";
    return false;
  }

  // Set the attributes
  UINT32 sig_scheme = TSS_SS_RSASSAPKCS1V15_DER;
  if (TPM_ERROR(result = Tspi_SetAttribUint32(local_key_handle,
                                              TSS_TSPATTRIB_KEY_INFO,
                                              TSS_TSPATTRIB_KEYINFO_SIGSCHEME,
                                              sig_scheme))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_SetAttribUint32";
    return false;
  }

  UINT32 enc_scheme = TSS_ES_RSAESPKCSV15;
  if (TPM_ERROR(result = Tspi_SetAttribUint32(local_key_handle,
                                              TSS_TSPATTRIB_KEY_INFO,
                                              TSS_TSPATTRIB_KEYINFO_ENCSCHEME,
                                              enc_scheme))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_SetAttribUint32";
    return false;
  }

  trousers::ScopedTssPolicy policy_handle(tpm_context_.value());
  if (TPM_ERROR(result = Tspi_Context_CreateObject(tpm_context_.value(),
                                                   TSS_OBJECT_TYPE_POLICY,
                                                   TSS_POLICY_MIGRATION,
                                                   policy_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Error creating policy object";
    return false;
  }

  // Set a random migration policy password, and discard it.  The key will not
  // be migrated, but to create the key outside of the TPM, we have to do it
  // this way.
  SecureBlob migration_password(kDefaultDiscardableWrapPasswordLength);
  CryptoLib::GetSecureRandom(migration_password.data(),
                             migration_password.size());
  if (TPM_ERROR(result = Tspi_Policy_SetSecret(policy_handle,
                         TSS_SECRET_MODE_PLAIN,
                         migration_password.size(),
                         migration_password.data()))) {
    TPM_LOG(ERROR, result) << "Error setting migration policy password";
    return false;
  }

  if (TPM_ERROR(result = Tspi_Policy_AssignToObject(policy_handle,
                                                    local_key_handle))) {
    TPM_LOG(ERROR, result) << "Error assigning migration policy";
    return false;
  }
  SecureBlob mutable_modulus(public_modulus.begin(), public_modulus.end());
  BYTE* public_modulus_buffer = static_cast<BYTE *>(mutable_modulus.data());
  if (TPM_ERROR(result = Tspi_SetAttribData(local_key_handle,
                                            TSS_TSPATTRIB_RSAKEY_INFO,
                                            TSS_TSPATTRIB_KEYINFO_RSA_MODULUS,
                                            public_modulus.size(),
                                            public_modulus_buffer))) {
    TPM_LOG(ERROR, result) << "Error setting RSA modulus";
    return false;
  }
  SecureBlob mutable_factor(prime_factor.begin(), prime_factor.end());
  BYTE* prime_factor_buffer = static_cast<BYTE *>(mutable_factor.data());
  if (TPM_ERROR(result = Tspi_SetAttribData(local_key_handle,
                                            TSS_TSPATTRIB_KEY_BLOB,
                                            TSS_TSPATTRIB_KEYBLOB_PRIVATE_KEY,
                                            prime_factor.size(),
                                            prime_factor_buffer))) {
    TPM_LOG(ERROR, result) << "Error setting private key";
    return false;
  }

  if (TPM_ERROR(result = Tspi_Key_WrapKey(local_key_handle,
                                          srk_handle,
                                          0))) {
    TPM_LOG(ERROR, result) << "Error wrapping RSA key";
    return false;
  }

  if (!GetKeyBlob(
      tpm_context_.value(), local_key_handle, wrapped_key, &result)) {
    return false;
  }

  return true;
}

bool TpmImpl::GetKeyBlob(TSS_HCONTEXT context_handle, TSS_HKEY key_handle,
                         SecureBlob* data_out, TSS_RESULT* result) const {
  *result = TSS_SUCCESS;

  if (!GetDataAttribute(context_handle, key_handle, TSS_TSPATTRIB_KEY_BLOB,
      TSS_TSPATTRIB_KEYBLOB_BLOB, data_out)) {
    LOG(ERROR) << __func__ << ": Failed to get key blob.";
    return false;
  }

  return true;
}

Tpm::TpmRetryAction TpmImpl::LoadWrappedKey(
    const brillo::SecureBlob& wrapped_key,
    ScopedKeyHandle* key_handle) {
  CHECK(key_handle);
  TSS_RESULT result = TSS_SUCCESS;
  // Load the Storage Root Key
  trousers::ScopedTssKey srk_handle(tpm_context_.value());
  if (!LoadSrk(tpm_context_.value(), srk_handle.ptr(), &result)) {
    if (result != kKeyNotFoundError) {
      TPM_LOG(INFO, result) << "LoadWrappedKey: Cannot load SRK";
      ReportCryptohomeError(kCannotLoadTpmSrk);
    }
    return ResultToRetryAction(result);
  }

  // Make sure we can get the public key for the SRK.  If not, then the TPM
  // is not available.
  {
    SecureBlob pubkey;
    if (!GetPublicKeyBlob(tpm_context_.value(), srk_handle, &pubkey, &result)) {
      TPM_LOG(INFO, result) << "LoadWrappedKey: Cannot load SRK public key";
      ReportCryptohomeError(kCannotReadTpmSrkPublic);
      return ResultToRetryAction(result);
    }
  }
  TpmKeyHandle local_key_handle;
  if (TPM_ERROR(result = Tspi_Context_LoadKeyByBlob(
                                 tpm_context_.value(),
                                 srk_handle,
                                 wrapped_key.size(),
                                 const_cast<BYTE*>(wrapped_key.data()),
                                 &local_key_handle))) {
    TPM_LOG(INFO, result) << "LoadWrappedKey: Cannot load key from blob";
    ReportCryptohomeError(kCannotLoadTpmKey);
    if (result == TPM_E_BAD_KEY_PROPERTY) {
      ReportCryptohomeError(kTpmBadKeyProperty);
    }
    return ResultToRetryAction(result);
  }

  SecureBlob pub_key;
  // Make sure that we can get the public key
  if (!GetPublicKeyBlob(tpm_context_.value(), local_key_handle,
                        &pub_key, &result)) {
    ReportCryptohomeError(kCannotReadTpmPublicKey);
    Tspi_Context_CloseObject(tpm_context_.value(), local_key_handle);
    return ResultToRetryAction(result);
  }
  key_handle->reset(this, local_key_handle);
  return kTpmRetryNone;
}

bool TpmImpl::LegacyLoadCryptohomeKey(ScopedKeyHandle* key_handle,
                                      brillo::SecureBlob* key_blob) {
  CHECK(key_handle);
  TSS_RESULT result = TSS_SUCCESS;
  TpmKeyHandle local_key_handle;
  if (TPM_ERROR(result = Tspi_Context_LoadKeyByUUID(tpm_context_.value(),
                                                    TSS_PS_TYPE_SYSTEM,
                                                    kCryptohomeWellKnownUuid,
                                                    &local_key_handle))) {
    TPM_LOG(INFO, result) << "LoadKeyByUuid: failed LoadKeyByUUID";
    return false;
  }

  if (key_blob && !GetKeyBlob(
      tpm_context_.value(), local_key_handle, key_blob, &result)) {
    Tspi_Context_CloseObject(tpm_context_.value(), local_key_handle);
    return false;
  }
  key_handle->reset(this, local_key_handle);
  return true;
}

void TpmImpl::CloseHandle(TpmKeyHandle key_handle) {
  Tspi_Context_CloseObject(tpm_context_.value(), key_handle);
}

}  // namespace cryptohome
