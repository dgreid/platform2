// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/pca_agent/server/pca_agent_service.h"

#include <attestation/proto_bindings/interface.pb.h>

namespace attestation {
namespace pca_agent {

void PcaAgentService::Enroll(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<EnrollReply>>
        response,
    const EnrollRequest& in_request) {
  EnrollReply reply;
  reply.set_status(STATUS_NOT_SUPPORTED);
  response->Return(reply);
}
void PcaAgentService::GetCertificate(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<GetCertificateReply>>
        response,
    const GetCertificateRequest& in_request) {
  GetCertificateReply reply;
  reply.set_status(STATUS_NOT_SUPPORTED);
  response->Return(reply);
}

}  // namespace pca_agent
}  // namespace attestation
