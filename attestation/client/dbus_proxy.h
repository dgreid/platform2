// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_CLIENT_DBUS_PROXY_H_
#define ATTESTATION_CLIENT_DBUS_PROXY_H_

#include "attestation/common/attestation_interface.h"

#include <string>

#include <base/memory/ref_counted.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>

namespace attestation {

// An implementation of AttestationInterface that forwards requests over D-Bus.
// Usage:
//   std::unique_ptr<AttestationInterface> attestation = new DBusProxy();
//   attestation->Initialize();
//   attestation->GetEndorsementInfo(...);
class DBusProxy : public AttestationInterface {
 public:
  DBusProxy();
  ~DBusProxy() override;

  // AttestationInterface methods.
  bool Initialize() override;
  void GetKeyInfo(const GetKeyInfoRequest& request,
                  const GetKeyInfoCallback& callback) override;
  void GetEndorsementInfo(const GetEndorsementInfoRequest& request,
                          const GetEndorsementInfoCallback& callback) override;
  void GetAttestationKeyInfo(
      const GetAttestationKeyInfoRequest& request,
      const GetAttestationKeyInfoCallback& callback) override;
  void ActivateAttestationKey(
      const ActivateAttestationKeyRequest& request,
      const ActivateAttestationKeyCallback& callback) override;
  void CreateCertifiableKey(
      const CreateCertifiableKeyRequest& request,
      const CreateCertifiableKeyCallback& callback) override;
  void Decrypt(const DecryptRequest& request,
               const DecryptCallback& callback) override;
  void Sign(const SignRequest& request, const SignCallback& callback) override;
  void RegisterKeyWithChapsToken(
      const RegisterKeyWithChapsTokenRequest& request,
      const RegisterKeyWithChapsTokenCallback& callback) override;
  void GetEnrollmentPreparations(
      const GetEnrollmentPreparationsRequest& request,
      const GetEnrollmentPreparationsCallback& callback) override;
  void GetStatus(const GetStatusRequest& request,
                 const GetStatusCallback& callback) override;
  void Verify(const VerifyRequest& request,
              const VerifyCallback& callback) override;
  void CreateEnrollRequest(
      const CreateEnrollRequestRequest& request,
      const CreateEnrollRequestCallback& callback) override;
  void FinishEnroll(const FinishEnrollRequest& request,
                    const FinishEnrollCallback& callback) override;
  void Enroll(const EnrollRequest& request,
              const EnrollCallback& callback) override;
  void CreateCertificateRequest(
      const CreateCertificateRequestRequest& request,
      const CreateCertificateRequestCallback& callback) override;
  void FinishCertificateRequest(
      const FinishCertificateRequestRequest& request,
      const FinishCertificateRequestCallback& callback) override;
  void GetCertificate(const GetCertificateRequest& request,
                      const GetCertificateCallback& callback) override;
  void SignEnterpriseChallenge(
      const SignEnterpriseChallengeRequest& request,
      const SignEnterpriseChallengeCallback& callback) override;
  void SignSimpleChallenge(
      const SignSimpleChallengeRequest& request,
      const SignSimpleChallengeCallback& callback) override;
  void SetKeyPayload(const SetKeyPayloadRequest& request,
                     const SetKeyPayloadCallback& callback) override;
  void DeleteKeys(const DeleteKeysRequest& request,
                  const DeleteKeysCallback& callback) override;
  void ResetIdentity(const ResetIdentityRequest& request,
                     const ResetIdentityCallback& callback) override;
  void GetEnrollmentId(const GetEnrollmentIdRequest& request,
                       const GetEnrollmentIdCallback& callback) override;
  void GetCertifiedNvIndex(
      const GetCertifiedNvIndexRequest& request,
      const GetCertifiedNvIndexCallback& callback) override;

  // Useful for testing.
  void set_object_proxy(dbus::ObjectProxy* object_proxy) {
    object_proxy_ = object_proxy;
  }

 private:
  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* object_proxy_;
};

}  // namespace attestation

#endif  // ATTESTATION_CLIENT_DBUS_PROXY_H_
