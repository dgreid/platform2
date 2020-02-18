// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/pca_agent/server/pca_response_handler.h"

namespace attestation {
namespace pca_agent {

template <typename ReplyType>
PcaResponseHandler<ReplyType>::PcaResponseHandler(
    const std::string& name,
    std::unique_ptr<DBusResponseType> response)
    : name_(name), response_(std::move(response)) {}

template <typename ReplyType>
void PcaResponseHandler<ReplyType>::OnError(
    brillo::http::RequestID /*not used*/,
    const brillo::Error* err) {
  ReplyType reply;
  LOG(ERROR) << name_
             << ": Failed to talk to PCA server: " << err->GetMessage();
  reply.set_status(STATUS_CA_NOT_AVAILABLE);
  response_->Return(reply);
}

template <typename ReplyType>
void PcaResponseHandler<ReplyType>::OnSuccess(
    brillo::http::RequestID,
    std::unique_ptr<brillo::http::Response> pca_response) {
  ReplyType reply;
  if (pca_response->IsSuccessful()) {
    if (pca_response->GetStatusCode() == 200) {
      reply.set_status(STATUS_SUCCESS);
      *reply.mutable_response() = pca_response->ExtractDataAsString();
    } else {
      LOG(ERROR) << name_
                 << ": |pca_agent| doesn't support any other status code other "
                    "than 200 even if it's a successful call. Status code = "
                 << pca_response->GetStatusCode();
      reply.set_status(STATUS_NOT_SUPPORTED);
    }
  } else {
    LOG(ERROR) << name_ << " Bad response code from CA: "
               << pca_response->GetStatusCode();
    reply.set_status(STATUS_REQUEST_DENIED_BY_CA);
  }
  response_->Return(reply);
}

// Explicit instantiation.
template class PcaResponseHandler<EnrollReply>;
template class PcaResponseHandler<GetCertificateReply>;

}  // namespace pca_agent
}  // namespace attestation
