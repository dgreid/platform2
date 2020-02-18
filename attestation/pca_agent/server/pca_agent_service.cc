// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/pca_agent/server/pca_agent_service.h"

#include <string>
#include <utility>

#include <attestation/proto_bindings/interface.pb.h>
#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/memory/ref_counted.h>
#include <brillo/http/http_utils.h>
#include <brillo/mime_utils.h>

#include "attestation/pca_agent/server/pca_response_handler.h"

namespace attestation {
namespace pca_agent {

namespace {

constexpr char kDefaultPCAServerUrl[] = "https://chromeos-ca.gstatic.com";
constexpr char kTestPCAServerUrl[] = "https://asbestos-qa.corp.google.com";

constexpr char kEnrollPath[] = "enroll";
constexpr char kSignPath[] = "sign";

std::string ACATypeToServerUrl(ACAType type) {
  if (type == TEST_ACA) {
    return kTestPCAServerUrl;
  }
  return kDefaultPCAServerUrl;
}

std::string EnrollRequestToServerUrl(const EnrollRequest& req) {
  return ACATypeToServerUrl(req.aca_type()) + "/" + kEnrollPath;
}

std::string CertRequestToServerUrl(const GetCertificateRequest& req) {
  return ACATypeToServerUrl(req.aca_type()) + "/" + kSignPath;
}

}  // namespace

PcaAgentService::PcaAgentService()
    : transport_(brillo::http::Transport::CreateDefault()) {}

void PcaAgentService::Enroll(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<EnrollReply>>
        response,
    const EnrollRequest& request) {
  VLOG(1) << __func__;
  const std::string url = EnrollRequestToServerUrl(request);

  auto pca_response_handler = scoped_refptr<PcaResponseHandler<EnrollReply>>(
      new PcaResponseHandler<EnrollReply>(__func__, std::move(response)));
  // Ignores the request id.
  brillo::http::PostText(url, request.request(),
                         brillo::mime::application::kOctet_stream, {},
                         transport_,
                         base::Bind(&PcaResponseHandler<EnrollReply>::OnSuccess,
                                    base::RetainedRef(pca_response_handler)),
                         base::Bind(&PcaResponseHandler<EnrollReply>::OnError,
                                    base::RetainedRef(pca_response_handler)));
}

void PcaAgentService::GetCertificate(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<GetCertificateReply>>
        response,
    const GetCertificateRequest& request) {
  VLOG(1) << __func__;
  const std::string url = CertRequestToServerUrl(request);

  auto pca_response_handler =
      scoped_refptr<PcaResponseHandler<GetCertificateReply>>(
          new PcaResponseHandler<GetCertificateReply>(__func__,
                                                      std::move(response)));
  // Ignores the request id.
  brillo::http::PostText(
      url, request.request(), brillo::mime::application::kOctet_stream, {},
      transport_,
      base::Bind(&PcaResponseHandler<GetCertificateReply>::OnSuccess,
                 base::RetainedRef(pca_response_handler)),
      base::Bind(&PcaResponseHandler<GetCertificateReply>::OnError,
                 base::RetainedRef(pca_response_handler)));
}

}  // namespace pca_agent
}  // namespace attestation
