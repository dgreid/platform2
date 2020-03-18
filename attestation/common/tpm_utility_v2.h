// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_COMMON_TPM_UTILITY_V2_H_
#define ATTESTATION_COMMON_TPM_UTILITY_V2_H_

#include <stdint.h>

#include "attestation/common/tpm_utility.h"

#include <map>
#include <memory>
#include <string>

#include <base/macros.h>
#include <base/threading/thread.h>

#include "tpm_manager/client/tpm_nvram_dbus_proxy.h"
#include "tpm_manager/client/tpm_ownership_dbus_proxy.h"
#include "trunks/trunks_factory_impl.h"

namespace attestation {

// A TpmUtility implementation for TPM v2.0 modules.
class TpmUtilityV2 : public TpmUtility {
 public:
  TpmUtilityV2() = default;
  TpmUtilityV2(tpm_manager::TpmOwnershipInterface* tpm_owner,
               tpm_manager::TpmNvramInterface* tpm_nvram,
               trunks::TrunksFactory* trunks_factory);
  ~TpmUtilityV2() override;

  // TpmUtility methods.
  bool Initialize() override;
  TpmVersion GetVersion() override { return TPM_2_0; }
  bool IsTpmReady() override;
  bool ActivateIdentity(const std::string& identity_key_blob,
                        const std::string& asym_ca_contents,
                        const std::string& sym_ca_attestation,
                        std::string* credential) override;
  bool ActivateIdentityForTpm2(KeyType key_type,
                               const std::string& identity_key_blob,
                               const std::string& encrypted_seed,
                               const std::string& credential_mac,
                               const std::string& wrapped_credential,
                               std::string* credential) override;
  bool CreateCertifiedKey(KeyType key_type,
                          KeyUsage key_usage,
                          const std::string& identity_key_blob,
                          const std::string& external_data,
                          std::string* key_blob,
                          std::string* public_key_der,
                          std::string* public_key_tpm_format,
                          std::string* key_info,
                          std::string* proof) override;
  bool SealToPCR0(const std::string& data, std::string* sealed_data) override;
  bool Unseal(const std::string& sealed_data, std::string* data) override;
  bool GetEndorsementPublicKey(KeyType key_type,
                               std::string* public_key_der) override;
  bool GetEndorsementCertificate(KeyType key_type,
                                 std::string* certificate) override;
  bool Unbind(const std::string& key_blob,
              const std::string& bound_data,
              std::string* data) override;
  bool Sign(const std::string& key_blob,
            const std::string& data_to_sign,
            std::string* signature) override;
  bool QuotePCR(uint32_t pcr_index,
                const std::string& key_blob,
                std::string* quoted_pcr_value,
                std::string* quoted_data,
                std::string* quote) override;
  bool IsQuoteForPCR(const std::string& quoted_pcr_value,
                     const std::string& quoted_data,
                     const std::string& quote,
                     uint32_t pcr_index) const override;
  bool ReadPCR(uint32_t pcr_index, std::string* pcr_value) override;
  bool GetNVDataSize(uint32_t nv_index, uint16_t* nv_size) const override;
  bool CertifyNV(uint32_t nv_index,
                 int nv_size,
                 const std::string& key_blob,
                 std::string* quoted_data,
                 std::string* quote) override;
  bool RemoveOwnerDependency() override;
  bool GetEndorsementPublicKeyModulus(KeyType key_type,
                                      std::string* ekm) override;

  bool CreateIdentity(KeyType key_type,
                      AttestationDatabase::Identity* identity) override;

  bool GetRsuDeviceId(std::string* device_id) override;

  // Creates a restricted key of |key_type| for |key_usage|.
  // |public_key_der| is DER encoded which is converted from TPM public key
  // object. |public_key_tpm_format| is a serialized TPMT_PUBLIC.
  // |private_key_blob| is an opaque blob which only the TPM is able to unwrap.
  // Note: Currently the function is still in the public field because of the
  // legacy unittest code.
  // TODO(cylai): redesign the interface of this class or move out to a common
  // TPM2.0 utility.
  bool CreateRestrictedKey(KeyType key_type,
                           KeyUsage key_usage,
                           std::string* public_key_der,
                           std::string* public_key_tpm_format,
                           std::string* private_key_blob);

