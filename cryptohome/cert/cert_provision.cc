// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Library that provides certificate provisioning/signing interface.

#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>

#include "cert/cert_provision.pb.h"
#include "cryptohome/cert/cert_provision_cryptohome.h"
#include "cryptohome/cert/cert_provision_keystore.h"
#include "cryptohome/cert/cert_provision_pca.h"
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

const char kGetCertAction[] = "sign";
const char kEnrollAction[] = "enroll";

const char kEndCertificate[] = "-----END CERTIFICATE-----";

std::string GetDefaultPCAUrl(cert_provision::PCAType pca_type) {
  std::string url;
  switch (pca_type) {
    case cert_provision::PCAType::kDefaultPCA:
      url = "https://chromeos-ca.gstatic.com";
      break;
    case cert_provision::PCAType::kTestPCA:
      url = "https://asbestos-qa.corp.google.com";
      break;
    default:
      break;
  }
  return url;
}

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

Status ProvisionCertificate(PCAType pca_type,
                            const std::string& pca_url,
                            const std::string& label,
                            CertificateProfile cert_profile,
                            const ProgressCallback& progress_callback) {
  ProgressReporter reporter(progress_callback, kMaxSteps);
  std::string url(pca_url);
  if (url.empty()) {
    url = GetDefaultPCAUrl(pca_type);
    if (url.empty()) {
      return reporter.ReportAndReturn(Status::HttpError,
                                      "PCA url is not defined.");
    }
  }
  auto pca_proxy = PCAProxy::Create(url);
  auto c_proxy = CryptohomeProxy::Create();

  OpResult result = c_proxy->Init();
  if (!result) {
    return reporter.ReportAndReturn(result);
  }

  reporter.Step("Checking if enrolled");
  bool is_enrolled;
  result = c_proxy->CheckIfEnrolled(&is_enrolled);
  if (!result) {
    return reporter.ReportAndReturn(result);
  }

  if (is_enrolled) {
    reporter.SetSteps(kNoEnrollSteps);
  } else {
    reporter.Step("Checking if ready for enrollment");
    bool is_prepared;
    result = c_proxy->CheckIfPrepared(&is_prepared);
    if (!result) {
      return reporter.ReportAndReturn(result);
    }
    if (!is_prepared) {
      return reporter.ReportAndReturn(Status::NotPrepared,
                                      "Not ready for enrollment.");
    }

    reporter.Step("Creating enroll request");
    brillo::SecureBlob request;
    result = c_proxy->CreateEnrollRequest(pca_type, &request);
    if (!result) {
      return reporter.ReportAndReturn(result);
    }
    reporter.Step("Sending enroll request");
    brillo::SecureBlob response;
    result = pca_proxy->MakeRequest(kEnrollAction, request, &response);
    if (!result) {
      return reporter.ReportAndReturn(result);
    }
    reporter.Step("Processing enroll response");
    result = c_proxy->ProcessEnrollResponse(pca_type, response);
    if (!result) {
      return reporter.ReportAndReturn(result);
    }
  }

  brillo::SecureBlob cert_chain;
  reporter.Step("Creating certificate request");
  brillo::SecureBlob request;
  result = c_proxy->CreateCertRequest(pca_type, cert_profile, &request);
  if (!result) {
    return reporter.ReportAndReturn(result);
  }
  reporter.Step("Sending certificate request");
  brillo::SecureBlob response;
  result = pca_proxy->MakeRequest(kGetCertAction, request, &response);
  if (!result) {
    return reporter.ReportAndReturn(result);
  }
  reporter.Step("Processing certificate response");
  result = c_proxy->ProcessCertResponse(label, response, &cert_chain);
  if (!result) {
    return reporter.ReportAndReturn(result);
  }

  reporter.Step("Registering new keys");
  brillo::SecureBlob public_key;
  result = c_proxy->GetPublicKey(label, &public_key);
  if (!result) {
    return reporter.ReportAndReturn(result);
  }
  std::string key_id = GetKeyID(public_key);
  if (key_id.empty()) {
    return reporter.ReportAndReturn(Status::KeyStoreError,
                                    "Failed to calculate key ID.");
  }
  VLOG(1) << "Obtained key id "
          << base::HexEncode(key_id.data(), key_id.size());

  result = c_proxy->Register(label);
  if (!result) {
    return reporter.ReportAndReturn(result);
  }

  reporter.Step("Updating provision status");
  auto key_store = KeyStore::Create();
  result = key_store->Init();
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

  provision_status.set_provisioned(true);
  provision_status.set_key_id(key_id);
  provision_status.set_certificate_chain(cert_chain.to_string());
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
                   const std::string& pca_url,
                   const ProgressCallback& progress_callback) {
  ProgressReporter reporter(progress_callback, kEnrollSteps);
  std::string url(pca_url);
  if (url.empty()) {
    url = GetDefaultPCAUrl(pca_type);
    if (url.empty()) {
      return reporter.ReportAndReturn(Status::HttpError,
                                      "PCA url is not defined.");
    }
  }
  auto pca_proxy = PCAProxy::Create(url);
  auto c_proxy = CryptohomeProxy::Create();

  OpResult result = c_proxy->Init();
  if (!result) {
    return reporter.ReportAndReturn(result);
  }

  reporter.Step("Checking if ready for enrollment");
  bool is_prepared;
  result = c_proxy->CheckIfPrepared(&is_prepared);
  if (!result) {
    return reporter.ReportAndReturn(result);
  }
  if (!is_prepared) {
    return reporter.ReportAndReturn(Status::NotPrepared,
                                    "Not ready for enrollment.");
  }

  reporter.Step("Creating enroll request");
  brillo::SecureBlob request;
  result = c_proxy->CreateEnrollRequest(pca_type, &request);
  if (!result) {
    return reporter.ReportAndReturn(result);
  }

  reporter.Step("Sending enroll request");
  brillo::SecureBlob response;
  result = pca_proxy->MakeRequest(kEnrollAction, request, &response);
  if (!result) {
    return reporter.ReportAndReturn(result);
  }

  reporter.Step("Processing enroll response");
  result = c_proxy->ProcessEnrollResponse(pca_type, response);
  if (!result) {
    return reporter.ReportAndReturn(result);
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
