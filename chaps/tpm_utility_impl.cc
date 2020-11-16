// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/tpm_utility_impl.h"

#include <map>
#include <set>
#include <sstream>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/synchronization/lock.h>
#include <brillo/secure_blob.h>
#include <openssl/rand.h>
#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>

#include "chaps/chaps_utility.h"

using base::AutoLock;
using brillo::SecureBlob;
using std::hex;
using std::map;
using std::set;
using std::string;
using std::stringstream;
using trousers::ScopedTssContext;
using trousers::ScopedTssKey;
using trousers::ScopedTssObject;
using trousers::ScopedTssPolicy;

namespace chaps {

// TSSEncryptedData wraps a TSS encrypted data object. The underlying TSS object
// will be closed when this object falls out of scope.
typedef ScopedTssObject<TSS_HENCDATA> ScopedTssEncData;
class TSSEncryptedData {
 public:
  explicit TSSEncryptedData(TSS_HCONTEXT context)
      : context_(context), handle_(context) {}
  TSSEncryptedData(const TSSEncryptedData&) = delete;
  TSSEncryptedData& operator=(const TSSEncryptedData&) = delete;

  bool Create() {
    TSS_RESULT result = Tspi_Context_CreateObject(
        context_, TSS_OBJECT_TYPE_ENCDATA, TSS_ENCDATA_BIND, handle_.ptr());
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_Context_CreateObject - "
                 << TPMUtilityImpl::ResultToString(result);
      return false;
    }

    return true;
  }
  bool GetData(string* data) {
    UINT32 length = 0;
    BYTE* buffer = NULL;
    TSS_RESULT result =
        Tspi_GetAttribData(handle_, TSS_TSPATTRIB_ENCDATA_BLOB,
                           TSS_TSPATTRIB_ENCDATABLOB_BLOB, &length, &buffer);
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_GetAttribData(ENCDATA_BLOB) - "
                 << TPMUtilityImpl::ResultToString(result);
      return false;
    }
    *data = ConvertByteBufferToString(buffer, length);
    Tspi_Context_FreeMemory(context_, buffer);
    return true;
  }
  bool SetData(const string& data) {
    TSS_RESULT result = Tspi_SetAttribData(
        handle_, TSS_TSPATTRIB_ENCDATA_BLOB, TSS_TSPATTRIB_ENCDATABLOB_BLOB,
        data.length(), ConvertStringToByteBuffer(data.data()));
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_SetAttribData(ENCDATA_BLOB) - "
                 << TPMUtilityImpl::ResultToString(result);
      return false;
    }
    return true;
  }
  operator TSS_HENCDATA() { return handle_; }

 private:
  TSS_HCONTEXT context_;
  ScopedTssObject<TSS_HENCDATA> handle_;
};

// TSSHash wraps a TSS hash object. The underlying TSS object will be closed
// when this object falls out of scope.
class TSSHash {
 public:
  explicit TSSHash(TSS_HCONTEXT context)
      : context_(context), handle_(context) {}
  TSSHash(const TSSHash&) = delete;
  TSSHash& operator=(const TSSHash&) = delete;

  bool Create(const string& value) {
    TSS_RESULT result = Tspi_Context_CreateObject(
        context_, TSS_OBJECT_TYPE_HASH, TSS_HASH_OTHER, handle_.ptr());
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_Context_CreateObject - "
                 << TPMUtilityImpl::ResultToString(result);
      return false;
    }

    result = Tspi_Hash_SetHashValue(handle_, value.length(),
                                    ConvertStringToByteBuffer(value.data()));
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_Hash_SetHashValue - "
                 << TPMUtilityImpl::ResultToString(result);
      return false;
    }
    return true;
  }
  operator TSS_HHASH() { return handle_; }

 private:
  TSS_HCONTEXT context_;
  ScopedTssObject<TSS_HHASH> handle_;
};

TPMUtilityImpl::TPMUtilityImpl(const string& srk_auth_data)
    : is_initialized_(false),
      is_srk_ready_(false),
      default_policy_(0),
      srk_(0),
      srk_auth_data_(srk_auth_data),
      srk_public_loaded_(false),
      default_exponent_("\x1\x0\x1", 3),
      last_handle_(0),
      is_enabled_(false),
      is_enabled_ready_(false) {}

TPMUtilityImpl::~TPMUtilityImpl() {
  LOG(INFO) << "Unloading keys for all slots.";
  map<int, HandleInfo>::iterator it;
  set<int>::iterator it2;
  for (it = slot_handles_.begin(); it != slot_handles_.end(); ++it) {
    set<int>* slot_handles = &it->second.handles_;
    for (it2 = slot_handles->begin(); it2 != slot_handles->end(); ++it2) {
      Tspi_Key_UnloadKey(GetTssHandle(*it2));
      Tspi_Context_CloseObject(tsp_context_, GetTssHandle(*it2));
    }
  }
  // These can't use ScopedTssObject because they must be closed before the
  // context (tsp_context_) closes.
  if (srk_)
    Tspi_Context_CloseObject(tsp_context_, srk_);
  if (default_policy_)
    Tspi_Context_CloseObject(tsp_context_, default_policy_);
}

bool TPMUtilityImpl::Init() {
  if (is_initialized_)
    return true;
  VLOG(1) << "TPMUtilityImpl::Init enter";
  AutoLock lock(lock_);
  TSS_RESULT result = TSS_SUCCESS;
  result = Tspi_Context_Create(tsp_context_.ptr());
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Context_Create - " << ResultToString(result);
    return false;
  }
  result = Tspi_Context_Connect(tsp_context_, NULL);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Context_Connect - " << ResultToString(result);
    return false;
  }
  // Get the default policy so we can compare against it later.
  result = Tspi_Context_GetDefaultPolicy(tsp_context_, &default_policy_);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Context_GetDefaultPolicy - " << ResultToString(result);
    return false;
  }
  // Make sure we can communicate with the TPM.
  TSS_HTPM tpm;
  result = Tspi_Context_GetTpmObject(tsp_context_, &tpm);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Context_GetTpmObject - " << ResultToString(result);
    return false;
  }
  BYTE* random_bytes = NULL;
  result = Tspi_TPM_GetRandom(tpm, 4, &random_bytes);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_TPM_GetRandom - " << ResultToString(result);
    return false;
  }
  Tspi_Context_FreeMemory(tsp_context_, random_bytes);
  VLOG(1) << "TPMUtilityImpl::Init success";
  is_initialized_ = true;
  return true;
}

bool TPMUtilityImpl::IsTPMAvailable() {
  if (is_enabled_ready_) {
    return is_enabled_;
  }
  // If the TPM works, clearly it's available.
  if (is_initialized_) {
    is_enabled_ready_ = true;
    is_enabled_ = true;
    return true;
  }
  // If the system says there is an enabled TPM, expect to use it.
  const base::FilePath kMiscEnabledFile("/sys/class/misc/tpm0/device/enabled");
  const base::FilePath kTpmEnabledFile("/sys/class/tpm/tpm0/device/enabled");
  string file_content;
  if ((base::ReadFileToString(kMiscEnabledFile, &file_content) ||
       base::ReadFileToString(kTpmEnabledFile, &file_content)) &&
      !file_content.empty() && file_content[0] == '1') {
    is_enabled_ = true;
  }
  is_enabled_ready_ = true;
  return is_enabled_;
}

