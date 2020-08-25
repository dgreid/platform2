// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/client/dbus_proxy.h"

#include <base/bind.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <dbus/attestation/dbus-constants.h>

namespace {

// Use a two minute timeout because TPM operations can take a long time and
// there may be a few of them queued up.
const int kDBusTimeoutMS = 120000;

}  // namespace

namespace attestation {

DBusProxy::DBusProxy() {}
DBusProxy::~DBusProxy() {
  if (bus_) {
    bus_->ShutdownAndBlock();
  }
}

bool DBusProxy::Initialize() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(options);
  object_proxy_ = bus_->GetObjectProxy(
      attestation::kAttestationServiceName,
      dbus::ObjectPath(attestation::kAttestationServicePath));
  return (object_proxy_ != nullptr);
}

void DBusProxy::GetKeyInfo(const GetKeyInfoRequest& request,
                           const GetKeyInfoCallback& callback) {
  auto on_error = [](const GetKeyInfoCallback& callback, brillo::Error* error) {
    GetKeyInfoReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kGetKeyInfo, callback, base::Bind(on_error, callback),
      request);
}

void DBusProxy::GetEndorsementInfo(const GetEndorsementInfoRequest& request,
                                   const GetEndorsementInfoCallback& callback) {
  auto on_error = [](const GetEndorsementInfoCallback& callback,
                     brillo::Error* error) {
    GetEndorsementInfoReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kGetEndorsementInfo, callback,
      base::Bind(on_error, callback), request);
}

void DBusProxy::GetAttestationKeyInfo(
    const GetAttestationKeyInfoRequest& request,
    const GetAttestationKeyInfoCallback& callback) {
  auto on_error = [](const GetAttestationKeyInfoCallback& callback,
                     brillo::Error* error) {
    GetAttestationKeyInfoReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kGetAttestationKeyInfo, callback,
      base::Bind(on_error, callback), request);
}

void DBusProxy::ActivateAttestationKey(
    const ActivateAttestationKeyRequest& request,
    const ActivateAttestationKeyCallback& callback) {
  auto on_error = [](const ActivateAttestationKeyCallback& callback,
                     brillo::Error* error) {
    ActivateAttestationKeyReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kActivateAttestationKey, callback,
      base::Bind(on_error, callback), request);
}

void DBusProxy::CreateCertifiableKey(
    const CreateCertifiableKeyRequest& request,
    const CreateCertifiableKeyCallback& callback) {
  auto on_error = [](const CreateCertifiableKeyCallback& callback,
                     brillo::Error* error) {
    CreateCertifiableKeyReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kCreateCertifiableKey, callback,
      base::Bind(on_error, callback), request);
}

void DBusProxy::Decrypt(const DecryptRequest& request,
                        const DecryptCallback& callback) {
  auto on_error = [](const DecryptCallback& callback, brillo::Error* error) {
    DecryptReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kDecrypt, callback, base::Bind(on_error, callback), request);
}

void DBusProxy::Sign(const SignRequest& request, const SignCallback& callback) {
  auto on_error = [](const SignCallback& callback, brillo::Error* error) {
    SignReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kSign, callback, base::Bind(on_error, callback), request);
}

void DBusProxy::RegisterKeyWithChapsToken(
    const RegisterKeyWithChapsTokenRequest& request,
    const RegisterKeyWithChapsTokenCallback& callback) {
  auto on_error = [](const RegisterKeyWithChapsTokenCallback& callback,
                     brillo::Error* error) {
    RegisterKeyWithChapsTokenReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kRegisterKeyWithChapsToken, callback,
      base::Bind(on_error, callback), request);
}

void DBusProxy::GetEnrollmentPreparations(
    const GetEnrollmentPreparationsRequest& request,
    const GetEnrollmentPreparationsCallback& callback) {
  auto on_error = [](const GetEnrollmentPreparationsCallback& callback,
                     brillo::Error* error) {
    GetEnrollmentPreparationsReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kGetEnrollmentPreparations, callback,
      base::Bind(on_error, callback), request);
}

void DBusProxy::GetStatus(const GetStatusRequest& request,
                          const GetStatusCallback& callback) {
  auto on_error = [](const GetStatusCallback& callback, brillo::Error* error) {
    GetStatusReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kGetStatus, callback, base::Bind(on_error, callback),
      request);
}

void DBusProxy::Verify(const VerifyRequest& request,
                       const VerifyCallback& callback) {
  auto on_error = [](const VerifyCallback& callback, brillo::Error* error) {
    VerifyReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kVerify, callback, base::Bind(on_error, callback), request);
}

