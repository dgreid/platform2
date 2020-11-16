// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libtpmcrypto/tpm1_impl.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <brillo/secure_blob.h>
#include <trousers/scoped_tss_type.h>

using base::PlatformThread;
using base::TimeDelta;
using brillo::Blob;
using brillo::SecureBlob;
using trousers::ScopedTssContext;
using trousers::ScopedTssKey;
using trousers::ScopedTssMemory;
using trousers::ScopedTssNvStore;
using trousers::ScopedTssPcrs;

namespace tpmcrypto {

#define TPM_LOG(severity, result)                               \
  LOG(severity) << "TPM error 0x" << std::hex << result << " (" \
                << Trspi_Error_String(result) << "): "

constexpr unsigned char kDefaultSrkAuth[] = {};
constexpr unsigned int kTpmConnectRetries = 10;
constexpr unsigned int kTpmConnectIntervalMs = 100;

Tpm1Impl::Tpm1Impl() = default;
Tpm1Impl::~Tpm1Impl() = default;

std::unique_ptr<Tpm> CreateTpmInstance() {
  return std::make_unique<Tpm1Impl>();
}

bool Tpm1Impl::SealToPCR0(const SecureBlob& value, SecureBlob* sealed_value) {
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
    TPM_LOG(ERROR, result) << "SealToPCR0: Failed to load SRK.";
    return false;
  }

  // Check the SRK public key
  unsigned int size_n = 0;
  ScopedTssMemory public_srk(context_handle);
  if (TPM_ERROR(
          result = Tspi_Key_GetPubKey(srk_handle, &size_n, public_srk.ptr()))) {
    TPM_LOG(ERROR, result) << "SealToPCR0: Unable to get the SRK public key";
    return false;
  }

