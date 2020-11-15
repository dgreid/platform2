// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Library that provides certificate provisioning/signing interface.

#include <string>

// This group goes first so the next group can see the needed definitions.
#include <attestation/proto_bindings/interface.pb.h>

#include <attestation-client/attestation/dbus-proxies.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>

#include "cert/cert_provision.pb.h"
#include "cryptohome/cert/cert_provision_keystore.h"
#include "cryptohome/cert/cert_provision_util.h"
#include "cryptohome/cert_provision.h"

namespace {

// Number of steps for different provision stages.
constexpr int kInitSteps = 1;
constexpr int kGetCertSteps = 3;
constexpr int kRegisterSteps = 3;
constexpr int kNoEnrollSteps = kInitSteps + kGetCertSteps + kRegisterSteps;
constexpr int kEnrollSteps = 4;
constexpr int kMaxSteps = kNoEnrollSteps + kEnrollSteps;

constexpr base::TimeDelta kGetCertificateTimeout =
    base::TimeDelta::FromSeconds(80);
constexpr base::TimeDelta kEnrollTimeout = base::TimeDelta::FromSeconds(50);

const char kEndCertificate[] = "-----END CERTIFICATE-----";

cert_provision::Status ReportAndReturn(cert_provision::Status status,
                                       const std::string& message) {
  LOG(ERROR) << message;
  return status;
}

cert_provision::Status ReportAndReturn(const cert_provision::OpResult& result) {
  return ReportAndReturn(result.status, result.message);
}

}  // namespace