void DBusProxy::CreateEnrollRequest(
    const CreateEnrollRequestRequest& request,
    const CreateEnrollRequestCallback& callback) {
  auto on_error = [](const CreateEnrollRequestCallback& callback,
                     brillo::Error* error) {
    CreateEnrollRequestReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kCreateEnrollRequest, callback,
      base::Bind(on_error, callback), request);
}

void DBusProxy::FinishEnroll(const FinishEnrollRequest& request,
                             const FinishEnrollCallback& callback) {
  auto on_error = [](const FinishEnrollCallback& callback,
                     brillo::Error* error) {
    FinishEnrollReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kFinishEnroll, callback, base::Bind(on_error, callback),
      request);
}

void DBusProxy::Enroll(const EnrollRequest& request,
                       const EnrollCallback& callback) {
  auto on_error = [](const EnrollCallback& callback, brillo::Error* error) {
    EnrollReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kEnroll, callback, base::Bind(on_error, callback), request);
}

void DBusProxy::CreateCertificateRequest(
    const CreateCertificateRequestRequest& request,
    const CreateCertificateRequestCallback& callback) {
  auto on_error = [](const CreateCertificateRequestCallback& callback,
                     brillo::Error* error) {
    CreateCertificateRequestReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kCreateCertificateRequest, callback,
      base::Bind(on_error, callback), request);
}

void DBusProxy::FinishCertificateRequest(
    const FinishCertificateRequestRequest& request,
    const FinishCertificateRequestCallback& callback) {
  auto on_error = [](const FinishCertificateRequestCallback& callback,
                     brillo::Error* error) {
    FinishCertificateRequestReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kFinishCertificateRequest, callback,
      base::Bind(on_error, callback), request);
}

void DBusProxy::GetCertificate(const GetCertificateRequest& request,
                               const GetCertificateCallback& callback) {
  auto on_error = [](const GetCertificateCallback& callback,
                     brillo::Error* error) {
    GetCertificateReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kGetCertificate, callback, base::Bind(on_error, callback),
      request);
}

void DBusProxy::SignEnterpriseChallenge(
    const SignEnterpriseChallengeRequest& request,
    const SignEnterpriseChallengeCallback& callback) {
  auto on_error = [](const SignEnterpriseChallengeCallback& callback,
                     brillo::Error* error) {
    SignEnterpriseChallengeReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kSignEnterpriseChallenge, callback,
      base::Bind(on_error, callback), request);
}

void DBusProxy::SignSimpleChallenge(
    const SignSimpleChallengeRequest& request,
    const SignSimpleChallengeCallback& callback) {
  auto on_error = [](const SignSimpleChallengeCallback& callback,
                     brillo::Error* error) {
    SignSimpleChallengeReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kSignSimpleChallenge, callback,
      base::Bind(on_error, callback), request);
}

void DBusProxy::SetKeyPayload(const SetKeyPayloadRequest& request,
                              const SetKeyPayloadCallback& callback) {
  auto on_error = [](const SetKeyPayloadCallback& callback,
                     brillo::Error* error) {
    SetKeyPayloadReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kSetKeyPayload, callback, base::Bind(on_error, callback),
      request);
}

void DBusProxy::DeleteKeys(const DeleteKeysRequest& request,
                           const DeleteKeysCallback& callback) {
  auto on_error = [](const DeleteKeysCallback& callback, brillo::Error* error) {
    DeleteKeysReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kDeleteKeys, callback, base::Bind(on_error, callback),
      request);
}

void DBusProxy::ResetIdentity(const ResetIdentityRequest& request,
                              const ResetIdentityCallback& callback) {
  auto on_error = [](const ResetIdentityCallback& callback,
                     brillo::Error* error) {
    ResetIdentityReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kResetIdentity, callback, base::Bind(on_error, callback),
      request);
}

void DBusProxy::GetEnrollmentId(const GetEnrollmentIdRequest& request,
                                const GetEnrollmentIdCallback& callback) {
  auto on_error = [](const GetEnrollmentIdCallback& callback,
                     brillo::Error* error) {
    GetEnrollmentIdReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kGetEnrollmentId, callback, base::Bind(on_error, callback),
      request);
}

void DBusProxy::GetCertifiedNvIndex(
    const GetCertifiedNvIndexRequest& request,
    const GetCertifiedNvIndexCallback& callback) {
  auto on_error = [](const GetCertifiedNvIndexCallback& callback,
                     brillo::Error* error) {
    GetCertifiedNvIndexReply reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, attestation::kAttestationInterface,
      attestation::kGetCertifiedNvIndex, callback,
      base::Bind(on_error, callback), request);
}

}  // namespace attestation
