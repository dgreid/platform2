// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_COMMON_MOCK_ATTESTATION_INTERFACE_H_
#define ATTESTATION_COMMON_MOCK_ATTESTATION_INTERFACE_H_

#include <string>

#include <gmock/gmock.h>

#include "attestation/common/attestation_interface.h"

namespace attestation {

class MockAttestationInterface : public AttestationInterface {
 public:
  MockAttestationInterface() = default;
  virtual ~MockAttestationInterface() = default;

  MOCK_METHOD(bool, Initialize, (), (override));
  MOCK_METHOD(void,
              GetKeyInfo,
              (const GetKeyInfoRequest&, const GetKeyInfoCallback&),
              (override));
  MOCK_METHOD(void,
              GetEndorsementInfo,
              (const GetEndorsementInfoRequest&,
               const GetEndorsementInfoCallback&),
              (override));
  MOCK_METHOD(void,
              GetAttestationKeyInfo,
              (const GetAttestationKeyInfoRequest&,
               const GetAttestationKeyInfoCallback&),
              (override));
  MOCK_METHOD(void,
              ActivateAttestationKey,
              (const ActivateAttestationKeyRequest&,
               const ActivateAttestationKeyCallback&),
              (override));
  MOCK_METHOD(void,
              CreateCertifiableKey,
              (const CreateCertifiableKeyRequest&,
               const CreateCertifiableKeyCallback&),
              (override));
  MOCK_METHOD(void,
              Decrypt,
              (const DecryptRequest&, const DecryptCallback&),
              (override));
  MOCK_METHOD(void,
              Sign,
              (const SignRequest&, const SignCallback&),
              (override));
  MOCK_METHOD(void,
              RegisterKeyWithChapsToken,
              (const RegisterKeyWithChapsTokenRequest&,
               const RegisterKeyWithChapsTokenCallback&),
              (override));
  MOCK_METHOD(void,
              GetEnrollmentPreparations,
              (const GetEnrollmentPreparationsRequest&,
               const GetEnrollmentPreparationsCallback&),
              (override));
  MOCK_METHOD(void,
              GetStatus,
              (const GetStatusRequest&, const GetStatusCallback&),
              (override));
  MOCK_METHOD(void,
              Verify,
              (const VerifyRequest&, const VerifyCallback&),
              (override));
  MOCK_METHOD(void,
              CreateEnrollRequest,
              (const CreateEnrollRequestRequest&,
               const CreateEnrollRequestCallback&),
              (override));
  MOCK_METHOD(void,
              Enroll,
              (const EnrollRequest&, const EnrollCallback&),
              (override));
  MOCK_METHOD(void,
              FinishEnroll,
              (const FinishEnrollRequest&, const FinishEnrollCallback&),
              (override));
  MOCK_METHOD(void,
              CreateCertificateRequest,
              (const CreateCertificateRequestRequest&,
               const CreateCertificateRequestCallback&),
              (override));
  MOCK_METHOD(void,
              FinishCertificateRequest,
              (const FinishCertificateRequestRequest&,
               const FinishCertificateRequestCallback&),
              (override));
  MOCK_METHOD(void,
              GetCertificate,
              (const GetCertificateRequest&, const GetCertificateCallback&),
              (override));
  MOCK_METHOD(void,
              SignEnterpriseChallenge,
              (const SignEnterpriseChallengeRequest&,
               const SignEnterpriseChallengeCallback&),
              (override));
  MOCK_METHOD(void,
              SignSimpleChallenge,
              (const SignSimpleChallengeRequest&,
               const SignSimpleChallengeCallback&),
              (override));
  MOCK_METHOD(void,
              SetKeyPayload,
              (const SetKeyPayloadRequest&, const SetKeyPayloadCallback&),
              (override));
  MOCK_METHOD(void,
              DeleteKeys,
              (const DeleteKeysRequest&, const DeleteKeysCallback&),
              (override));
  MOCK_METHOD(void,
              ResetIdentity,
              (const ResetIdentityRequest&, const ResetIdentityCallback&),
              (override));
  MOCK_METHOD(void,
              GetEnrollmentId,
              (const GetEnrollmentIdRequest&, const GetEnrollmentIdCallback&),
              (override));
  MOCK_METHOD(void,
              GetCertifiedNvIndex,
              (const GetCertifiedNvIndexRequest&,
               const GetCertifiedNvIndexCallback&),
              (override));
};

}  // namespace attestation

#endif  // ATTESTATION_COMMON_MOCK_ATTESTATION_INTERFACE_H_
