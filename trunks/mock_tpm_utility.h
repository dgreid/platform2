// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_MOCK_TPM_UTILITY_H_
#define TRUNKS_MOCK_TPM_UTILITY_H_

#include <map>
#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "trunks/tpm_utility.h"

namespace trunks {

class MockTpmUtility : public TpmUtility {
 public:
  MockTpmUtility();
  ~MockTpmUtility() override;

  MOCK_METHOD0(Startup, TPM_RC());
  MOCK_METHOD0(Clear, TPM_RC());
  MOCK_METHOD0(Shutdown, void());
  MOCK_METHOD0(InitializeTpm, TPM_RC());
  MOCK_METHOD0(CheckState, TPM_RC());
  MOCK_METHOD1(AllocatePCR, TPM_RC(const std::string&));
  MOCK_METHOD0(PrepareForOwnership, TPM_RC());
  MOCK_METHOD3(TakeOwnership,
               TPM_RC(const std::string&,
                      const std::string&,
                      const std::string&));
  MOCK_METHOD2(StirRandom, TPM_RC(const std::string&, AuthorizationDelegate*));
  MOCK_METHOD3(GenerateRandom,
               TPM_RC(size_t, AuthorizationDelegate*, std::string*));
  MOCK_METHOD1(GetAlertsData, TPM_RC(TpmAlertsData*));
  MOCK_METHOD3(ExtendPCR,
               TPM_RC(int, const std::string&, AuthorizationDelegate*));
  MOCK_METHOD2(ReadPCR, TPM_RC(int, std::string*));
  MOCK_METHOD6(AsymmetricEncrypt,
               TPM_RC(TPM_HANDLE,
                      TPM_ALG_ID,
                      TPM_ALG_ID,
                      const std::string&,
                      AuthorizationDelegate*,
                      std::string*));
  MOCK_METHOD6(AsymmetricDecrypt,
               TPM_RC(TPM_HANDLE,
                      TPM_ALG_ID,
                      TPM_ALG_ID,
                      const std::string&,
                      AuthorizationDelegate*,
                      std::string*));
  MOCK_METHOD7(RawSign,
               TPM_RC(TPM_HANDLE,
                      TPM_ALG_ID,
                      TPM_ALG_ID,
                      const std::string&,
                      bool,
                      AuthorizationDelegate*,
                      TPMT_SIGNATURE*));
  MOCK_METHOD7(Sign,
               TPM_RC(TPM_HANDLE,
                      TPM_ALG_ID,
                      TPM_ALG_ID,
                      const std::string&,
                      bool,
                      AuthorizationDelegate*,
                      std::string*));
  MOCK_METHOD7(Verify,
               TPM_RC(TPM_HANDLE,
                      TPM_ALG_ID,
                      TPM_ALG_ID,
                      const std::string&,
                      bool,
                      const std::string&,
                      AuthorizationDelegate*));
  MOCK_METHOD2(CertifyCreation, TPM_RC(TPM_HANDLE, const std::string&));
  MOCK_METHOD4(ChangeKeyAuthorizationData,
               TPM_RC(TPM_HANDLE,
                      const std::string&,
                      AuthorizationDelegate*,
                      std::string*));
  MOCK_METHOD7(ImportRSAKey,
               TPM_RC(AsymmetricKeyUsage,
                      const std::string&,
                      uint32_t,
                      const std::string&,
                      const std::string&,
                      AuthorizationDelegate*,
                      std::string*));
  MOCK_METHOD8(ImportECCKey,
               TPM_RC(AsymmetricKeyUsage,
                      TPMI_ECC_CURVE,
                      const std::string&,
                      const std::string&,
                      const std::string&,
                      const std::string&,
                      AuthorizationDelegate*,
                      std::string*));
  MOCK_METHOD10(CreateRSAKeyPair,
                TPM_RC(AsymmetricKeyUsage,
                       int,
                       uint32_t,
                       const std::string&,
                       const std::string&,
                       bool,
                       const std::vector<uint32_t>&,
                       AuthorizationDelegate*,
                       std::string*,
                       std::string*));
  MOCK_METHOD9(CreateECCKeyPair,
               TPM_RC(AsymmetricKeyUsage,
                      TPMI_ECC_CURVE,
                      const std::string&,
                      const std::string&,
                      bool,
                      const std::vector<uint32_t>&,
                      AuthorizationDelegate*,
                      std::string*,
                      std::string*));
  MOCK_METHOD3(LoadKey,
               TPM_RC(const std::string&, AuthorizationDelegate*, TPM_HANDLE*));
  MOCK_METHOD7(LoadRSAPublicKey,
               TPM_RC(AsymmetricKeyUsage,
                      TPM_ALG_ID,
                      TPM_ALG_ID,
                      const std::string&,
                      uint32_t,
                      AuthorizationDelegate*,
                      TPM_HANDLE*));
  MOCK_METHOD8(LoadECPublicKey,
               TPM_RC(AsymmetricKeyUsage,
                      TPM_ECC_CURVE,
                      TPM_ALG_ID,
                      TPM_ALG_ID,
                      const std::string&,
                      const std::string&,
                      AuthorizationDelegate*,
                      TPM_HANDLE*));
  MOCK_METHOD2(GetKeyName, TPM_RC(TPM_HANDLE, std::string*));
  MOCK_METHOD2(GetKeyPublicArea, TPM_RC(TPM_HANDLE, TPMT_PUBLIC*));
  MOCK_METHOD5(SealData,
               TPM_RC(const std::string&,
                      const std::string&,
                      const std::string&,
                      AuthorizationDelegate*,
                      std::string*));
  MOCK_METHOD3(UnsealData,
               TPM_RC(const std::string&,
                      AuthorizationDelegate*,
                      std::string*));
  MOCK_METHOD1(StartSession, TPM_RC(HmacSession*));
  MOCK_METHOD3(GetPolicyDigestForPcrValues,
               TPM_RC(const std::map<uint32_t, std::string>&, bool,
                      std::string*));
  MOCK_METHOD6(DefineNVSpace,
               TPM_RC(uint32_t,
                      size_t,
                      TPMA_NV,
                      const std::string&,
                      const std::string&,
                      AuthorizationDelegate*));
  MOCK_METHOD2(DestroyNVSpace, TPM_RC(uint32_t, AuthorizationDelegate*));
  MOCK_METHOD5(LockNVSpace,
               TPM_RC(uint32_t, bool, bool, bool, AuthorizationDelegate*));
  MOCK_METHOD6(WriteNVSpace,
               TPM_RC(uint32_t,
                      uint32_t,
                      const std::string&,
                      bool,
                      bool,
                      AuthorizationDelegate*));
  MOCK_METHOD6(ReadNVSpace,
               TPM_RC(uint32_t,
                      uint32_t,
                      size_t,
                      bool,
                      std::string*,
                      AuthorizationDelegate*));
  MOCK_METHOD2(GetNVSpaceName, TPM_RC(uint32_t, std::string*));
  MOCK_METHOD2(GetNVSpacePublicArea, TPM_RC(uint32_t, TPMS_NV_PUBLIC*));
  MOCK_METHOD1(ListNVSpaces, TPM_RC(std::vector<uint32_t>*));
  MOCK_METHOD4(SetDictionaryAttackParameters,
               TPM_RC(uint32_t, uint32_t, uint32_t, AuthorizationDelegate*));
  MOCK_METHOD1(ResetDictionaryAttackLock, TPM_RC(AuthorizationDelegate*));
  MOCK_METHOD4(GetEndorsementKey,
               TPM_RC(TPM_ALG_ID,
                      AuthorizationDelegate*,
                      AuthorizationDelegate*,
                      TPM_HANDLE*));
  MOCK_METHOD3(CreateIdentityKey,
               TPM_RC(TPM_ALG_ID, AuthorizationDelegate*, std::string*));
  MOCK_METHOD0(DeclareTpmFirmwareStable, TPM_RC());
  MOCK_METHOD1(GetPublicRSAEndorsementKeyModulus, TPM_RC(std::string*));
  MOCK_METHOD1(ManageCCDPwd, TPM_RC(bool));
  MOCK_METHOD2(PinWeaverIsSupported, TPM_RC(uint8_t, uint8_t*));
  MOCK_METHOD5(PinWeaverResetTree,
               TPM_RC(uint8_t, uint8_t, uint8_t, uint32_t*, std::string*));
  // No MOCK_METHOD11 (max is 10).
  TPM_RC PinWeaverInsertLeaf(uint8_t protocol_version,
                             uint64_t label,
                             const std::string& h_aux,
                             const brillo::SecureBlob& le_secret,
                             const brillo::SecureBlob& he_secret,
                             const brillo::SecureBlob& reset_secret,
                             const std::map<uint32_t, uint32_t>& delay_schedule,
                             const ValidPcrCriteria& valid_pcr_criteria,
                             uint32_t* result_code,
                             std::string* root_hash,
                             std::string* cred_metadata,
                             std::string* mac) override;
  MOCK_METHOD6(PinWeaverRemoveLeaf,
               TPM_RC(uint8_t,
                      uint64_t,
                      const std::string&,
                      const std::string&,
                      uint32_t*,
                      std::string*));
  TPM_RC PinWeaverTryAuth(uint8_t protocol_version,
                          const brillo::SecureBlob& le_secret,
                          const std::string& h_aux,
                          const std::string& cred_metadata,
                          uint32_t* result_code,
                          std::string* root_hash,
                          uint32_t* seconds_to_wait,
                          brillo::SecureBlob* he_secret,
                          brillo::SecureBlob* reset_secret,
                          std::string* cred_metadata_out,
                          std::string* mac_out) override;
  MOCK_METHOD9(PinWeaverResetAuth,
               TPM_RC(uint8_t,
                      const brillo::SecureBlob&,
                      const std::string&,
                      const std::string&,
                      uint32_t*,
                      std::string*,
                      brillo::SecureBlob*,
                      std::string*,
                      std::string*));
  MOCK_METHOD5(PinWeaverGetLog,
               TPM_RC(uint8_t,
                      const std::string&,
                      uint32_t*,
                      std::string*,
                      std::vector<trunks::PinWeaverLogEntry>*));
  MOCK_METHOD8(PinWeaverLogReplay,
               TPM_RC(uint8_t,
                      const std::string&,
                      const std::string&,
                      const std::string&,
                      uint32_t*,
                      std::string*,
                      std::string*,
                      std::string*));
  MOCK_METHOD1(GetRsuDeviceId, TPM_RC(std::string*));
};

}  // namespace trunks

#endif  // TRUNKS_MOCK_TPM_UTILITY_H_