  // Create a PCRS object which holds the value of PCR0.
  ScopedTssPcrs pcrs_handle(context_handle);
  if (TPM_ERROR(result = Tspi_Context_CreateObject(
                    context_handle, TSS_OBJECT_TYPE_PCRS, TSS_PCRS_STRUCT_INFO,
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
  if (TPM_ERROR(result = Tspi_Context_CreateObject(
                    context_handle, TSS_OBJECT_TYPE_ENCDATA, TSS_ENCDATA_SEAL,
                    enc_handle.ptr()))) {
    TPM_LOG(ERROR, result)
        << "SealToPCR0: Error calling Tspi_Context_CreateObject";
    return false;
  }

  // Seal the given value with the SRK.
  if (TPM_ERROR(result = Tspi_Data_Seal(enc_handle, srk_handle, value.size(),
                                        const_cast<BYTE*>(value.data()),
                                        pcrs_handle))) {
    TPM_LOG(ERROR, result) << "SealToPCR0: Error calling Tspi_Data_Seal";
    return false;
  }

  // Extract the sealed value.
  ScopedTssMemory enc_data(context_handle);
  UINT32 enc_data_length = 0;
  if (TPM_ERROR(result =
                    Tspi_GetAttribData(enc_handle, TSS_TSPATTRIB_ENCDATA_BLOB,
                                       TSS_TSPATTRIB_ENCDATABLOB_BLOB,
                                       &enc_data_length, enc_data.ptr()))) {
    TPM_LOG(ERROR, result) << "SealToPCR0: Error calling Tspi_GetAttribData";
    return false;
  }
  sealed_value->assign(&enc_data.value()[0],
                       &enc_data.value()[enc_data_length]);
  return true;
}

bool Tpm1Impl::Unseal(const SecureBlob& sealed_value, SecureBlob* value) {
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
    TPM_LOG(ERROR, result) << "Unseal: Failed to load SRK.";
    return false;
  }

  // Create an ENCDATA object with the sealed value.
  ScopedTssKey enc_handle(context_handle);
  if (TPM_ERROR(result = Tspi_Context_CreateObject(
                    context_handle, TSS_OBJECT_TYPE_ENCDATA, TSS_ENCDATA_SEAL,
                    enc_handle.ptr()))) {
    TPM_LOG(ERROR, result) << "Unseal: Error calling Tspi_Context_CreateObject";
    return false;
  }

  if (TPM_ERROR(result = Tspi_SetAttribData(
                    enc_handle, TSS_TSPATTRIB_ENCDATA_BLOB,
                    TSS_TSPATTRIB_ENCDATABLOB_BLOB, sealed_value.size(),
                    const_cast<BYTE*>(sealed_value.data())))) {
    TPM_LOG(ERROR, result) << "Unseal: Error calling Tspi_SetAttribData";
    return false;
  }

  // Unseal using the SRK.
  ScopedTssMemory dec_data(context_handle);
  UINT32 dec_data_length = 0;
  if (TPM_ERROR(result = Tspi_Data_Unseal(enc_handle, srk_handle,
                                          &dec_data_length, dec_data.ptr()))) {
    TPM_LOG(ERROR, result) << "Unseal: Error calling Tspi_Data_Unseal";
    return false;
  }
  value->assign(&dec_data.value()[0], &dec_data.value()[dec_data_length]);
  brillo::SecureClear(dec_data.value(), dec_data_length);
  return true;
}

TSS_HCONTEXT Tpm1Impl::ConnectContext() {
  TSS_RESULT result;
  TSS_HCONTEXT context_handle = 0;
  if (!OpenAndConnectTpm(&context_handle, &result)) {
    return 0;
  }
  return context_handle;
}

bool Tpm1Impl::OpenAndConnectTpm(TSS_HCONTEXT* context_handle,
                                 TSS_RESULT* result) {
  TSS_RESULT local_result;
  ScopedTssContext local_context_handle;
  if (TPM_ERROR(local_result =
                    Tspi_Context_Create(local_context_handle.ptr()))) {
    TPM_LOG(ERROR, local_result) << "Error calling Tspi_Context_Create";
    if (result)
      *result = local_result;
    return false;
  }

  for (unsigned int i = 0; i < kTpmConnectRetries; i++) {
    LOG(INFO) << "Attempting to connect to TPM";
    if (TPM_ERROR(local_result =
                      Tspi_Context_Connect(local_context_handle, NULL))) {
      // If there was a communications failure, try sleeping a bit here--it may
      // be that tcsd is still starting
      if (ERROR_CODE(local_result) == TSS_E_COMM_FAILURE) {
        LOG(INFO) << "Sleeping to wait for TPM";
        PlatformThread::Sleep(
            TimeDelta::FromMilliseconds(kTpmConnectIntervalMs));
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

bool Tpm1Impl::GetTpm(TSS_HCONTEXT context_handle, TSS_HTPM* tpm_handle) {
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

bool Tpm1Impl::ConnectContextAsUser(TSS_HCONTEXT* context, TSS_HTPM* tpm) {
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

bool Tpm1Impl::LoadSrk(TSS_HCONTEXT context_handle,
                       TSS_HKEY* srk_handle,
                       TSS_RESULT* result) const {
  *result = TSS_SUCCESS;

  // Load the Storage Root Key
  TSS_UUID SRK_UUID = TSS_UUID_SRK;
  ScopedTssKey local_srk_handle(context_handle);
  if (TPM_ERROR(*result = Tspi_Context_LoadKeyByUUID(
                    context_handle, TSS_PS_TYPE_SYSTEM, SRK_UUID,
                    local_srk_handle.ptr()))) {
    return false;
  }

  // Check if the SRK wants a password
  UINT32 srk_authusage;
  if (TPM_ERROR(*result = Tspi_GetAttribUint32(
                    local_srk_handle, TSS_TSPATTRIB_KEY_INFO,
                    TSS_TSPATTRIB_KEYINFO_AUTHUSAGE, &srk_authusage))) {
    return false;
  }

  // Give it the password if needed
  if (srk_authusage) {
    TSS_HPOLICY srk_usage_policy;
    if (TPM_ERROR(*result = Tspi_GetPolicyObject(
                      local_srk_handle, TSS_POLICY_USAGE, &srk_usage_policy))) {
      return false;
    }

    *result = Tspi_Policy_SetSecret(srk_usage_policy, TSS_SECRET_MODE_PLAIN,
                                    sizeof(kDefaultSrkAuth),
                                    const_cast<BYTE*>(kDefaultSrkAuth));
    if (TPM_ERROR(*result)) {
      return false;
    }
  }

  *srk_handle = local_srk_handle.release();
  return true;
}

bool Tpm1Impl::GetNVAttributes(uint32_t index, uint32_t* attributes) {
  CHECK(attributes);
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "GetNVAttributes: Failed to connect to the TPM.";
    return false;
  }

  TSS_RESULT result;
  UINT32 nv_index_data_length = sizeof(TPM_NV_DATA_PUBLIC);
  ScopedTssMemory nv_index_data(context_handle);
  if (TPM_ERROR(result = Tspi_TPM_GetCapability(
                    tpm_handle, TSS_TPMCAP_NV_INDEX, sizeof(index),
                    reinterpret_cast<BYTE*>(&index), &nv_index_data_length,
                    nv_index_data.ptr()))) {
    TPM_LOG(ERROR, result) << "Error calling Tspi_TPM_GetCapability";
    return false;
  }
  if (nv_index_data_length == 0) {
    LOG(ERROR) << "The NV index public data length is not valid";
    return false;
  }

  TPM_NV_DATA_PUBLIC nv_data_public;
  UINT64 offset = 0;
  if (TPM_ERROR(result = Trspi_UnloadBlob_NV_DATA_PUBLIC(
                    &offset, *nv_index_data.ptr(), &nv_data_public))) {
    TPM_LOG(ERROR, result) << "Error unloading NV public data.";
    return false;
  }

  *attributes = nv_data_public.permission.attributes;
  return true;
}

bool Tpm1Impl::NVReadNoAuth(uint32_t index,
                            uint32_t offset,
                            size_t size,
                            std::string* data) {
  CHECK(data);
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "NVReadNoAuth: Failed to connect to the TPM.";
    return false;
  }

  // Create an NVRAM store object handle.
  TSS_RESULT result;
  ScopedTssNvStore nv_handle(context_handle);
  result = Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_NV, 0,
                                     nv_handle.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not acquire an NVRAM object handle";
    return false;
  }
  result = Tspi_SetAttribUint32(nv_handle, TSS_TSPATTRIB_NV_INDEX, 0, index);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << "Could not set index on NVRAM object: " << index;
    return false;
  }

  SecureBlob blob(size);
  // Read from NVRAM in conservatively small chunks. This is a limitation of the
  // TPM that is left for the application layer to deal with. The maximum size
  // that is supported here can vary between vendors / models, so we'll be
  // conservative. FWIW, the Infineon chips seem to handle up to 1024.
  const UINT32 kMaxDataSize = 128;
  UINT32 offset_l = 0;
  while (offset_l < size) {
    UINT32 chunk_size = size - offset_l;
    if (chunk_size > kMaxDataSize)
      chunk_size = kMaxDataSize;
    ScopedTssMemory space_data(context_handle);
    if ((result = Tspi_NV_ReadValue(nv_handle, offset_l + offset, &chunk_size,
                                    space_data.ptr()))) {
      TPM_LOG(ERROR, result) << "Could not read from NVRAM space: " << index;
      return false;
    }
    if (!space_data.value()) {
      LOG(ERROR) << "No data read from NVRAM space: " << index;
      return false;
    }
    CHECK(offset_l + chunk_size <= blob.size());
    unsigned char* buffer = blob.data() + offset_l;
    memcpy(buffer, space_data.value(), chunk_size);
    offset_l += chunk_size;
  }
  *data = std::string(blob.begin(), blob.end());
  return true;
}

}  // namespace tpmcrypto
