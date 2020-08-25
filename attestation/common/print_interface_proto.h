// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// THIS CODE IS GENERATED.
// Generated with command:
// ./proto_print.py --subdir common --proto-include attestation/proto_bindings
// ../../system_api/dbus/attestation/interface.proto

#ifndef ATTESTATION_COMMON_PRINT_INTERFACE_PROTO_H_
#define ATTESTATION_COMMON_PRINT_INTERFACE_PROTO_H_

#include <string>

#include "attestation/proto_bindings/interface.pb.h"

namespace attestation {

std::string GetProtoDebugStringWithIndent(AttestationStatus value,
                                          int indent_size);
std::string GetProtoDebugString(AttestationStatus value);
std::string GetProtoDebugStringWithIndent(ACAType value, int indent_size);
std::string GetProtoDebugString(ACAType value);
std::string GetProtoDebugStringWithIndent(VAType value, int indent_size);
std::string GetProtoDebugString(VAType value);
std::string GetProtoDebugStringWithIndent(const GetKeyInfoRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetKeyInfoRequest& value);
std::string GetProtoDebugStringWithIndent(const GetKeyInfoReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetKeyInfoReply& value);
std::string GetProtoDebugStringWithIndent(
    const GetEndorsementInfoRequest& value, int indent_size);
std::string GetProtoDebugString(const GetEndorsementInfoRequest& value);
std::string GetProtoDebugStringWithIndent(const GetEndorsementInfoReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetEndorsementInfoReply& value);
std::string GetProtoDebugStringWithIndent(
    const GetAttestationKeyInfoRequest& value, int indent_size);
std::string GetProtoDebugString(const GetAttestationKeyInfoRequest& value);
std::string GetProtoDebugStringWithIndent(
    const GetAttestationKeyInfoReply& value, int indent_size);
std::string GetProtoDebugString(const GetAttestationKeyInfoReply& value);
std::string GetProtoDebugStringWithIndent(
    const ActivateAttestationKeyRequest& value, int indent_size);
std::string GetProtoDebugString(const ActivateAttestationKeyRequest& value);
std::string GetProtoDebugStringWithIndent(
    const ActivateAttestationKeyReply& value, int indent_size);
std::string GetProtoDebugString(const ActivateAttestationKeyReply& value);
std::string GetProtoDebugStringWithIndent(
    const CreateCertifiableKeyRequest& value, int indent_size);
std::string GetProtoDebugString(const CreateCertifiableKeyRequest& value);
std::string GetProtoDebugStringWithIndent(
    const CreateCertifiableKeyReply& value, int indent_size);
std::string GetProtoDebugString(const CreateCertifiableKeyReply& value);
std::string GetProtoDebugStringWithIndent(const DecryptRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const DecryptRequest& value);
std::string GetProtoDebugStringWithIndent(const DecryptReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const DecryptReply& value);
std::string GetProtoDebugStringWithIndent(const SignRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const SignRequest& value);
std::string GetProtoDebugStringWithIndent(const SignReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const SignReply& value);
std::string GetProtoDebugStringWithIndent(
    const RegisterKeyWithChapsTokenRequest& value, int indent_size);
std::string GetProtoDebugString(const RegisterKeyWithChapsTokenRequest& value);
std::string GetProtoDebugStringWithIndent(
    const RegisterKeyWithChapsTokenReply& value, int indent_size);
std::string GetProtoDebugString(const RegisterKeyWithChapsTokenReply& value);
std::string GetProtoDebugStringWithIndent(
    const GetEnrollmentPreparationsRequest& value, int indent_size);
std::string GetProtoDebugString(const GetEnrollmentPreparationsRequest& value);
std::string GetProtoDebugStringWithIndent(
    const GetEnrollmentPreparationsReply& value, int indent_size);
std::string GetProtoDebugString(const GetEnrollmentPreparationsReply& value);
std::string GetProtoDebugStringWithIndent(const GetStatusRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetStatusRequest& value);
std::string GetProtoDebugStringWithIndent(const GetStatusReply::Identity& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetStatusReply::Identity& value);
std::string GetProtoDebugStringWithIndent(
    const GetStatusReply::IdentityCertificate& value, int indent_size);
std::string GetProtoDebugString(
    const GetStatusReply::IdentityCertificate& value);
std::string GetProtoDebugStringWithIndent(const GetStatusReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetStatusReply& value);
std::string GetProtoDebugStringWithIndent(const VerifyRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const VerifyRequest& value);
std::string GetProtoDebugStringWithIndent(const VerifyReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const VerifyReply& value);
std::string GetProtoDebugStringWithIndent(
    const CreateEnrollRequestRequest& value, int indent_size);
std::string GetProtoDebugString(const CreateEnrollRequestRequest& value);
std::string GetProtoDebugStringWithIndent(const CreateEnrollRequestReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const CreateEnrollRequestReply& value);
std::string GetProtoDebugStringWithIndent(const FinishEnrollRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const FinishEnrollRequest& value);
std::string GetProtoDebugStringWithIndent(const FinishEnrollReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const FinishEnrollReply& value);
std::string GetProtoDebugStringWithIndent(const EnrollRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const EnrollRequest& value);
std::string GetProtoDebugStringWithIndent(const EnrollReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const EnrollReply& value);
std::string GetProtoDebugStringWithIndent(
    const CreateCertificateRequestRequest& value, int indent_size);
std::string GetProtoDebugString(const CreateCertificateRequestRequest& value);
std::string GetProtoDebugStringWithIndent(
    const CreateCertificateRequestReply& value, int indent_size);
std::string GetProtoDebugString(const CreateCertificateRequestReply& value);
std::string GetProtoDebugStringWithIndent(
    const FinishCertificateRequestRequest& value, int indent_size);
std::string GetProtoDebugString(const FinishCertificateRequestRequest& value);
std::string GetProtoDebugStringWithIndent(
    const FinishCertificateRequestReply& value, int indent_size);
std::string GetProtoDebugString(const FinishCertificateRequestReply& value);
std::string GetProtoDebugStringWithIndent(const GetCertificateRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetCertificateRequest& value);
std::string GetProtoDebugStringWithIndent(const GetCertificateReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetCertificateReply& value);
std::string GetProtoDebugStringWithIndent(
    const SignEnterpriseChallengeRequest& value, int indent_size);
std::string GetProtoDebugString(const SignEnterpriseChallengeRequest& value);
std::string GetProtoDebugStringWithIndent(
    const SignEnterpriseChallengeReply& value, int indent_size);
std::string GetProtoDebugString(const SignEnterpriseChallengeReply& value);
std::string GetProtoDebugStringWithIndent(
    const SignSimpleChallengeRequest& value, int indent_size);
std::string GetProtoDebugString(const SignSimpleChallengeRequest& value);
std::string GetProtoDebugStringWithIndent(const SignSimpleChallengeReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const SignSimpleChallengeReply& value);
std::string GetProtoDebugStringWithIndent(const SetKeyPayloadRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const SetKeyPayloadRequest& value);
std::string GetProtoDebugStringWithIndent(const SetKeyPayloadReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const SetKeyPayloadReply& value);
std::string GetProtoDebugStringWithIndent(const DeleteKeysRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const DeleteKeysRequest& value);
std::string GetProtoDebugStringWithIndent(const DeleteKeysReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const DeleteKeysReply& value);
std::string GetProtoDebugStringWithIndent(const ResetIdentityRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const ResetIdentityRequest& value);
std::string GetProtoDebugStringWithIndent(const ResetIdentityReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const ResetIdentityReply& value);
std::string GetProtoDebugStringWithIndent(const GetEnrollmentIdRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetEnrollmentIdRequest& value);
std::string GetProtoDebugStringWithIndent(const GetEnrollmentIdReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetEnrollmentIdReply& value);
std::string GetProtoDebugStringWithIndent(
    const GetCertifiedNvIndexRequest& value, int indent_size);
std::string GetProtoDebugString(const GetCertifiedNvIndexRequest& value);
std::string GetProtoDebugStringWithIndent(const GetCertifiedNvIndexReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetCertifiedNvIndexReply& value);

}  // namespace attestation

#endif  // ATTESTATION_COMMON_PRINT_INTERFACE_PROTO_H_
