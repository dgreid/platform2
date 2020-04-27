// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/verified_access/verified_access.h"

namespace hwsec_test_utils {
namespace verified_access {

base::Optional<attestation::SignedData>
VerifiedAccessChallenge::GenerateChallenge(const std::string& prefix) {
  // Not implemented.
  return {};
}

bool VerifiedAccessChallenge::VerifyChallengeResponse(
    const attestation::SignedData& signed_challenge_response,
    const std::string& prefix) {
  // Not implemented.
  return false;
}

}  // namespace verified_access
}  // namespace hwsec_test_utils
