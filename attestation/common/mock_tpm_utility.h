// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_COMMON_MOCK_TPM_UTILITY_H_
#define ATTESTATION_COMMON_MOCK_TPM_UTILITY_H_

#include "attestation/common/tpm_utility.h"

#include <stdint.h>

#include <string>

#include <gmock/gmock.h>

namespace attestation {

class MockTpmUtility : public TpmUtility {
 public:
  MockTpmUtility();
  ~MockTpmUtility() override;
  // By default this class will fake seal/unbind/sign operations by passing the
  // input through Transform(<method>). E.g. The expected output of a fake Sign
  // operation on "foo" can be computed by calling
  // MockTpmUtility::Transform("Sign", "foo").
  static std::string Transform(const std::string& method,
                               const std::string& input);

  MOCK_METHOD(bool, Initialize, (), (override));
  MOCK_METHOD(TpmVersion, GetVersion, (), (override));
  MOCK_METHOD(bool, IsTpmReady, (), (override));
  MOCK_METHOD(bool,
              ActivateIdentity,
              (const std::string&,
               const std::string&,
               const std::string&,
               std::string*),
              (override));
  MOCK_METHOD(bool,
              ActivateIdentityForTpm2,
              (KeyType,
               const std::string&,
               const std::string&,
               const std::string&,
               const std::string&,
               std::string*),
              (override));
  MOCK_METHOD(bool,
              CreateCertifiedKey,
              (KeyType,
               KeyUsage,
               const std::string&,
               const std::string&,
               std::string*,
               std::string*,
               std::string*,
               std::string*,
               std::string*),
              (override));
  MOCK_METHOD(bool, SealToPCR0, (const std::string&, std::string*), (override));
  MOCK_METHOD(bool, Unseal, (const std::string&, std::string*), (override));
  MOCK_METHOD(bool,
              GetEndorsementPublicKey,
              (KeyType, std::string*),
              (override));
  MOCK_METHOD(bool,
              GetEndorsementPublicKeyModulus,
              (KeyType, std::string*),
              (override));
  MOCK_METHOD(bool,
              GetEndorsementCertificate,
              (KeyType, std::string*),
              (override));
  MOCK_METHOD(bool,
              Unbind,
              (const std::string&, const std::string&, std::string*),
              (override));
  MOCK_METHOD(bool,
              Sign,
              (const std::string&, const std::string&, std::string*),
              (override));
  MOCK_METHOD(
      bool,
      QuotePCR,
      (uint32_t, const std::string&, std::string*, std::string*, std::string*),
      (override));
  MOCK_METHOD(bool, GetNVDataSize, (uint32_t, uint16_t*), (const, override));
  MOCK_METHOD(bool,
              CertifyNV,
              (uint32_t, int, const std::string&, std::string*, std::string*),
              (override));
  MOCK_METHOD(
      bool,
      IsQuoteForPCR,
      (const std::string&, const std::string&, const std::string&, uint32_t),
      (const, override));
  MOCK_METHOD(bool, ReadPCR, (uint32_t, std::string*), (override));
  MOCK_METHOD(bool, IsPCR0Valid, (), (override));
  MOCK_METHOD(bool, RemoveOwnerDependency, (), (override));
  MOCK_METHOD(bool,
              CreateIdentity,
              (KeyType, AttestationDatabase::Identity*),
              (override));
  MOCK_METHOD(bool, GetRsuDeviceId, (std::string*), (override));
};

}  // namespace attestation

#endif  // ATTESTATION_COMMON_MOCK_TPM_UTILITY_H_