TPMVersion TPMUtilityImpl::GetTPMVersion() {
  return TPMVersion::TPM1_2;
}

bool TPMUtilityImpl::InitSRK() {
  if (!is_initialized_)
    return false;
  if (is_srk_ready_)
    return true;
  VLOG(1) << "TPMUtilityImpl::InitSRK enter";
  // Load the SRK and assign it a usage policy with authorization data.
  TSS_RESULT result = TSS_SUCCESS;
  TSS_UUID uuid = TSS_UUID_SRK;
  result =
      Tspi_Context_LoadKeyByUUID(tsp_context_, TSS_PS_TYPE_SYSTEM, uuid, &srk_);
  if (result != TSS_SUCCESS) {
    if (result == (TSS_LAYER_TCS | TSS_E_PS_KEY_NOTFOUND)) {
      LOG(WARNING) << "SRK does not exist - this is normal when the TPM is not "
                   << "yet owned.";
    } else {
      LOG(ERROR) << "Tspi_Context_LoadKeyByUUID - " << ResultToString(result);
    }
    return false;
  }
  ScopedTssPolicy srk_policy(tsp_context_);
  result = Tspi_Context_CreateObject(tsp_context_, TSS_OBJECT_TYPE_POLICY,
                                     TSS_POLICY_USAGE, srk_policy.ptr());
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Context_CreateObject - " << ResultToString(result);
    return false;
  }
  if (srk_auth_data_.empty()) {
    result = Tspi_Policy_SetSecret(srk_policy, TSS_SECRET_MODE_PLAIN, 0, NULL);
  } else {
    LOG(INFO) << "Using non-empty secret for SRK policy.";
    // If the authorization data is 20 null bytes, use SHA1 mode for
    // compatibility with other tools that use this value.
    result = Tspi_Policy_SetSecret(
        srk_policy,
        srk_auth_data_ == string(20, 0) ? TSS_SECRET_MODE_SHA1
                                        : TSS_SECRET_MODE_PLAIN,
        srk_auth_data_.length(),
        ConvertStringToByteBuffer(srk_auth_data_.data()));
  }
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Policy_SetSecret - " << ResultToString(result);
    return false;
  }
  result = Tspi_Policy_AssignToObject(srk_policy.release(), srk_);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Policy_AssignToObject - " << ResultToString(result);
    return false;
  }
  VLOG(1) << "TPMUtilityImpl::InitSRK success";
  is_srk_ready_ = true;
  return true;
}

bool TPMUtilityImpl::Authenticate(int slot_id,
                                  const SecureBlob& auth_data,
                                  const string& auth_key_blob,
                                  const string& encrypted_master_key,
                                  SecureBlob* master_key) {
  VLOG(1) << "TPMUtilityImpl::Authenticate enter";
  int key_handle = 0;
  if (!LoadKey(slot_id, auth_key_blob, auth_data, &key_handle))
    return false;
  string master_key_str;
  if (!Unbind(key_handle, encrypted_master_key, &master_key_str))
    return false;
  *master_key = SecureBlob(master_key_str.begin(), master_key_str.end());
  ClearString(&master_key_str);
  VLOG(1) << "TPMUtilityImpl::Authenticate success";
  return true;
}

bool TPMUtilityImpl::ChangeAuthData(int slot_id,
                                    const SecureBlob& old_auth_data,
                                    const SecureBlob& new_auth_data,
                                    const string& old_auth_key_blob,
                                    string* new_auth_key_blob) {
  VLOG(1) << "TPMUtilityImpl::ChangeAuthData enter";
  int key_handle = 0;
  if (!LoadKey(slot_id, old_auth_key_blob, old_auth_data, &key_handle))
    return false;
  // Make sure the old auth data is ok.
  string encrypted, decrypted;
  if (!Bind(key_handle, "testdata", &encrypted))
    return false;
  if (!Unbind(key_handle, encrypted, &decrypted))
    return false;
  // Change the secret.
  AutoLock lock(lock_);
  TSS_RESULT result = TSS_SUCCESS;
  ScopedTssPolicy policy(tsp_context_);
  result = Tspi_Context_CreateObject(tsp_context_, TSS_OBJECT_TYPE_POLICY,
                                     TSS_POLICY_USAGE, policy.ptr());
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Context_CreateObject - " << ResultToString(result);
    return false;
  }
  result =
      Tspi_Policy_SetSecret(policy, TSS_SECRET_MODE_SHA1, new_auth_data.size(),
                            const_cast<BYTE*>(new_auth_data.data()));
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Policy_SetSecret - " << ResultToString(result);
    return false;
  }
  result = Tspi_ChangeAuth(GetTssHandle(key_handle), srk_, policy.release());
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_ChangeAuth - " << ResultToString(result);
    return false;
  }
  if (!GetKeyBlob(GetTssHandle(key_handle), new_auth_key_blob))
    return false;
  VLOG(1) << "TPMUtilityImpl::ChangeAuthData success";
  return true;
}

bool TPMUtilityImpl::GenerateRandom(int num_bytes, string* random_data) {
  VLOG(1) << "TPMUtilityImpl::GenerateRandom enter";
  AutoLock lock(lock_);
  if (!InitSRK())
    return false;
  TSS_RESULT result = TSS_SUCCESS;
  TSS_HTPM tpm;
  BYTE* random_bytes = NULL;
  result = Tspi_Context_GetTpmObject(tsp_context_, &tpm);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Context_GetTpmObject - " << ResultToString(result);
    return false;
  }
  result = Tspi_TPM_GetRandom(tpm, num_bytes, &random_bytes);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_TPM_GetRandom - " << ResultToString(result);
    return false;
  }
  *random_data = ConvertByteBufferToString(random_bytes, num_bytes);
  Tspi_Context_FreeMemory(tsp_context_, random_bytes);
  VLOG(1) << "TPMUtilityImpl::GenerateRandom success";
  return true;
}

bool TPMUtilityImpl::StirRandom(const string& entropy_data) {
  VLOG(1) << "TPMUtilityImpl::StirRandom enter";
  AutoLock lock(lock_);
  if (!InitSRK())
    return false;
  TSS_RESULT result = TSS_SUCCESS;
  TSS_HTPM tpm;
  result = Tspi_Context_GetTpmObject(tsp_context_, &tpm);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Context_GetTpmObject - " << ResultToString(result);
    return false;
  }
  result = Tspi_TPM_StirRandom(tpm, entropy_data.length(),
                               ConvertStringToByteBuffer(entropy_data.data()));
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_TPM_StirRandom - " << ResultToString(result);
    return false;
  }
  VLOG(1) << "TPMUtilityImpl::StirRandom success";
  return true;
}

