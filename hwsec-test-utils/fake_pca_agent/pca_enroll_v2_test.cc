// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/fake_pca_agent/pca_enroll_v2.h"

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace hwsec_test_utils {

class PcaEnrollV2Test : public testing::Test {
 public:
  PcaEnrollV2Test() {
    pca_enroll_ = std::make_unique<fake_pca_agent::PcaEnrollV2>(request_);
  }
  ~PcaEnrollV2Test() override = default;

 protected:
  attestation::AttestationEnrollmentRequest request_;
  std::unique_ptr<fake_pca_agent::PcaEnrollV2> pca_enroll_;
};

TEST_F(PcaEnrollV2Test, Enroll) {
  EXPECT_FALSE(pca_enroll_->Preprocess());
  EXPECT_FALSE(pca_enroll_->Verify());
  EXPECT_FALSE(pca_enroll_->Generate());
}

}  // namespace hwsec_test_utils
