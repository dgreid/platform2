// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/verified_access/verified_access.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace hwsec_test_utils {

class VerifiedAccessChallengeTest : public testing::Test {
 public:
  VerifiedAccessChallengeTest() = default;
  ~VerifiedAccessChallengeTest() override = default;

 protected:
  verified_access::VerifiedAccessChallenge va_challenge_;
};

TEST_F(VerifiedAccessChallengeTest, GenerateChallenge) {
  EXPECT_FALSE(va_challenge_.GenerateChallenge("prefix").has_value());
}

TEST_F(VerifiedAccessChallengeTest, VerifyChallengeResponse) {
  EXPECT_FALSE(va_challenge_.VerifyChallengeResponse(attestation::SignedData(),
                                                     "prefix"));
}

}  // namespace hwsec_test_utils
