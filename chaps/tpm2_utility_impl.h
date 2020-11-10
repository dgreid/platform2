// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_TPM2_UTILITY_IMPL_H_
#define CHAPS_TPM2_UTILITY_IMPL_H_

#include "chaps/tpm_utility.h"

#include <map>
#include <memory>
#include <set>
#include <string>

#include <base/macros.h>
#include <base/single_thread_task_runner.h>
#include <base/synchronization/lock.h>
#include <gtest/gtest_prod.h>
#include <trunks/hmac_session.h>
#include <trunks/tpm_generated.h>
#include <trunks/tpm_utility.h>
#include <trunks/trunks_factory.h>
#include <trunks/trunks_factory_impl.h>

// TODO(http://crbug.com/473843, http://crosbug.com/p/59754): restore using
// one global session when session handles virtualization is supported by
// trunks.
#define CHAPS_TPM2_USE_PER_OP_SESSIONS

namespace chaps {

class TPM2UtilityImpl : public TPMUtility {
 public:
  // Min and max supported RSA modulus sizes (in bytes).
  const uint32_t kMinModulusSize = 128;
  const uint32_t kMaxModulusSize = 256;

  TPM2UtilityImpl();
  // This c'tor allows us to specify a task_runner to use to perform TPM
  // operations.
  explicit TPM2UtilityImpl(
      const scoped_refptr<base::SingleThreadTaskRunner>& task_runner);
  // Does not take ownership of |factory|.
  explicit TPM2UtilityImpl(trunks::TrunksFactory* factory);
  TPM2UtilityImpl(const TPM2UtilityImpl&) = delete;
  TPM2UtilityImpl& operator=(const TPM2UtilityImpl&) = delete;

  ~TPM2UtilityImpl() override;
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

  // Convert the RSA Key in the specified in |public_data| to the openssl RSA
  // object and return it if successful, otherwise, nullptr is returned.
  static crypto::ScopedRSA PublicAreaToScopedRsa(
      const trunks::TPMT_PUBLIC& public_data);

 private:
  // These internal methods implement the LoadKeyWithParent and Unbind methods.
  // They are implemented with no locking to allow the public methods above to
  // implement synchronization.
  bool LoadKeyWithParentInternal(int slot,
                                 const std::string& key_blob,
                                 const brillo::SecureBlob& auth_data,
                                 int parent_key_handle,
                                 int* key_handle);
  bool UnbindInternal(int key_handle,
                      const std::string& input,
                      std::string* output);

  // Load the public key of the key specified in |key_handle| to the openssl RSA
  // object, and return it if successful, otherwise, nullptr is returned.
  crypto::ScopedRSA KeyToScopedRsa(int key_handle);

  // This method flushes the provided |key_handle| from internal structures. If
  // |key_handle| is not tracked, this method does nothing.
  void FlushHandle(int key_handle);

  // Task runner used to operate the |default_trunks_proxy_|.
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  std::unique_ptr<trunks::CommandTransceiver> default_trunks_proxy_;
  std::unique_ptr<trunks::CommandTransceiver> default_background_transceiver_;
  std::unique_ptr<trunks::TrunksFactoryImpl> default_factory_;
  trunks::TrunksFactory* factory_;
  bool is_trunks_proxy_initialized_ = false;
  bool is_initialized_ = false;
  bool is_enabled_ready_ = false;
  bool is_enabled_ = false;
  base::Lock lock_;
  std::unique_ptr<trunks::HmacSession> session_;
  std::unique_ptr<trunks::TpmUtility> trunks_tpm_utility_;
  std::map<int, std::set<int>> slot_handles_;
  std::map<int, brillo::SecureBlob> handle_auth_data_;
  std::map<int, std::string> handle_name_;

  FRIEND_TEST(TPM2UtilityTest, IsTPMAvailable);
  FRIEND_TEST(TPM2UtilityTest, LoadKeySuccess);
  FRIEND_TEST(TPM2UtilityTest, UnloadKeysTest);
};

}  // namespace chaps

#endif  // CHAPS_TPM2_UTILITY_IMPL_H_