bool TPMUtilityImpl::GenerateRSAKey(int slot,
                                    int modulus_bits,
                                    const string& public_exponent,
                                    const SecureBlob& auth_data,
                                    string* key_blob,
                                    int* key_handle) {
  VLOG(1) << "TPMUtilityImpl::GenerateRSAKey enter";
  AutoLock lock(lock_);
  if (!InitSRK())
    return false;
  TSS_RESULT result = TSS_SUCCESS;
  ScopedTssKey key(tsp_context_);
  result = Tspi_Context_CreateObject(tsp_context_, TSS_OBJECT_TYPE_RSAKEY,
                                     GetKeyFlags(modulus_bits), key.ptr());
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Context_CreateObject - " << ResultToString(result);
    return false;
  }
  if (public_exponent != default_exponent_) {
    LOG(WARNING) << "Non-Default Public Exponent: "
                 << PrintIntVector(ConvertByteStringToVector(public_exponent));
    result = Tspi_SetAttribData(
        key, TSS_TSPATTRIB_RSAKEY_INFO, TSS_TSPATTRIB_KEYINFO_RSA_EXPONENT,
        public_exponent.length(),
        ConvertStringToByteBuffer(public_exponent.data()));
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_SetAttribData(EXPONENT) - " << ResultToString(result);
      return false;
    }
  }
  if (!CreateKeyPolicy(key, auth_data, false))
    return false;
  result = Tspi_Key_CreateKey(key, srk_, 0);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Key_CreateKey - " << ResultToString(result);
    return false;
  }
  result = Tspi_Key_LoadKey(key, srk_);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Key_LoadKey - " << ResultToString(result);
    return false;
  }
  if (!GetKeyBlob(key, key_blob))
    return false;
  *key_handle = CreateHandle(slot, key.release(), *key_blob, auth_data);
  VLOG(1) << "TPMUtilityImpl::GenerateRSAKey success";
  return true;
}

bool TPMUtilityImpl::GetRSAPublicKey(int key_handle,
                                     string* public_exponent,
                                     string* modulus) {
  VLOG(1) << "TPMUtilityImpl::GetRSAPublicKey enter";
  AutoLock lock(lock_);
  if (!InitSRK())
    return false;
  if (!GetKeyAttributeData(GetTssHandle(key_handle), TSS_TSPATTRIB_RSAKEY_INFO,
                           TSS_TSPATTRIB_KEYINFO_RSA_EXPONENT, public_exponent))
    return false;
  if (!GetKeyAttributeData(GetTssHandle(key_handle), TSS_TSPATTRIB_RSAKEY_INFO,
                           TSS_TSPATTRIB_KEYINFO_RSA_MODULUS, modulus))
    return false;
  VLOG(1) << "TPMUtilityImpl::GetRSAPublicKey success";
  return true;
}

bool TPMUtilityImpl::IsECCurveSupported(int curve_nid) {
  return false;
}

bool TPMUtilityImpl::GenerateECCKey(int slot,
                                    int nid,
                                    const brillo::SecureBlob& auth_data,
                                    std::string* key_blob,
                                    int* key_handle) {
  LOG(ERROR) << __func__ << "TPM 1.2 doesn't support ECC.";
  return false;
}

bool TPMUtilityImpl::GetECCPublicKey(int key_handle,
                                     std::string* public_point) {
  LOG(ERROR) << __func__ << "TPM 1.2 doesn't support ECC.";
  return false;
}

bool TPMUtilityImpl::WrapRSAKey(int slot,
                                const string& public_exponent,
                                const string& modulus,
                                const string& prime_factor,
                                const SecureBlob& auth_data,
                                string* key_blob,
                                int* key_handle) {
  VLOG(1) << "TPMUtilityImpl::WrapRSAKey enter";
  AutoLock lock(lock_);
  if (!InitSRK())
    return false;
  if (!GetSRKPublicKey())
    return false;
  ScopedTssKey key(tsp_context_);
  TSS_RESULT result =
      Tspi_Context_CreateObject(tsp_context_, TSS_OBJECT_TYPE_RSAKEY,
                                GetKeyFlags(modulus.length() * 8), key.ptr());
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Context_CreateObject - " << ResultToString(result);
    return false;
  }
  if (public_exponent != default_exponent_) {
    LOG(WARNING) << "Non-Default Public Exponent: "
                 << PrintIntVector(ConvertByteStringToVector(public_exponent));
    result = Tspi_SetAttribData(
        key, TSS_TSPATTRIB_RSAKEY_INFO, TSS_TSPATTRIB_KEYINFO_RSA_EXPONENT,
        public_exponent.length(),
        ConvertStringToByteBuffer(public_exponent.data()));
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_SetAttribData(EXPONENT) - " << ResultToString(result);
      return false;
    }
  }
  result = Tspi_SetAttribData(
      key, TSS_TSPATTRIB_RSAKEY_INFO, TSS_TSPATTRIB_KEYINFO_RSA_MODULUS,
      modulus.length(), ConvertStringToByteBuffer(modulus.data()));
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_SetAttribData(MODULUS) - " << ResultToString(result);
    return false;
  }
  // The private parameter here is one of the prime factors (p or q). The reason
  // is that they are half the size of the modulus and from one factor (and the
  // modulus) the entire private key can be derived. The small size allows the
  // key to be wrapped in a single operation by another key of the same size.
  // See TPM_STORE_ASYMKEY in TPM Main Part 2 v1.2 r116 section 10.6 page 92.
  result = Tspi_SetAttribData(
      key, TSS_TSPATTRIB_KEY_BLOB, TSS_TSPATTRIB_KEYBLOB_PRIVATE_KEY,
      prime_factor.length(), ConvertStringToByteBuffer(prime_factor.data()));
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_SetAttribData(FACTOR) - " << ResultToString(result);
    return false;
  }
  if (!CreateKeyPolicy(key, auth_data, false))
    return false;
  result = Tspi_Key_WrapKey(key, srk_, 0);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Key_WrapKey - " << ResultToString(result);
    return false;
  }
  result = Tspi_Key_LoadKey(key, srk_);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Key_LoadKey - " << ResultToString(result);
    return false;
  }
  if (!GetKeyBlob(key, key_blob))
    return false;
  *key_handle = CreateHandle(slot, key.release(), *key_blob, auth_data);
  VLOG(1) << "TPMUtilityImpl::WrapRSAKey success";
  return true;
}

bool TPMUtilityImpl::WrapECCKey(int slot,
                                int curve_nid,
                                const std::string& public_point_x,
                                const std::string& public_point_y,
                                const std::string& private_value,
                                const brillo::SecureBlob& auth_data,
                                std::string* key_blob,
                                int* key_handle) {
  LOG(ERROR) << __func__ << "TPM 1.2 doesn't support ECC.";
  return false;
}

bool TPMUtilityImpl::LoadKey(int slot,
                             const string& key_blob,
                             const SecureBlob& auth_data,
                             int* key_handle) {
  // Use the SRK as the parent. This is the normal case.
  return LoadKeyWithParent(slot, key_blob, auth_data, srk_, key_handle);
}

bool TPMUtilityImpl::LoadKeyWithParent(int slot,
                                       const string& key_blob,
                                       const SecureBlob& auth_data,
                                       int parent_key_handle,
                                       int* key_handle) {
  AutoLock lock(lock_);
  if (!InitSRK())
    return false;
  if (IsAlreadyLoaded(slot, key_blob, key_handle))
    return true;
  VLOG(1) << "TPMUtilityImpl::LoadKeyWithParent enter";
  ScopedTssKey key(tsp_context_);
  if (!LoadKeyInternal(GetTssHandle(parent_key_handle), key_blob, auth_data,
                       key.ptr()))
    return false;
  *key_handle = CreateHandle(slot, key.release(), key_blob, auth_data);
  VLOG(1) << "TPMUtilityImpl::LoadKeyWithParent success";
  return true;
}

