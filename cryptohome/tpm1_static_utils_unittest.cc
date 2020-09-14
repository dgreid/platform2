// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <brillo/secure_blob.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <gtest/gtest.h>
#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>  // NOLINT(build/include_alpha) - needs tss.h

#include "cryptohome/cryptolib.h"
#include "cryptohome/tpm1_static_utils.h"

using brillo::Blob;

namespace cryptohome {

namespace {

// Creates the blob of the TPM_PUBKEY structure that holds information about the
// given RSA public key.
Blob BuildRsaTpmPubkeyBlob(const RSA& rsa) {
  Blob modulus(RSA_size(&rsa));
  const BIGNUM* n;
  RSA_get0_key(&rsa, &n, nullptr, nullptr);
  EXPECT_EQ(BN_bn2bin(n, modulus.data()), modulus.size());

  // Build the TPM_RSA_KEY_PARMS structure.
  TPM_RSA_KEY_PARMS rsa_key_parms;
  rsa_key_parms.keyLength = RSA_size(&rsa) * 8;
  rsa_key_parms.numPrimes = 2;
  // The default exponent is assumed.
  rsa_key_parms.exponentSize = 0;
  rsa_key_parms.exponent = nullptr;

  // Convert the TPM_RSA_KEY_PARMS structure into blob.
  UINT64 offset = 0;
  Trspi_LoadBlob_RSA_KEY_PARMS(&offset, nullptr, &rsa_key_parms);
  Blob rsa_key_parms_blob(offset);
  offset = 0;
  Trspi_LoadBlob_RSA_KEY_PARMS(&offset, rsa_key_parms_blob.data(),
                               &rsa_key_parms);
  EXPECT_EQ(offset, rsa_key_parms_blob.size());

  // Build the TPM_PUBKEY structure.
  TPM_PUBKEY pubkey;
  pubkey.algorithmParms.algorithmID = TPM_ALG_RSA;
  pubkey.algorithmParms.encScheme = 0;
  pubkey.algorithmParms.sigScheme = 0;
  pubkey.algorithmParms.parmSize = rsa_key_parms_blob.size();
  pubkey.algorithmParms.parms = rsa_key_parms_blob.data();
  pubkey.pubKey.keyLength = modulus.size();
  pubkey.pubKey.key = const_cast<BYTE*>(modulus.data());

  // Convert the TPM_PUBKEY structure blob into blob.
  offset = 0;
  Trspi_LoadBlob_PUBKEY(&offset, nullptr, &pubkey);
  Blob pubkey_blob(offset);
  offset = 0;
  Trspi_LoadBlob_PUBKEY(&offset, pubkey_blob.data(), &pubkey);
  EXPECT_EQ(offset, pubkey_blob.size());
  return pubkey_blob;
}

class Tpm1StaticUtilsRsaKeyTest
    : public testing::TestWithParam<int /* rsa_key_size_bits */> {
 protected:
  void SetUp() {
    crypto::ScopedBIGNUM e(BN_new());
    CHECK(e);
    ASSERT_TRUE(BN_set_word(e.get(), kWellKnownExponent));
    rsa_.reset(RSA_new());
    CHECK(rsa_);
    ASSERT_TRUE(
        RSA_generate_key_ex(rsa_.get(), rsa_key_size_bits(), e.get(), nullptr));
  }

  int rsa_key_size_bits() const { return GetParam(); }

  RSA* rsa() { return rsa_.get(); }

 private:
  crypto::ScopedRSA rsa_;
};

}  // namespace

// Tests the ParseRsaFromTpmPubkeyBlob function with correct TPM_PUBKEY blobs.
TEST_P(Tpm1StaticUtilsRsaKeyTest, ParseRsaFromTpmPubkeyBlob) {
  const Blob pubkey_blob = BuildRsaTpmPubkeyBlob(*rsa());

  crypto::ScopedRSA parsed_rsa = ParseRsaFromTpmPubkeyBlob(pubkey_blob);
  ASSERT_TRUE(parsed_rsa);

  const BIGNUM* n;
  const BIGNUM* e;
  RSA_get0_key(rsa(), &n, &e, nullptr);

  const BIGNUM* parsed_n;
  const BIGNUM* parsed_e;
  RSA_get0_key(parsed_rsa.get(), &parsed_n, &parsed_e, nullptr);

  EXPECT_EQ(BN_cmp(n, parsed_n), 0);
  EXPECT_EQ(BN_cmp(e, parsed_e), 0);
}

INSTANTIATE_TEST_SUITE_P(,
                         Tpm1StaticUtilsRsaKeyTest,
                         testing::Values(512, 1024, 2048, 4096));

}  // namespace cryptohome
