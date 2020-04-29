// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/verified_access/verified_access.h"

#include <base/optional.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hwsec-test-utils/common/openssl_utility.h"
#include "hwsec-test-utils/well_known_key_pairs/well_known_key_pairs.h"

namespace hwsec_test_utils {

class VerifiedAccessChallengeTest : public testing::Test {
 public:
  VerifiedAccessChallengeTest() = default;
  ~VerifiedAccessChallengeTest() override = default;

 protected:
  verified_access::VerifiedAccessChallenge va_challenge_;
};

TEST_F(VerifiedAccessChallengeTest, GenerateChallenge) {
  // Creates the output under test.
  constexpr char kExpectedPrefix[] = "prefix";
  base::Optional<attestation::SignedData> optional_signed_data =
      va_challenge_.GenerateChallenge(kExpectedPrefix);
  ASSERT_TRUE(optional_signed_data.has_value());
  const attestation::SignedData& signed_data = *optional_signed_data;
  const std::string serialized_challenge = signed_data.data();
  attestation::Challenge challenge;
  ASSERT_TRUE(challenge.ParseFromString(serialized_challenge));

  // Verify data.
  EXPECT_EQ(challenge.prefix(), std::string(kExpectedPrefix));
  EXPECT_FALSE(challenge.nonce().empty());

  // Verify signature.
  crypto::ScopedEVP_PKEY key = well_known_key_pairs::GetVaSigningkey();
  ASSERT_NE(key.get(), nullptr);
  EXPECT_TRUE(EVPDigestVerify(key, EVP_sha256(), signed_data.data(),
                              signed_data.signature()));
}

TEST_F(VerifiedAccessChallengeTest, VerifyChallengeResponse) {
  EXPECT_FALSE(va_challenge_.VerifyChallengeResponse(attestation::SignedData(),
                                                     "prefix"));
}

}  // namespace hwsec_test_utils
