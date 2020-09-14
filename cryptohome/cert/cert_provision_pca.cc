// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Utility classes for cert_provision library.

#include <brillo/mime_utils.h>

#include "cryptohome/cert/cert_provision_pca.h"

namespace cert_provision {

PCAProxy* PCAProxy::subst_obj = nullptr;

Scoped<PCAProxy> PCAProxy::Create(const std::string& pca_url) {
  return subst_obj ? Scoped<PCAProxy>(subst_obj)
                   : Scoped<PCAProxy>(GetDefault(pca_url));
}

std::unique_ptr<PCAProxy> PCAProxy::GetDefault(const std::string& pca_url) {
  return std::unique_ptr<PCAProxy>(new PCAProxyImpl(pca_url));
}

PCAProxyImpl::PCAProxyImpl(const std::string& pca_url)
    : PCAProxy(pca_url),
      http_transport_(brillo::http::Transport::CreateDefault()) {}

OpResult PCAProxyImpl::MakeRequest(const std::string& action,
                                   const brillo::SecureBlob& request,
                                   brillo::SecureBlob* reply) {
  brillo::ErrorPtr error;
  auto response = brillo::http::PostBinaryAndBlock(
      pca_url_ + "/" + action, request.data(), request.size(),
      brillo::mime::application::kOctet_stream, {},  // headers
      http_transport_, &error);
  if (!response) {
    return {Status::HttpError, std::string("Sending PCA request failed: ") +
                                   action + ": " + error->GetMessage()};
  }
  if (!response->IsSuccessful()) {
    return {Status::ServerError, std::string("PCA server error: ") + action +
                                     ": " + response->GetStatusText()};
  }
  auto response_data = response->ExtractData();
  brillo::SecureBlob tmp(response_data.begin(), response_data.end());
  reply->swap(tmp);
  return OpResult();
}

}  // namespace cert_provision
