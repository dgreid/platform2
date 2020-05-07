// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/fake_pca_agent/pca_factory.h"

#include <memory>
#include <utility>

#if USE_TPM2
#include "hwsec-test-utils/fake_pca_agent/pca_certify_v2.h"
#include "hwsec-test-utils/fake_pca_agent/pca_enroll_v2.h"
#else
#include "hwsec-test-utils/fake_pca_agent/pca_certify_v1.h"
#include "hwsec-test-utils/fake_pca_agent/pca_enroll_v1.h"
#endif

namespace hwsec_test_utils {
namespace fake_pca_agent {

std::unique_ptr<PcaBase<attestation::AttestationEnrollmentRequest,
                        attestation::AttestationEnrollmentResponse>>
CreatePcaEnroll(attestation::AttestationEnrollmentRequest request) {
#if USE_TPM2
  return std::make_unique<PcaEnrollV2>(std::move(request));
#else
  return std::make_unique<PcaEnrollV1>(std::move(request));
#endif
}

std::unique_ptr<PcaBase<attestation::AttestationCertificateRequest,
                        attestation::AttestationCertificateResponse>>
CreatePcaCertify(attestation::AttestationCertificateRequest request) {
#if USE_TPM2
  return std::make_unique<PcaCertifyV2>(std::move(request));
#else
  return std::make_unique<PcaCertifyV1>(std::move(request));
#endif
}

}  // namespace fake_pca_agent
}  // namespace hwsec_test_utils