void TPMUtilityImpl::UnloadKeysForSlot(int slot) {
  VLOG(1) << "TPMUtilityImpl::UnloadKeysForSlot enter";
  AutoLock lock(lock_);
  if (!InitSRK())
    return;
  set<int>* handles = &slot_handles_[slot].handles_;
  set<int>::iterator it;
  for (it = handles->begin(); it != handles->end(); ++it) {
    Tspi_Key_UnloadKey(GetTssHandle(*it));
    Tspi_Context_CloseObject(tsp_context_, GetTssHandle(*it));
  }
  slot_handles_.erase(slot);
  LOG(INFO) << "Unloaded keys for slot " << slot;
  VLOG(1) << "TPMUtilityImpl::UnloadKeysForSlot success";
}

bool TPMUtilityImpl::Bind(int key_handle, const string& input, string* output) {
  VLOG(1) << "TPMUtilityImpl::Bind enter";
  AutoLock lock(lock_);
  if (!InitSRK())
    return false;
  TSSEncryptedData encrypted(tsp_context_);
  if (!encrypted.Create())
    return false;
  TSS_RESULT result =
      Tspi_Data_Bind(encrypted, GetTssHandle(key_handle), input.length(),
                     ConvertStringToByteBuffer(input.data()));
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Data_Bind - " << ResultToString(result);
    return false;
  }
  if (!encrypted.GetData(output))
    return false;
  VLOG(1) << "TPMUtilityImpl::Bind success";
  return true;
}

bool TPMUtilityImpl::Unbind(int key_handle,
                            const string& input,
                            string* output) {
  VLOG(1) << "TPMUtilityImpl::Unbind enter";
  AutoLock lock(lock_);
  if (!InitSRK())
    return false;
  TSSEncryptedData encrypted(tsp_context_);
  if (!encrypted.Create())
    return false;
  if (!encrypted.SetData(input))
    return false;
  UINT32 length = 0;
  BYTE* buffer = NULL;
  TSS_RESULT result =
      Tspi_Data_Unbind(encrypted, GetTssHandle(key_handle), &length, &buffer);
  if (result == (TSS_LAYER_TCS | TCS_E_KM_LOADFAILED)) {
    // On some TPMs, the TCS layer will fail to reload a key that has been
    // evicted. If this occurs, we can attempt to reload the key manually and
    // then try the operation again.
    LOG(WARNING) << "TCS load failure: attempting to reload key.";
    if (!ReloadKey(key_handle))
      return false;
    result =
        Tspi_Data_Unbind(encrypted, GetTssHandle(key_handle), &length, &buffer);
  }
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Data_Unbind - " << ResultToString(result);
    return false;
  }
  *output = ConvertByteBufferToString(buffer, length);
  Tspi_Context_FreeMemory(tsp_context_, buffer);
  VLOG(1) << "TPMUtilityImpl::Unbind success";
  return true;
}

bool TPMUtilityImpl::Sign(int key_handle,
                          CK_MECHANISM_TYPE signing_mechanism,
                          const std::string& mechanism_parameter,
                          const string& input,
                          string* signature) {
  VLOG(1) << "TPMUtilityImpl::Sign enter";
  AutoLock lock(lock_);
  DigestAlgorithm digest_algorithm = GetDigestAlgorithm(signing_mechanism);

  // Using the TSS_SS_RSASSAPKCS1V15_DER scheme, we need to manually
  // insert the hash OID.
  std::string data_to_sign =
      GetDigestAlgorithmEncoding(digest_algorithm) + input;
  if (!InitSRK())
    return false;
  TSSHash hash(tsp_context_);
  if (!hash.Create(data_to_sign))
    return false;

  // Tspi_Hash_Sign only do PKCS#1 v1.5, and only the mechanisms below match.
  if (GetSigningSchemeForMechanism(signing_mechanism) !=
      RsaPaddingScheme::RSASSA_PKCS1_V1_5) {
    LOG(ERROR) << "Unsupported mechanism for tpm1.2 key " << signing_mechanism;
    return false;
  }

  UINT32 length = 0;
  BYTE* buffer = NULL;
  TSS_RESULT result =
      Tspi_Hash_Sign(hash, GetTssHandle(key_handle), &length, &buffer);
  if (result == (TSS_LAYER_TCS | TCS_E_KM_LOADFAILED)) {
    // On some TPMs, the TCS layer will fail to reload a key that has been
    // evicted. If this occurs, we can attempt to reload the key manually and
    // then try the operation again.
    LOG(WARNING) << "TCS load failure: attempting to reload key.";
    if (!ReloadKey(key_handle))
      return false;
    result = Tspi_Hash_Sign(hash, GetTssHandle(key_handle), &length, &buffer);
  }
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Hash_Sign - " << ResultToString(result);
    return false;
  }
  *signature = ConvertByteBufferToString(buffer, length);
  Tspi_Context_FreeMemory(tsp_context_, buffer);
  VLOG(1) << "TPMUtilityImpl::Sign success";
  return true;
}

bool TPMUtilityImpl::IsSRKReady() {
  VLOG(1) << "TPMUtilityImpl::IsSRKReady";
  AutoLock lock(lock_);
  return InitSRK();
}

int TPMUtilityImpl::CreateHandle(int slot,
                                 TSS_HKEY key,
                                 const string& key_blob,
                                 const SecureBlob& auth_data) {
  int handle = ++last_handle_;
  HandleInfo* handle_info = &slot_handles_[slot];
  handle_info->handles_.insert(handle);
  handle_info->blob_handle_[key_blob] = handle;
  KeyInfo* key_info = &handle_info_[handle];
  key_info->tss_handle = key;
  key_info->blob = key_blob;
  key_info->auth_data = auth_data;
  return handle;
}

bool TPMUtilityImpl::CreateKeyPolicy(TSS_HKEY key,
                                     const SecureBlob& auth_data,
                                     bool auth_only) {
  ScopedTssPolicy policy(tsp_context_);
  TSS_RESULT result = Tspi_Context_CreateObject(
      tsp_context_, TSS_OBJECT_TYPE_POLICY, TSS_POLICY_USAGE, policy.ptr());
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Context_CreateObject - " << ResultToString(result);
    return false;
  }
  if (auth_data.empty()) {
    result = Tspi_Policy_SetSecret(policy, TSS_SECRET_MODE_NONE, 0, NULL);
  } else {
    result =
        Tspi_Policy_SetSecret(policy, TSS_SECRET_MODE_SHA1, auth_data.size(),
                              const_cast<BYTE*>(auth_data.data()));
  }
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Policy_SetSecret - " << ResultToString(result);
    return false;
  }
  if (!auth_only) {
    result = Tspi_SetAttribUint32(key, TSS_TSPATTRIB_KEY_INFO,
                                  TSS_TSPATTRIB_KEYINFO_ENCSCHEME,
                                  TSS_ES_RSAESPKCSV15);
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_SetAttribUint(ENCSCHEME) - "
                 << ResultToString(result);
      return false;
    }
    result = Tspi_SetAttribUint32(key, TSS_TSPATTRIB_KEY_INFO,
                                  TSS_TSPATTRIB_KEYINFO_SIGSCHEME,
                                  TSS_SS_RSASSAPKCS1V15_DER);
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_SetAttribUint(SIGSCHEME) - "
                 << ResultToString(result);
      return false;
    }
    ScopedTssPolicy migration_policy(tsp_context_);
    result =
        Tspi_Context_CreateObject(tsp_context_, TSS_OBJECT_TYPE_POLICY,
                                  TSS_POLICY_MIGRATION, migration_policy.ptr());
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_SetAttribUint(SIGSCHEME) - "
                 << ResultToString(result);
      return false;
    }
    // We need to set a migration policy but we don't want the key to be
    // migratable. We'll set random authorization data and then discard it.
    const int kSha1OutputBytes = 20;
    unsigned char discard[kSha1OutputBytes];
    RAND_bytes(discard, kSha1OutputBytes);
    result = Tspi_Policy_SetSecret(migration_policy, TSS_SECRET_MODE_SHA1,
                                   kSha1OutputBytes, discard);
    brillo::SecureClear(discard, kSha1OutputBytes);
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_Policy_SetSecret - " << ResultToString(result);
      return false;
    }
    result = Tspi_Policy_AssignToObject(migration_policy.release(), key);
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_Policy_AssignToObject - " << ResultToString(result);
      return false;
    }
  }
  result = Tspi_Policy_AssignToObject(policy.release(), key);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Policy_AssignToObject - " << ResultToString(result);
    return false;
  }
  return true;
}