namespace cert_provision {

namespace {

::attestation::CertificateProfile ToAttestationCertProfile(
    CertificateProfile p) {
  // Enumerate all the valid conversion for better error-proof during compile
  // time.
  switch (p) {
    case ENTERPRISE_MACHINE_CERTIFICATE:
    case ENTERPRISE_USER_CERTIFICATE:
    case CONTENT_PROTECTION_CERTIFICATE:
    case CONTENT_PROTECTION_CERTIFICATE_WITH_STABLE_ID:
    case CAST_CERTIFICATE:
    case GFSC_CERTIFICATE:
    case JETSTREAM_CERTIFICATE:
    case ENTERPRISE_ENROLLMENT_CERTIFICATE:
    case XTS_CERTIFICATE:
    case ENTERPRISE_VTPM_EK_CERTIFICATE:
      return static_cast<::attestation::CertificateProfile>(p);
  }
  LOG(DFATAL) << "Unknown value of profile: " << p;
  return ::attestation::ENTERPRISE_MACHINE_CERTIFICATE;
}

::attestation::ACAType ToAttestationAcaType(PCAType pca_type) {
  switch (pca_type) {
    case kDefaultPCA:
      return ::attestation::DEFAULT_ACA;
    case kTestPCA:
      return ::attestation::TEST_ACA;
  }
  LOG(DFATAL) << "Unknown value of pca type: " << pca_type;
  return ::attestation::DEFAULT_ACA;
}

}  // namespace

Status ProvisionCertificate(PCAType pca_type,
                            const std::string& label,
                            CertificateProfile cert_profile,
                            const ProgressCallback& progress_callback) {
  return ProvisionCertificate(pca_type, /*pca_url=*/std::string(), label,
                              cert_profile, progress_callback);
}

Status ProvisionCertificate(PCAType pca_type,
                            const std::string& pca_url,
                            const std::string& label,
                            CertificateProfile cert_profile,
                            const ProgressCallback& progress_callback) {
  DCHECK(pca_url.empty()) << "The arbitrary pca server URL is not supported.";

  ProgressReporter reporter(progress_callback, kMaxSteps);
  auto proxy = AttestationProxyFactory::Create();
  // By design, the factory must return a valid object.
  CHECK(proxy);

  reporter.Step("Checking if ready for enrollment");
  ::attestation::GetStatusReply get_status_reply;
  brillo::ErrorPtr error;
  if (!proxy->GetStatus(::attestation::GetStatusRequest(), &get_status_reply,
                        &error)) {
    return reporter.ReportAndReturn(Status::DBusError, error->GetMessage());
  }
  if (get_status_reply.status() != ::attestation::STATUS_SUCCESS) {
    return reporter.ReportAndReturn(Status::AttestationError,
                                    "Failed to get attestation status.");
  }
  if (!get_status_reply.prepared_for_enrollment()) {
    return reporter.ReportAndReturn(Status::NotPrepared,
                                    "Not ready for enrollment.");
  }

  // The attestation is confirmed to be attestation prepared; get certificate.
  reporter.Step("Getting certificate");
  ::attestation::GetCertificateRequest request;
  request.set_aca_type(ToAttestationAcaType(pca_type));
  request.set_username("");
  request.set_key_label(label);
  request.set_forced(true);
  request.set_certificate_profile(ToAttestationCertProfile(cert_profile));
  request.set_request_origin("");
  request.set_shall_trigger_enrollment(true);

  ::attestation::GetCertificateReply reply;
  if (!proxy->GetCertificate(request, &reply, &error,
                             kGetCertificateTimeout.InMilliseconds())) {
    return reporter.ReportAndReturn(Status::DBusError, error->GetMessage());
  }
  if (reply.status() != ::attestation::STATUS_SUCCESS) {
    return reporter.ReportAndReturn(Status::AttestationError,
                                    "Failed to get cert.");
  }

  reporter.Step("Registering new keys");
  ::attestation::RegisterKeyWithChapsTokenRequest register_request;
  register_request.set_username("");
  register_request.set_key_label(label);
  ::attestation::RegisterKeyWithChapsTokenReply register_reply;
  if (!proxy->RegisterKeyWithChapsToken(register_request, &register_reply,
                                        &error)) {
    return reporter.ReportAndReturn(Status::DBusError, error->GetMessage());
  }
  if (register_reply.status() != ::attestation::STATUS_SUCCESS) {
    return reporter.ReportAndReturn(Status::AttestationError,
                                    "Failed to register key.");
  }

  reporter.Step("Updating provision status");
  auto key_store = KeyStore::Create();
  OpResult result = key_store->Init();
  if (!result) {
    return reporter.ReportAndReturn(result);
  }

  ProvisionStatus provision_status;
  result = key_store->ReadProvisionStatus(label, &provision_status);
  if (!result) {
    return reporter.ReportAndReturn(result);
  }

  std::string old_id;
  if (provision_status.provisioned()) {
    old_id = provision_status.key_id();
  }
  VLOG(1) << "Old key id " << base::HexEncode(old_id.data(), old_id.size());

  const std::string key_id = GetKeyID(brillo::SecureBlob(reply.public_key()));

  provision_status.set_provisioned(true);
  provision_status.set_key_id(key_id);
  provision_status.set_certificate_chain(reply.certificate());
  result = key_store->WriteProvisionStatus(label, provision_status);
  if (!result) {
    return reporter.ReportAndReturn(result);
  }

  reporter.Step("Deleting old keys");
  if (!old_id.empty() && (key_id != old_id) &&
      !(result = key_store->DeleteKeys(old_id, label))) {
    return reporter.ReportAndReturn(result);
  }

  reporter.Done();
  return Status::Success;
}

Status ForceEnroll(PCAType pca_type,
                   const ProgressCallback& progress_callback) {
  return ForceEnroll(pca_type, /*pca_url=*/std::string(), progress_callback);
}

Status ForceEnroll(PCAType pca_type,
                   const std::string& pca_url,
                   const ProgressCallback& progress_callback) {
  DCHECK(pca_url.empty()) << "The arbitrary pca server URL is not supported.";

  ProgressReporter reporter(progress_callback, kEnrollSteps);
  auto proxy = AttestationProxyFactory::Create();
  // By design, the factory must return a valid object.
  CHECK(proxy);

  reporter.Step("Checking if ready for enrollment");
  ::attestation::GetStatusReply get_status_reply;
  brillo::ErrorPtr error;
  if (!proxy->GetStatus(::attestation::GetStatusRequest(), &get_status_reply,
                        &error)) {
    return reporter.ReportAndReturn(Status::DBusError, error->GetMessage());
  }
  if (get_status_reply.status() != ::attestation::STATUS_SUCCESS) {
    return reporter.ReportAndReturn(Status::AttestationError,
                                    "Failed to get attestation status.");
  }
  if (!get_status_reply.prepared_for_enrollment()) {
    return reporter.ReportAndReturn(Status::NotPrepared,
                                    "Not ready for enrollment.");
  }

  // The attestation is confirmed to be attestation prepared; (re-)enroll the
  // device.
  reporter.Step("Enrolling");
  ::attestation::EnrollRequest request;
  request.set_aca_type(ToAttestationAcaType(pca_type));
  request.set_forced(true);
  ::attestation::EnrollReply reply;

  if (!proxy->Enroll(request, &reply, &error,
                     kEnrollTimeout.InMilliseconds())) {
    return reporter.ReportAndReturn(Status::DBusError, error->GetMessage());
  }
  if (reply.status() != ::attestation::STATUS_SUCCESS) {
    return reporter.ReportAndReturn(Status::AttestationError,
                                    "Failed to enroll.");
  }

  reporter.Done();
  return Status::Success;
}

Status GetCertificate(const std::string& label,
                      bool include_intermediate,
                      std::string* cert) {
  auto key_store = KeyStore::Create();
  ProvisionStatus provision_status;

  OpResult result = key_store->Init();
  if (!result) {
    return ReportAndReturn(result);
  }
  result = key_store->ReadProvisionStatus(label, &provision_status);
  if (!result) {
    return ReportAndReturn(result);
  }
  if (!provision_status.provisioned()) {
    return ReportAndReturn(Status::NotProvisioned, "Not provisioned");
  }

  size_t pos;
  if (include_intermediate) {
    pos = std::string::npos;
  } else {
    pos = provision_status.certificate_chain().find(kEndCertificate);
    if (pos != std::string::npos) {
      pos += base::size(kEndCertificate) - 1;
    }
  }
  cert->assign(provision_status.certificate_chain().substr(0, pos));

  return Status::Success;
}

Status Sign(const std::string& label,
            SignMechanism mechanism,
            const std::string& data,
            std::string* signature) {
  auto key_store = KeyStore::Create();
  ProvisionStatus provision_status;

  OpResult result = key_store->Init();
  if (!result) {
    return ReportAndReturn(result);
  }
  result = key_store->ReadProvisionStatus(label, &provision_status);
  if (!result) {
    return ReportAndReturn(result);
  }
  if (!provision_status.provisioned()) {
    return ReportAndReturn(Status::NotProvisioned, "Not provisioned");
  }
  VLOG(1) << "Signing with key id " << provision_status.key_id();
  result = key_store->Sign(provision_status.key_id(), label, mechanism, data,
                           signature);
  if (!result) {
    return ReportAndReturn(result);
  }
  return Status::Success;
}

}  // namespace cert_provision
