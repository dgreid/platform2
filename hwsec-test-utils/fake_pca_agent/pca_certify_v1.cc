// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/fake_pca_agent/pca_certify_v1.h"

namespace hwsec_test_utils {
namespace fake_pca_agent {

bool PcaCertifyV1::Preprocess() {
  return false;
}

bool PcaCertifyV1::Verify() {
  return false;
}

bool PcaCertifyV1::Generate() {
  return false;
}

bool PcaCertifyV1::Write(
    attestation::AttestationCertificateResponse* response) {
  return false;
}

}  // namespace fake_pca_agent
}  // namespace hwsec_test_utils
