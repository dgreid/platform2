// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Asynchronous attestation tasks.

#include "cryptohome/attestation_task.h"

#include <string>

#include <brillo/secure_blob.h>

#include "cryptohome/attestation.h"

using brillo::SecureBlob;

namespace cryptohome {

AttestationTask::AttestationTask(AttestationTaskObserver* observer,
                                 Attestation* attestation,
                                 int sequence_id)
    : MountTask(observer, NULL, sequence_id),
      attestation_(attestation) {
}

AttestationTask::~AttestationTask() {}

CreateEnrollRequestTask::CreateEnrollRequestTask(
    AttestationTaskObserver* observer,
    Attestation* attestation,
    Attestation::PCAType pca_type,
    int sequence_id)
    : AttestationTask(observer, attestation, sequence_id),
      pca_type_(pca_type) {}

CreateEnrollRequestTask::~CreateEnrollRequestTask() {}

void CreateEnrollRequestTask::Run() {
  result()->set_return_status(FALSE);
  if (attestation_) {
    SecureBlob pca_request;
    bool status = attestation_->CreateEnrollRequest(pca_type_, &pca_request);
    result()->set_return_status(status);
    result()->set_return_data(pca_request);
  }
  Notify();
}

EnrollTask::EnrollTask(AttestationTaskObserver* observer,
                       Attestation* attestation,
                       Attestation::PCAType pca_type,
                       const SecureBlob& pca_response,
                       int sequence_id)
    : AttestationTask(observer, attestation, sequence_id),
      pca_type_(pca_type),
      pca_response_(pca_response) {
}

EnrollTask::~EnrollTask() {}

void EnrollTask::Run() {
  result()->set_return_status(FALSE);
  if (attestation_) {
    bool status = attestation_->Enroll(pca_type_, pca_response_);
    result()->set_return_status(status);
  }
  Notify();
}

EnrollExTask::EnrollExTask(AttestationTaskObserver* observer,
                           Attestation* attestation,
                           Attestation::PCAType pca_type,
                           bool forced,
                           int sequence_id)
    : AttestationTask(observer, attestation, sequence_id),
      pca_type_(pca_type),
      forced_(forced) {}

void EnrollExTask::Run() {
  result()->set_return_status(FALSE);
  if (attestation_) {
    bool status = attestation_->EnrollEx(pca_type_, forced_);
    result()->set_return_status(status);
  }
  Notify();
}

CreateCertRequestTask::CreateCertRequestTask(AttestationTaskObserver* observer,
                                             Attestation* attestation,
                                             Attestation::PCAType pca_type,
                                             CertificateProfile profile,
                                             const std::string& username,
                                             const std::string& origin,
                                             int sequence_id)
    : AttestationTask(observer, attestation, sequence_id),
      pca_type_(pca_type),
      profile_(profile),
      username_(username),
      origin_(origin) {
}

CreateCertRequestTask::~CreateCertRequestTask() {}

void CreateCertRequestTask::Run() {
  result()->set_return_status(FALSE);
  if (attestation_) {
    SecureBlob pca_request;
    bool status = attestation_->CreateCertRequest(pca_type_,
                                                  profile_,
                                                  username_,
                                                  origin_,
                                                  &pca_request);
    result()->set_return_status(status);
    result()->set_return_data(pca_request);
  }
  Notify();
}

FinishCertRequestTask::FinishCertRequestTask(AttestationTaskObserver* observer,
                                             Attestation* attestation,
                                             const SecureBlob& pca_response,
                                             bool is_user_specific,
                                             const std::string& username,
                                             const std::string& key_name,
                                             int sequence_id)
    : AttestationTask(observer, attestation, sequence_id),
      pca_response_(pca_response),
      is_user_specific_(is_user_specific),
      username_(username),
      key_name_(key_name) {
}

FinishCertRequestTask::~FinishCertRequestTask() {}

void FinishCertRequestTask::Run() {
  result()->set_return_status(FALSE);
  if (attestation_) {
    SecureBlob cert;
    bool status = attestation_->FinishCertRequest(pca_response_,
                                                  is_user_specific_,
                                                  username_,
                                                  key_name_,
                                                  &cert);
    result()->set_return_status(status);
    result()->set_return_data(cert);
  }
  Notify();
}

SignChallengeTask::SignChallengeTask(AttestationTaskObserver* observer,
                                     Attestation* attestation,
                                     bool is_user_specific,
                                     const std::string& username,
                                     const std::string& key_name,
                                     const SecureBlob& challenge,
                                     int sequence_id)
    : AttestationTask(observer, attestation, sequence_id),
      challenge_type_(kSimpleChallengeType),
      is_user_specific_(is_user_specific),
      username_(username),
      key_name_(key_name),
      challenge_(challenge) {
}

SignChallengeTask::SignChallengeTask(AttestationTaskObserver* observer,
                                     Attestation* attestation,
                                     Attestation::VAType va_type,
                                     bool is_user_specific,
                                     const std::string& username,
                                     const std::string& key_name,
                                     const std::string& domain,
                                     const SecureBlob& device_id,
                                     bool include_signed_public_key,
                                     const SecureBlob& challenge,
                                     const std::string& key_name_for_spkac,
                                     int sequence_id)
    : AttestationTask(observer, attestation, sequence_id),
      challenge_type_(kEnterpriseVaChallengeType),
      va_type_(va_type),
      is_user_specific_(is_user_specific),
      username_(username),
      key_name_(key_name),
      domain_(domain),
      device_id_(device_id),
      include_signed_public_key_(include_signed_public_key),
      challenge_(challenge),
      key_name_for_spkac_(key_name_for_spkac) {}

SignChallengeTask::~SignChallengeTask() {}

void SignChallengeTask::Run() {
  result()->set_return_status(FALSE);
  if (!attestation_) {
    Notify();
    return;
  }
  SecureBlob response;
  bool status = false;
  switch (challenge_type_) {
    case kSimpleChallengeType:
      status = attestation_->SignSimpleChallenge(is_user_specific_,
                                                 username_,
                                                 key_name_,
                                                 challenge_,
                                                 &response);
      break;
    case kEnterpriseChallengeType:
      status = attestation_->SignEnterpriseChallenge(is_user_specific_,
                                                     username_,
                                                     key_name_,
                                                     domain_,
                                                     device_id_,
                                                     include_signed_public_key_,
                                                     challenge_,
                                                     &response);
      break;
    case kEnterpriseVaChallengeType:
      status = attestation_->SignEnterpriseVaChallenge(
          va_type_,
          is_user_specific_,
          username_,
          key_name_,
          domain_,
          device_id_,
          include_signed_public_key_,
          challenge_,
          key_name_for_spkac_,
          &response);
      break;
  }
  result()->set_return_status(status);
  result()->set_return_data(response);
  Notify();
}

RegisterKeyTask::RegisterKeyTask(AttestationTaskObserver* observer,
                                 Attestation* attestation,
                                 bool is_user_specific,
                                 const std::string& username,
                                 const std::string& key_name,
                                 int sequence_id)
    : AttestationTask(observer, attestation, sequence_id),
      is_user_specific_(is_user_specific),
      username_(username),
      key_name_(key_name) {
}

RegisterKeyTask::~RegisterKeyTask() {}

void RegisterKeyTask::Run() {
  result()->set_return_status(FALSE);
  if (attestation_) {
    bool status = attestation_->RegisterKey(is_user_specific_,
                                            username_,
                                            key_name_,
                                            false);  // include_certificates
    result()->set_return_status(status);
  }
  Notify();
}

}  // namespace cryptohome