bool TPMUtilityImpl::GetKeyAttributeData(TSS_HKEY key,
                                         TSS_FLAG flag,
                                         TSS_FLAG sub_flag,
                                         string* data) {
  UINT32 length = 0;
  BYTE* buffer = NULL;
  TSS_RESULT result = Tspi_GetAttribData(key, flag, sub_flag, &length, &buffer);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_GetAttribData(" << flag << ", " << sub_flag << ") - "
               << ResultToString(result);
    return false;
  }
  *data = ConvertByteBufferToString(buffer, length);
  Tspi_Context_FreeMemory(tsp_context_, buffer);
  return true;
}

bool TPMUtilityImpl::GetKeyBlob(TSS_HKEY key, string* blob) {
  return GetKeyAttributeData(key, TSS_TSPATTRIB_KEY_BLOB,
                             TSS_TSPATTRIB_KEYBLOB_BLOB, blob);
}

TSS_FLAG TPMUtilityImpl::GetKeyFlags(int modulus_bits) {
  // We want the keys we create / wrap to be capable of signing and binding.
  // This means we need to use the 'legacy' key type. Keys of this type are
  // migratable by definition. See TCG Architecture Overview 1.4: 4.2.7.2.
  TSS_FLAG flags =
      TSS_KEY_TYPE_LEGACY | TSS_KEY_AUTHORIZATION | TSS_KEY_MIGRATABLE;
  if (modulus_bits == TSS_KEY_SIZEVAL_512BIT) {
    flags |= TSS_KEY_SIZE_512;
  } else if (modulus_bits == TSS_KEY_SIZEVAL_1024BIT) {
    flags |= TSS_KEY_SIZE_1024;
  } else if (modulus_bits == TSS_KEY_SIZEVAL_2048BIT) {
    flags |= TSS_KEY_SIZE_2048;
  } else if (modulus_bits == TSS_KEY_SIZEVAL_4096BIT) {
    flags |= TSS_KEY_SIZE_4096;
  } else if (modulus_bits == TSS_KEY_SIZEVAL_8192BIT) {
    flags |= TSS_KEY_SIZE_8192;
  } else if (modulus_bits == TSS_KEY_SIZEVAL_16384BIT) {
    flags |= TSS_KEY_SIZE_16384;
  } else {
    flags |= TSS_KEY_SIZE_DEFAULT;
  }
  return flags;
}

bool TPMUtilityImpl::GetSRKPublicKey() {
  // In order to wrap a key with the SRK we need access to the SRK public key
  // and we need to get it manually. Once it's in the key object, we don't need
  // to do this again.
  if (!srk_public_loaded_) {
    UINT32 length = 0;
    BYTE* buffer = NULL;
    TSS_RESULT result = Tspi_Key_GetPubKey(srk_, &length, &buffer);
    if (result != TSS_SUCCESS) {
      if (result == TPM_E_INVALID_KEYHANDLE) {
        LOG(ERROR) << "The TPM is not configured to allow reading the public "
                   << "SRK. Use 'tpm_restrictsrk -a' to allow this.";
      } else {
        LOG(ERROR) << "Tspi_Key_GetPubKey - " << ResultToString(result);
      }
      return false;
    }
    Tspi_Context_FreeMemory(tsp_context_, buffer);
    srk_public_loaded_ = true;
  }
  return true;
}

TSS_HKEY TPMUtilityImpl::GetTssHandle(int key_handle) {
  if (static_cast<TSS_HKEY>(key_handle) == srk_)
    return srk_;
  map<int, KeyInfo>::iterator it = handle_info_.find(key_handle);
  if (it == handle_info_.end())
    return 0;
  return it->second.tss_handle;
}

bool TPMUtilityImpl::IsAlreadyLoaded(int slot,
                                     const string& key_blob,
                                     int* key_handle) {
  HandleInfo* handle_info = &slot_handles_[slot];
  map<string, int>::iterator it = handle_info->blob_handle_.find(key_blob);
  if (it == handle_info->blob_handle_.end())
    return false;
  *key_handle = it->second;
  return true;
}

bool TPMUtilityImpl::LoadKeyInternal(TSS_HKEY parent,
                                     const string& key_blob,
                                     const SecureBlob& auth_data,
                                     TSS_HKEY* key) {
  TSS_RESULT result = Tspi_Context_LoadKeyByBlob(
      tsp_context_, parent, key_blob.length(),
      ConvertStringToByteBuffer(key_blob.data()), key);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_Context_LoadKeyByBlob - " << ResultToString(result);
    return false;
  }
  TSS_HPOLICY policy;
  result = Tspi_GetPolicyObject(*key, TSS_POLICY_USAGE, &policy);
  if (result != TSS_SUCCESS) {
    LOG(ERROR) << "Tspi_GetPolicyObject - " << ResultToString(result);
    return false;
  }
  if (policy == default_policy_) {
    if (!CreateKeyPolicy(*key, auth_data, true))
      return false;
  } else if (auth_data.empty()) {
    result = Tspi_Policy_SetSecret(policy, TSS_SECRET_MODE_NONE, 0, NULL);
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_Policy_SetSecret - " << ResultToString(result);
      return false;
    }
  } else {
    result =
        Tspi_Policy_SetSecret(policy, TSS_SECRET_MODE_SHA1, auth_data.size(),
                              const_cast<BYTE*>(auth_data.data()));
    if (result != TSS_SUCCESS) {
      LOG(ERROR) << "Tspi_Policy_SetSecret - " << ResultToString(result);
      return false;
    }
  }
  return true;
}

bool TPMUtilityImpl::ReloadKey(int key_handle) {
  KeyInfo* key_info = &handle_info_[key_handle];
  // Unload the current handle.
  Tspi_Key_UnloadKey(key_info->tss_handle);
  Tspi_Context_CloseObject(tsp_context_, key_info->tss_handle);
  key_info->tss_handle = 0;
  // Load the same key blob again.
  ScopedTssKey scoped_key(tsp_context_);
  if (!LoadKeyInternal(srk_, key_info->blob, key_info->auth_data,
                       scoped_key.ptr())) {
    LOG(ERROR) << "Failed to reload key.";
    return false;
  }
  key_info->tss_handle = scoped_key.release();
  return true;
}