 private:
  // Tpm_manager communication thread class that cleans up after stopping.
  class TpmManagerThread : public base::Thread {
   public:
    explicit TpmManagerThread(TpmUtilityV2* tpm_utility)
        : base::Thread("tpm_manager_thread"), tpm_utility_(tpm_utility) {
      DCHECK(tpm_utility_);
    }
    ~TpmManagerThread() override {
      Stop();
    }

   private:
    void CleanUp() override {
      tpm_utility_->ShutdownTask();
    }

    TpmUtilityV2* const tpm_utility_;

    DISALLOW_COPY_AND_ASSIGN(TpmManagerThread);
  };

  // Initialization operation that must be performed on the tpm_manager
  // thread.
  void InitializationTask(base::WaitableEvent* completion);

  // Shutdown operation that must be performed on the tpm_manager thread.
  void ShutdownTask();

  // Sends a request to tpm_managerd and waits for a response. The given
  // interface |method| will be called and a |reply_proto| will be populated.
  //
  // Example usage:
  //
  // tpm_manager::GetTpmStatusReply tpm_status;
  // SendTpmManagerRequestAndWait(
  //     base::Bind(&tpm_manager::TpmOwnershipInterface::GetTpmStatus,
  //                base::Unretained(tpm_owner_),
  //                tpm_manager::GetTpmStatusRequest()),
  //     &tpm_status);
  template <typename ReplyProtoType, typename MethodType>
  void SendTpmManagerRequestAndWait(const MethodType& method,
                                    ReplyProtoType* reply_proto);

  // Gets the endorsement password from tpm_managerd. Returns false if the
  // password is not available.
  bool GetEndorsementPassword(std::string* password);

  // Gets the owner password from tpm_managerd. Returns false if the password is
  // not available.
  bool GetOwnerPassword(std::string* password);

  // Caches various TPM state including owner / endorsement passwords. On
  // success, fields like is_ready_ and owner_password_ will be populated.
  // Returns true on success.
  bool CacheTpmState();

  // Gets the specified endorsement key. Returns true on success and provides
  // the |key_handle|.
  bool GetEndorsementKey(KeyType key_type, trunks::TPM_HANDLE* key_handle);

  bool is_ready_{false};
  std::string endorsement_password_;
  std::string owner_password_;
  std::map<KeyType, trunks::TPM_HANDLE> endorsement_keys_;
  // |tpm_owner_| and |tpm_nvram_| typically point to |default_tpm_owner_| and
  // |default_tpm_nvram_| respectively, created/destroyed on the
  // |tpm_manager_thread_|. As such, should not be accessed after that thread
  // is stopped/destroyed.
  tpm_manager::TpmOwnershipInterface* tpm_owner_{nullptr};
  tpm_manager::TpmNvramInterface* tpm_nvram_{nullptr};
  trunks::TrunksFactory* trunks_factory_{nullptr};
  // |default_tpm_owner_| and |default_tpm_nvram_| are created and destroyed
  // on the |tpm_manager_thread_|, and are not available after the thread is
  // stopped/destroyed.
  std::unique_ptr<tpm_manager::TpmOwnershipDBusProxy> default_tpm_owner_;
  std::unique_ptr<tpm_manager::TpmNvramDBusProxy> default_tpm_nvram_;
  std::unique_ptr<trunks::TrunksFactoryImpl> default_trunks_factory_;
  std::unique_ptr<trunks::TpmUtility> trunks_utility_;

  // A message loop thread dedicated for asynchronous communication with
  // tpm_managerd. Declared last, so that it is destroyed before the
  // objects it uses.
  TpmManagerThread tpm_manager_thread_{this};

  DISALLOW_COPY_AND_ASSIGN(TpmUtilityV2);
};

}  // namespace attestation

#endif  // ATTESTATION_COMMON_TPM_UTILITY_V2_H_
