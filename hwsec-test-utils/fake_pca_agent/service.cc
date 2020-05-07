// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/fake_pca_agent/service.h"

#include <attestation/proto_bindings/pca_agent.pb.h>

namespace hwsec_test_utils {
namespace fake_pca_agent {

FakePcaAgentService::FakePcaAgentService() = default;

void FakePcaAgentService::Enroll(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        attestation::pca_agent::EnrollReply>> response,
    const attestation::pca_agent::EnrollRequest& in_request) {
  response->Return({});
}

void FakePcaAgentService::GetCertificate(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        attestation::pca_agent::GetCertificateReply>> response,
    const attestation::pca_agent::GetCertificateRequest& in_request) {
  response->Return({});
}

}  // namespace fake_pca_agent
}  // namespace hwsec_test_utils