string TPMUtilityImpl::ResultToString(TSS_RESULT result) {
  if (result == TSS_SUCCESS)
    return "TSS_SUCCESS";
  TSS_RESULT layer = ERROR_LAYER(result);
  TSS_RESULT code = ERROR_CODE(result);
  if (layer == TSS_LAYER_TPM) {
    switch (code) {
      case TPM_E_AUTHFAIL:
        return "TPM_E_AUTHFAIL";
      case TPM_E_BADINDEX:
        return "TPM_E_BADINDEX";
      case TPM_E_BAD_PARAMETER:
        return "TPM_E_BAD_PARAMETER";
      case TPM_E_AUDITFAILURE:
        return "TPM_E_AUDITFAILURE";
      case TPM_E_CLEAR_DISABLED:
        return "TPM_E_CLEAR_DISABLED";
      case TPM_E_DEACTIVATED:
        return "TPM_E_DEACTIVATED";
      case TPM_E_DISABLED:
        return "TPM_E_DISABLED";
      case TPM_E_DISABLED_CMD:
        return "TPM_E_DISABLED_CMD";
      case TPM_E_FAIL:
        return "TPM_E_FAIL";
      case TPM_E_BAD_ORDINAL:
        return "TPM_E_BAD_ORDINAL";
      case TPM_E_INSTALL_DISABLED:
        return "TPM_E_INSTALL_DISABLED";
      case TPM_E_INVALID_KEYHANDLE:
        return "TPM_E_INVALID_KEYHANDLE";
      case TPM_E_KEYNOTFOUND:
        return "TPM_E_KEYNOTFOUND";
      case TPM_E_INAPPROPRIATE_ENC:
        return "TPM_E_INAPPROPRIATE_ENC";
      case TPM_E_MIGRATEFAIL:
        return "TPM_E_MIGRATEFAIL";
      case TPM_E_INVALID_PCR_INFO:
        return "TPM_E_INVALID_PCR_INFO";
      case TPM_E_NOSPACE:
        return "TPM_E_NOSPACE";
      case TPM_E_NOSRK:
        return "TPM_E_NOSRK";
      case TPM_E_NOTSEALED_BLOB:
        return "TPM_E_NOTSEALED_BLOB";
      case TPM_E_OWNER_SET:
        return "TPM_E_OWNER_SET";
      case TPM_E_RESOURCES:
        return "TPM_E_RESOURCES";
      case TPM_E_SHORTRANDOM:
        return "TPM_E_SHORTRANDOM";
      case TPM_E_SIZE:
        return "TPM_E_SIZE";
      case TPM_E_WRONGPCRVAL:
        return "TPM_E_WRONGPCRVAL";
      case TPM_E_BAD_PARAM_SIZE:
        return "TPM_E_BAD_PARAM_SIZE";
      case TPM_E_SHA_THREAD:
        return "TPM_E_SHA_THREAD";
      case TPM_E_SHA_ERROR:
        return "TPM_E_SHA_ERROR";
      case TPM_E_FAILEDSELFTEST:
        return "TPM_E_FAILEDSELFTEST";
      case TPM_E_AUTH2FAIL:
        return "TPM_E_AUTH2FAIL";
      case TPM_E_BADTAG:
        return "TPM_E_BADTAG";
      case TPM_E_IOERROR:
        return "TPM_E_IOERROR";
      case TPM_E_ENCRYPT_ERROR:
        return "TPM_E_ENCRYPT_ERROR";
      case TPM_E_DECRYPT_ERROR:
        return "TPM_E_DECRYPT_ERROR";
      case TPM_E_INVALID_AUTHHANDLE:
        return "TPM_E_INVALID_AUTHHANDLE";
      case TPM_E_NO_ENDORSEMENT:
        return "TPM_E_NO_ENDORSEMENT";
      case TPM_E_INVALID_KEYUSAGE:
        return "TPM_E_INVALID_KEYUSAGE";
      case TPM_E_WRONG_ENTITYTYPE:
        return "TPM_E_WRONG_ENTITYTYPE";
      case TPM_E_INVALID_POSTINIT:
        return "TPM_E_INVALID_POSTINIT";
      case TPM_E_INAPPROPRIATE_SIG:
        return "TPM_E_INAPPROPRIATE_SIG";
      case TPM_E_BAD_KEY_PROPERTY:
        return "TPM_E_BAD_KEY_PROPERTY";
      case TPM_E_BAD_MIGRATION:
        return "TPM_E_BAD_MIGRATION";
      case TPM_E_BAD_SCHEME:
        return "TPM_E_BAD_SCHEME";
      case TPM_E_BAD_DATASIZE:
        return "TPM_E_BAD_DATASIZE";
      case TPM_E_BAD_MODE:
        return "TPM_E_BAD_MODE";
      case TPM_E_BAD_PRESENCE:
        return "TPM_E_BAD_PRESENCE";
      case TPM_E_BAD_VERSION:
        return "TPM_E_BAD_VERSION";
      case TPM_E_NO_WRAP_TRANSPORT:
        return "TPM_E_NO_WRAP_TRANSPORT";
      case TPM_E_AUDITFAIL_UNSUCCESSFUL:
        return "TPM_E_AUDITFAIL_UNSUCCESSFUL";
      case TPM_E_AUDITFAIL_SUCCESSFUL:
        return "TPM_E_AUDITFAIL_SUCCESSFUL";
      case TPM_E_NOTRESETABLE:
        return "TPM_E_NOTRESETABLE";
      case TPM_E_NOTLOCAL:
        return "TPM_E_NOTLOCAL";
      case TPM_E_BAD_TYPE:
        return "TPM_E_BAD_TYPE";
      case TPM_E_INVALID_RESOURCE:
        return "TPM_E_INVALID_RESOURCE";
      case TPM_E_NOTFIPS:
        return "TPM_E_NOTFIPS";
      case TPM_E_INVALID_FAMILY:
        return "TPM_E_INVALID_FAMILY";
      case TPM_E_NO_NV_PERMISSION:
        return "TPM_E_NO_NV_PERMISSION";
      case TPM_E_REQUIRES_SIGN:
        return "TPM_E_REQUIRES_SIGN";
      case TPM_E_KEY_NOTSUPPORTED:
        return "TPM_E_KEY_NOTSUPPORTED";
      case TPM_E_AUTH_CONFLICT:
        return "TPM_E_AUTH_CONFLICT";
      case TPM_E_AREA_LOCKED:
        return "TPM_E_AREA_LOCKED";
      case TPM_E_BAD_LOCALITY:
        return "TPM_E_BAD_LOCALITY";
      case TPM_E_READ_ONLY:
        return "TPM_E_READ_ONLY";
      case TPM_E_PER_NOWRITE:
        return "TPM_E_PER_NOWRITE";
      case TPM_E_FAMILYCOUNT:
        return "TPM_E_FAMILYCOUNT";
      case TPM_E_WRITE_LOCKED:
        return "TPM_E_WRITE_LOCKED";
      case TPM_E_BAD_ATTRIBUTES:
        return "TPM_E_BAD_ATTRIBUTES";
      case TPM_E_INVALID_STRUCTURE:
        return "TPM_E_INVALID_STRUCTURE";
      case TPM_E_KEY_OWNER_CONTROL:
        return "TPM_E_KEY_OWNER_CONTROL";
      case TPM_E_BAD_COUNTER:
        return "TPM_E_BAD_COUNTER";
      case TPM_E_NOT_FULLWRITE:
        return "TPM_E_NOT_FULLWRITE";
      case TPM_E_CONTEXT_GAP:
        return "TPM_E_CONTEXT_GAP";
      case TPM_E_MAXNVWRITES:
        return "TPM_E_MAXNVWRITES";
      case TPM_E_NOOPERATOR:
        return "TPM_E_NOOPERATOR";
      case TPM_E_RESOURCEMISSING:
        return "TPM_E_RESOURCEMISSING";
      case TPM_E_DELEGATE_LOCK:
        return "TPM_E_DELEGATE_LOCK";
      case TPM_E_DELEGATE_FAMILY:
        return "TPM_E_DELEGATE_FAMILY";
      case TPM_E_DELEGATE_ADMIN:
        return "TPM_E_DELEGATE_ADMIN";
      case TPM_E_TRANSPORT_NOTEXCLUSIVE:
        return "TPM_E_TRANSPORT_NOTEXCLUSIVE";
      case TPM_E_OWNER_CONTROL:
        return "TPM_E_OWNER_CONTROL";
      case TPM_E_DAA_RESOURCES:
        return "TPM_E_DAA_RESOURCES";
      case TPM_E_DAA_INPUT_DATA0:
        return "TPM_E_DAA_INPUT_DATA0";
      case TPM_E_DAA_INPUT_DATA1:
        return "TPM_E_DAA_INPUT_DATA1";
      case TPM_E_DAA_ISSUER_SETTINGS:
        return "TPM_E_DAA_ISSUER_SETTINGS";
      case TPM_E_DAA_TPM_SETTINGS:
        return "TPM_E_DAA_TPM_SETTINGS";
      case TPM_E_DAA_STAGE:
        return "TPM_E_DAA_STAGE";
      case TPM_E_DAA_ISSUER_VALIDITY:
        return "TPM_E_DAA_ISSUER_VALIDITY";
      case TPM_E_DAA_WRONG_W:
        return "TPM_E_DAA_WRONG_W";
      case TPM_E_BAD_HANDLE:
        return "TPM_E_BAD_HANDLE";
      case TPM_E_BAD_DELEGATE:
        return "TPM_E_BAD_DELEGATE";
      case TPM_E_BADCONTEXT:
        return "TPM_E_BADCONTEXT";
      case TPM_E_TOOMANYCONTEXTS:
        return "TPM_E_TOOMANYCONTEXTS";
      case TPM_E_MA_TICKET_SIGNATURE:
        return "TPM_E_MA_TICKET_SIGNATURE";
      case TPM_E_MA_DESTINATION:
        return "TPM_E_MA_DESTINATION";
      case TPM_E_MA_SOURCE:
        return "TPM_E_MA_SOURCE";
      case TPM_E_MA_AUTHORITY:
        return "TPM_E_MA_AUTHORITY";
      case TPM_E_PERMANENTEK:
        return "TPM_E_PERMANENTEK";
      case TPM_E_BAD_SIGNATURE:
        return "TPM_E_BAD_SIGNATURE";
      case TPM_E_NOCONTEXTSPACE:
        return "TPM_E_NOCONTEXTSPACE";
      case TPM_E_RETRY:
        return "TPM_E_RETRY";
      case TPM_E_NEEDS_SELFTEST:
        return "TPM_E_NEEDS_SELFTEST";
      case TPM_E_DOING_SELFTEST:
        return "TPM_E_DOING_SELFTEST";
      case TPM_E_DEFEND_LOCK_RUNNING:
        return "TPM_E_DEFEND_LOCK_RUNNING";
    }
  } else if (layer == TSS_LAYER_TDDL) {
    switch (code) {
      case TDDL_E_FAIL:
        return "TDDL_E_FAIL";
      case TDDL_E_TIMEOUT:
        return "TDDL_E_TIMEOUT";
      case TDDL_E_ALREADY_OPENED:
        return "TDDL_E_ALREADY_OPENED";
      case TDDL_E_ALREADY_CLOSED:
        return "TDDL_E_ALREADY_CLOSED";
      case TDDL_E_INSUFFICIENT_BUFFER:
        return "TDDL_E_INSUFFICIENT_BUFFER";
      case TDDL_E_COMMAND_COMPLETED:
        return "TDDL_E_COMMAND_COMPLETED";
      case TDDL_E_COMMAND_ABORTED:
        return "TDDL_E_COMMAND_ABORTED";
      case TDDL_E_IOERROR:
        return "TDDL_E_IOERROR";
      case TDDL_E_BADTAG:
        return "TDDL_E_BADTAG";
      case TDDL_E_COMPONENT_NOT_FOUND:
        return "TDDL_E_COMPONENT_NOT_FOUND";
    }
  } else if (layer == TSS_LAYER_TCS) {
    switch (code) {
      case TCS_E_INVALID_CONTEXTHANDLE:
        return "TCS_E_INVALID_CONTEXTHANDLE";
      case TCS_E_INVALID_KEYHANDLE:
        return "TCS_E_INVALID_KEYHANDLE";
      case TCS_E_INVALID_AUTHHANDLE:
        return "TCS_E_INVALID_AUTHHANDLE";
      case TCS_E_INVALID_AUTHSESSION:
        return "TCS_E_INVALID_AUTHSESSION";
      case TCS_E_INVALID_KEY:
        return "TCS_E_INVALID_KEY";
      case TCS_E_KEY_MISMATCH:
        return "TCS_E_KEY_MISMATCH";
      case TCS_E_KM_LOADFAILED:
        return "TCS_E_KM_LOADFAILED";
      case TCS_E_KEY_CONTEXT_RELOAD:
        return "TCS_E_KEY_CONTEXT_RELOAD";
      case TCS_E_BAD_INDEX:
        return "TCS_E_BAD_INDEX";
      case TCS_E_KEY_ALREADY_REGISTERED:
        return "TCS_E_KEY_ALREADY_REGISTERED";
      case TCS_E_BAD_PARAMETER:
        return "TCS_E_BAD_PARAMETER";
      case TCS_E_OUTOFMEMORY:
        return "TCS_E_OUTOFMEMORY";
      case TCS_E_NOTIMPL:
        return "TCS_E_NOTIMPL";
      case TCS_E_INTERNAL_ERROR:
        return "TCS_E_INTERNAL_ERROR";
    }
  } else if (layer == TSS_LAYER_TSP) {
    switch (code) {
      case TSS_E_FAIL:
        return "TSS_E_FAIL";
      case TSS_E_BAD_PARAMETER:
        return "TSS_E_BAD_PARAMETER";
      case TSS_E_INTERNAL_ERROR:
        return "TSS_E_INTERNAL_ERROR";
      case TSS_E_OUTOFMEMORY:
        return "TSS_E_OUTOFMEMORY";
      case TSS_E_NOTIMPL:
        return "TSS_E_NOTIMPL";
      case TSS_E_KEY_ALREADY_REGISTERED:
        return "TSS_E_KEY_ALREADY_REGISTERED";
      case TSS_E_TPM_UNEXPECTED:
        return "TSS_E_TPM_UNEXPECTED";
      case TSS_E_COMM_FAILURE:
        return "TSS_E_COMM_FAILURE";
      case TSS_E_TIMEOUT:
        return "TSS_E_TIMEOUT";
      case TSS_E_TPM_UNSUPPORTED_FEATURE:
        return "TSS_E_TPM_UNSUPPORTED_FEATURE";
      case TSS_E_CANCELED:
        return "TSS_E_CANCELED";
      case TSS_E_PS_KEY_NOTFOUND:
        return "TSS_E_PS_KEY_NOTFOUND";
      case TSS_E_PS_KEY_EXISTS:
        return "TSS_E_PS_KEY_EXISTS";
      case TSS_E_PS_BAD_KEY_STATE:
        return "TSS_E_PS_BAD_KEY_STATE";
      case TSS_E_INVALID_OBJECT_TYPE:
        return "TSS_E_INVALID_OBJECT_TYPE";
      case TSS_E_NO_CONNECTION:
        return "TSS_E_NO_CONNECTION";
      case TSS_E_CONNECTION_FAILED:
        return "TSS_E_CONNECTION_FAILED";
      case TSS_E_CONNECTION_BROKEN:
        return "TSS_E_CONNECTION_BROKEN";
      case TSS_E_HASH_INVALID_ALG:
        return "TSS_E_HASH_INVALID_ALG";
      case TSS_E_HASH_INVALID_LENGTH:
        return "TSS_E_HASH_INVALID_LENGTH";
      case TSS_E_HASH_NO_DATA:
        return "TSS_E_HASH_NO_DATA";
      case TSS_E_INVALID_ATTRIB_FLAG:
        return "TSS_E_INVALID_ATTRIB_FLAG";
      case TSS_E_INVALID_ATTRIB_SUBFLAG:
        return "TSS_E_INVALID_ATTRIB_SUBFLAG";
      case TSS_E_INVALID_ATTRIB_DATA:
        return "TSS_E_INVALID_ATTRIB_DATA";
      case TSS_E_INVALID_OBJECT_INITFLAG:
        return "TSS_E_INVALID_OBJECT_INITFLAG";
      case TSS_E_NO_PCRS_SET:
        return "TSS_E_NO_PCRS_SET";
      case TSS_E_KEY_NOT_LOADED:
        return "TSS_E_KEY_NOT_LOADED";
      case TSS_E_KEY_NOT_SET:
        return "TSS_E_KEY_NOT_SET";
      case TSS_E_VALIDATION_FAILED:
        return "TSS_E_VALIDATION_FAILED";
      case TSS_E_TSP_AUTHREQUIRED:
        return "TSS_E_TSP_AUTHREQUIRED";
      case TSS_E_TSP_AUTH2REQUIRED:
        return "TSS_E_TSP_AUTH2REQUIRED";
      case TSS_E_TSP_AUTHFAIL:
        return "TSS_E_TSP_AUTHFAIL";
      case TSS_E_TSP_AUTH2FAIL:
        return "TSS_E_TSP_AUTH2FAIL";
      case TSS_E_KEY_NO_MIGRATION_POLICY:
        return "TSS_E_KEY_NO_MIGRATION_POLICY";
      case TSS_E_POLICY_NO_SECRET:
        return "TSS_E_POLICY_NO_SECRET";
      case TSS_E_INVALID_OBJ_ACCESS:
        return "TSS_E_INVALID_OBJ_ACCESS";
      case TSS_E_INVALID_ENCSCHEME:
        return "TSS_E_INVALID_ENCSCHEME";
      case TSS_E_INVALID_SIGSCHEME:
        return "TSS_E_INVALID_SIGSCHEME";
      case TSS_E_ENC_INVALID_LENGTH:
        return "TSS_E_ENC_INVALID_LENGTH";
      case TSS_E_ENC_NO_DATA:
        return "TSS_E_ENC_NO_DATA";
      case TSS_E_ENC_INVALID_TYPE:
        return "TSS_E_ENC_INVALID_TYPE";
      case TSS_E_INVALID_KEYUSAGE:
        return "TSS_E_INVALID_KEYUSAGE";
      case TSS_E_VERIFICATION_FAILED:
        return "TSS_E_VERIFICATION_FAILED";
      case TSS_E_HASH_NO_IDENTIFIER:
        return "TSS_E_HASH_NO_IDENTIFIER";
      case TSS_E_INVALID_HANDLE:
        return "TSS_E_INVALID_HANDLE";
      case TSS_E_SILENT_CONTEXT:
        return "TSS_E_SILENT_CONTEXT";
      case TSS_E_EK_CHECKSUM:
        return "TSS_E_EK_CHECKSUM";
      case TSS_E_DELEGATION_NOTSET:
        return "TSS_E_DELEGATION_NOTSET";
      case TSS_E_DELFAMILY_NOTFOUND:
        return "TSS_E_DELFAMILY_NOTFOUND";
      case TSS_E_DELFAMILY_ROWEXISTS:
        return "TSS_E_DELFAMILY_ROWEXISTS";
      case TSS_E_VERSION_MISMATCH:
        return "TSS_E_VERSION_MISMATCH";
      case TSS_E_DAA_AR_DECRYPTION_ERROR:
        return "TSS_E_DAA_AR_DECRYPTION_ERROR";
      case TSS_E_DAA_AUTHENTICATION_ERROR:
        return "TSS_E_DAA_AUTHENTICATION_ERROR";
      case TSS_E_DAA_CHALLENGE_RESPONSE_ERROR:
        return "TSS_E_DAA_CHALLENGE_RESPONSE_ERROR";
      case TSS_E_DAA_CREDENTIAL_PROOF_ERROR:
        return "TSS_E_DAA_CREDENTIAL_PROOF_ERROR";
      case TSS_E_DAA_CREDENTIAL_REQUEST_PROOF_ERROR:
        return "TSS_E_DAA_CREDENTIAL_REQUEST_PROOF_ERROR";
      case TSS_E_DAA_ISSUER_KEY_ERROR:
        return "TSS_E_DAA_ISSUER_KEY_ERROR";
      case TSS_E_DAA_PSEUDONYM_ERROR:
        return "TSS_E_DAA_PSEUDONYM_ERROR";
      case TSS_E_INVALID_RESOURCE:
        return "TSS_E_INVALID_RESOURCE";
      case TSS_E_NV_AREA_EXIST:
        return "TSS_E_NV_AREA_EXIST";
      case TSS_E_NV_AREA_NOT_EXIST:
        return "TSS_E_NV_AREA_NOT_EXIST";
      case TSS_E_TSP_TRANS_AUTHFAIL:
        return "TSS_E_TSP_TRANS_AUTHFAIL";
      case TSS_E_TSP_TRANS_AUTHREQUIRED:
        return "TSS_E_TSP_TRANS_AUTHREQUIRED";
      case TSS_E_TSP_TRANS_NOTEXCLUSIVE:
        return "TSS_E_TSP_TRANS_NOTEXCLUSIVE";
      case TSS_E_TSP_TRANS_FAIL:
        return "TSS_E_TSP_TRANS_FAIL";
      case TSS_E_TSP_TRANS_NO_PUBKEY:
        return "TSS_E_TSP_TRANS_NO_PUBKEY";
      case TSS_E_NO_ACTIVE_COUNTER:
        return "TSS_E_NO_ACTIVE_COUNTER";
    }
  }
  // Unknown value, just give the hex numeric value.
  stringstream ss;
  ss << "0x" << hex << result;
  return ss.str();
}

}  // namespace chaps
