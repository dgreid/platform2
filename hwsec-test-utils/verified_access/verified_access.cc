// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/verified_access/verified_access.h"

#include <string>
#include <utility>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <base/optional.h>
#include <crypto/scoped_openssl_types.h>

#include "hwsec-test-utils/common/openssl_utility.h"
#include "hwsec-test-utils/well_known_key_pairs/well_known_key_pairs.h"

namespace hwsec_test_utils {
namespace verified_access {

namespace {

constexpr int kNonceSize = 20;

base::Optional<std::string> GenerateNonce() {
  return GetRandom(kNonceSize);
}

}  // namespace

base::Optional<attestation::SignedData>
VerifiedAccessChallenge::GenerateChallenge(const std::string& prefix) {
  // Generate the data to sign, including the prefix and a nonce.
  attestation::Challenge challenge;
  challenge.set_prefix(prefix);
  base::Optional<std::string> nonce = GenerateNonce();
  if (!nonce) {
    LOG(WARNING) << __func__ << ": Failed to generate nonce.";
    return {};
  }
  challenge.set_nonce(*nonce);
  std::string serialized_challenge;
  if (!challenge.SerializeToString(&serialized_challenge)) {
    LOG(ERROR) << __func__ << ": Failed to serialize challenge.";
    return {};
  }

  // Construct the return value, including the data and its signature.
  attestation::SignedData signed_data;
  signed_data.set_data(serialized_challenge);
  crypto::ScopedEVP_PKEY key = well_known_key_pairs::GetVaSigningkey();
  if (!key) {
    LOG(ERROR) << __func__ << ": Failed get the va signing key.";
    return {};
  }
  base::Optional<std::string> signature =
      EVPDigestSign(key, EVP_sha256(), serialized_challenge);
  if (!signature) {
    LOG(ERROR) << __func__ << ": Failed to sign the generated challenge.";
  }
  *signed_data.mutable_signature() = std::move(*signature);
  return signed_data;
}

bool VerifiedAccessChallenge::VerifyChallengeResponse(
    const attestation::SignedData& signed_challenge_response,
    const std::string& prefix) {
  // Not implemented.
  return false;
}

}  // namespace verified_access
}  // namespace hwsec_test_utils
