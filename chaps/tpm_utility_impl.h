// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_TPM_UTILITY_IMPL_H_
#define CHAPS_TPM_UTILITY_IMPL_H_

#include "chaps/tpm_utility.h"

#include <map>
#include <set>
#include <string>

#include <base/macros.h>
#include <base/synchronization/lock.h>
#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>

namespace chaps {

class TPMUtilityImpl : public TPMUtility {
 public:
  // Min and max supported RSA modulus sizes (in bytes).
  const uint32_t kMinModulusSize = 64;
  const uint32_t kMaxModulusSize = 256;

  explicit TPMUtilityImpl(const std::string& srk_auth_data);
  TPMUtilityImpl(const TPMUtilityImpl&) = delete;
  TPMUtilityImpl& operator=(const TPMUtilityImpl&) = delete;

  ~TPMUtilityImpl() override;
  size_t MinRSAKeyBits() override { return kMinModulusSize * 8; }
  size_t MaxRSAKeyBits() override { return kMaxModulusSize * 8; }
  bool Init() override;
  bool IsTPMAvailable() override;
  TPMVersion GetTPMVersion() override;
  bool Authenticate(int slot_id,
                    const brillo::SecureBlob& auth_data,
                    const std::string& auth_key_blob,
                    const std::string& encrypted_master_key,
                    brillo::SecureBlob* master_key) override;
  bool ChangeAuthData(int slot_id,
                      const brillo::SecureBlob& old_auth_data,
                      const brillo::SecureBlob& new_auth_data,
                      const std::string& old_auth_key_blob,
                      std::string* new_auth_key_blob) override;
  bool GenerateRandom(int num_bytes, std::string* random_data) override;
  bool StirRandom(const std::string& entropy_data) override;
  bool GenerateRSAKey(int slot,
                      int modulus_bits,
                      const std::string& public_exponent,
                      const brillo::SecureBlob& auth_data,
                      std::string* key_blob,
                      int* key_handle) override;
  bool GetRSAPublicKey(int key_handle,
                       std::string* public_exponent,
                       std::string* modulus) override;
  bool IsECCurveSupported(int curve_nid) override;
  bool GenerateECCKey(int slot,
                      int nid,
                      const brillo::SecureBlob& auth_data,
                      std::string* key_blob,
                      int* key_handle) override;
  bool GetECCPublicKey(int key_handle, std::string* public_point) override;
  bool WrapRSAKey(int slot,
                  const std::string& public_exponent,
                  const std::string& modulus,
                  const std::string& prime_factor,
                  const brillo::SecureBlob& auth_data,
                  std::string* key_blob,
                  int* key_handle) override;
  bool WrapECCKey(int slot,
                  int curve_nid,
                  const std::string& public_point_x,
                  const std::string& public_point_y,
                  const std::string& private_value,
                  const brillo::SecureBlob& auth_data,
                  std::string* key_blob,
                  int* key_handle) override;
  bool LoadKey(int slot,
               const std::string& key_blob,
               const brillo::SecureBlob& auth_data,
               int* key_handle) override;
  bool LoadKeyWithParent(int slot,
                         const std::string& key_blob,
                         const brillo::SecureBlob& auth_data,
                         int parent_key_handle,
                         int* key_handle) override;
  void UnloadKeysForSlot(int slot) override;
  bool Bind(int key_handle,
            const std::string& input,
            std::string* output) override;
  bool Unbind(int key_handle,
              const std::string& input,
              std::string* output) override;
  bool Sign(int key_handle,
            CK_MECHANISM_TYPE signing_mechanism,
            const std::string& mechanism_parameter,
            const std::string& input,
            std::string* signature) override;
  bool IsSRKReady() override;
  // Stringifies TSS error codes.
  static std::string ResultToString(TSS_RESULT result);

 private:
  // Holds handle information for each slot.
  struct HandleInfo {
    // The set of all handles (for the slot).
    std::set<int> handles_;
    // Maps known blobs to the associated key handle.
    std::map<std::string, int> blob_handle_;
  };

  // Holds key information for each key handle.
  struct KeyInfo {
    TSS_HKEY tss_handle;
    std::string blob;
    brillo::SecureBlob auth_data;
  };

  int CreateHandle(int slot,
                   TSS_HKEY key,
                   const std::string& key_blob,
                   const brillo::SecureBlob& auth_data);
  bool CreateKeyPolicy(TSS_HKEY key,
                       const brillo::SecureBlob& auth_data,
                       bool auth_only);
  bool GetKeyAttributeData(TSS_HKEY key,
                           TSS_FLAG flag,
                           TSS_FLAG sub_flag,
                           std::string* data);
  bool GetKeyBlob(TSS_HKEY key, std::string* blob);
  TSS_FLAG GetKeyFlags(int modulus_bits);
  bool GetSRKPublicKey();
  TSS_HKEY GetTssHandle(int key_handle);
  bool IsAlreadyLoaded(int slot, const std::string& key_blob, int* key_handle);
  bool LoadKeyInternal(TSS_HKEY parent,
                       const std::string& key_blob,
                       const brillo::SecureBlob& auth_data,
                       TSS_HKEY* key);
  bool ReloadKey(int key_handle);
  bool InitSRK();

  bool is_initialized_;
  bool is_srk_ready_;
  trousers::ScopedTssContext tsp_context_;
  TSS_HPOLICY default_policy_;
  TSS_HKEY srk_;
  std::string srk_auth_data_;
  bool srk_public_loaded_;
  const std::string default_exponent_;
  std::map<int, HandleInfo> slot_handles_;
  std::map<int, KeyInfo> handle_info_;
  base::Lock lock_;
  int last_handle_;
  bool is_enabled_;
  bool is_enabled_ready_;
};

}  // namespace chaps

#endif  // CHAPS_TPM_UTILITY_IMPL_H_
