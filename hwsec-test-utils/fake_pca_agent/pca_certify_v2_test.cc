// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/fake_pca_agent/pca_certify_v2.h"

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace hwsec_test_utils {

class PcaCertifyV2Test : public testing::Test {
 public:
  PcaCertifyV2Test() {
    pca_certify_ = std::make_unique<fake_pca_agent::PcaCertifyV2>(request_);
  }
  ~PcaCertifyV2Test() override = default;

 protected:
  attestation::AttestationCertificateRequest request_;
  std::unique_ptr<fake_pca_agent::PcaCertifyV2> pca_certify_;
};

TEST_F(PcaCertifyV2Test, Certify) {
  EXPECT_FALSE(pca_certify_->Preprocess());
  EXPECT_FALSE(pca_certify_->Verify());
  EXPECT_FALSE(pca_certify_->Generate());
}

}  // namespace hwsec_test_utils
