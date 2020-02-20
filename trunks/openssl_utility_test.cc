// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/openssl_utility.h"

#include <stdint.h>

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <crypto/scoped_openssl_types.h>
#include <gtest/gtest.h>
#include <libhwsec/crypto_utility.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#include "trunks/tpm_generated.h"

namespace {

// TODO(b/150264096): create a shared HexDecode in libhwsec and use that in this
// file instead.
std::string HexDecode(const std::string& hex) {
  std::vector<uint8_t> output;
  CHECK(base::HexStringToBytes(hex, &output));
  return std::string(output.begin(), output.end());
}

constexpr int kTestEccCurveId = NID_X9_62_prime256v1;

// Hex encoded x and y coordinates of a point on the ECC curve defined in
// kTestEccCurveId.
constexpr char kDefaultEccPointX[] =
    "BDC25C11F0A80AB85B4EC6A186B238C6B13F6049E4DE8136DD8AC34835645683";
constexpr char kDefaultEccPointY[] =
    "370C900BA238F2F89AF6C081783CB8D9C1ABDFCFE8D323200B2528386D29DA92";

}  // namespace

namespace trunks {

class OpensslUtilityTest : public testing::Test {
 public:
  OpensslUtilityTest()
      : default_ec_group_(EC_GROUP_new_by_curve_name(kTestEccCurveId)) {}

  // Creates an ECC point in the OpenSSL EC_POINT format, using the hex encoded
  // x, y coordinates |hex_x| and |hex_y|, and stores the point in |ec_point|.
  void CreateOpensslEccPoint(
      const char* hex_x, const char* hex_y, EC_POINT* ec_point) {
    hwsec::ScopedBN_CTX ctx;
    BIGNUM* x = BN_CTX_get(ctx.get());
    BIGNUM* y = BN_CTX_get(ctx.get());
    BN_hex2bn(&x, hex_x);
    BN_hex2bn(&y, hex_y);
    ASSERT_EQ(EC_POINT_set_affine_coordinates_GFp(
                  default_ec_group_.get(), ec_point, x, y, ctx.get()),
              1);
  }

  // Creates an ECC point in the TPMS_ECC_POINT format, using the hex encoded
  // x, y coordinates |hex_x| and |hex_y|, and returns the point.
  TPMS_ECC_POINT CreateTpmEccPoint(const char* hex_x, const char* hex_y) {
    TPMS_ECC_POINT point;
    point.x = trunks::Make_TPM2B_ECC_PARAMETER(HexDecode(hex_x));
    point.y = trunks::Make_TPM2B_ECC_PARAMETER(HexDecode(hex_y));
    return point;
  }

 protected:
  crypto::ScopedEC_GROUP default_ec_group_;
};

using OpensslUtilityDeathTest = OpensslUtilityTest;

TEST_F(OpensslUtilityTest, OpensslToTpmEccPointSuccess) {
  crypto::ScopedEC_POINT openssl_point(EC_POINT_new(default_ec_group_.get()));
  CreateOpensslEccPoint(
      kDefaultEccPointX, kDefaultEccPointY, openssl_point.get());

  TPMS_ECC_POINT tpm_point;
  EXPECT_TRUE(OpensslToTpmEccPoint(
      default_ec_group_.get(), openssl_point.get(), &tpm_point));
  EXPECT_EQ(StringFrom_TPM2B_ECC_PARAMETER(tpm_point.x),
            HexDecode(kDefaultEccPointX));
  EXPECT_EQ(StringFrom_TPM2B_ECC_PARAMETER(tpm_point.y),
            HexDecode(kDefaultEccPointY));
}

TEST_F(OpensslUtilityDeathTest, OpensslToTpmEccPointBadInput) {
  TPMS_ECC_POINT tpm_point;
  crypto::ScopedEC_POINT openssl_point(EC_POINT_new(default_ec_group_.get()));

  const std::string expected_error = ".*Uninitialized input argument.*";
  EXPECT_DEATH_IF_SUPPORTED(
      OpensslToTpmEccPoint(nullptr, openssl_point.get(), &tpm_point),
      expected_error);
  EXPECT_DEATH_IF_SUPPORTED(
      OpensslToTpmEccPoint(default_ec_group_.get(), nullptr, &tpm_point),
      expected_error);
  EXPECT_DEATH_IF_SUPPORTED(
      OpensslToTpmEccPoint(
          default_ec_group_.get(), openssl_point.get(), nullptr),
      expected_error);
}

TEST_F(OpensslUtilityTest, TpmToOpensslEccPointSuccess) {
  const TPMS_ECC_POINT tpm_point =
      CreateTpmEccPoint(kDefaultEccPointX, kDefaultEccPointY);

  crypto::ScopedEC_POINT openssl_point(EC_POINT_new(default_ec_group_.get()));
  EXPECT_TRUE(TpmToOpensslEccPoint(
      tpm_point, default_ec_group_.get(), openssl_point.get()));

  hwsec::ScopedBN_CTX ctx;
  BIGNUM* actual_x = BN_CTX_get(ctx.get());
  BIGNUM* actual_y = BN_CTX_get(ctx.get());
  ASSERT_EQ(EC_POINT_get_affine_coordinates_GFp(default_ec_group_.get(),
                                                openssl_point.get(),
                                                actual_x,
                                                actual_y,
                                                ctx.get()),
            1);

  BIGNUM* expected_x = BN_CTX_get(ctx.get());
  BIGNUM* expected_y = BN_CTX_get(ctx.get());
  BN_hex2bn(&expected_x, kDefaultEccPointX);
  BN_hex2bn(&expected_y, kDefaultEccPointY);
  EXPECT_EQ(BN_cmp(actual_x, expected_x), 0);
  EXPECT_EQ(BN_cmp(actual_y, expected_y), 0);
}

TEST_F(OpensslUtilityDeathTest, TpmToOpensslEccPointBadInput) {
  TPMS_ECC_POINT tpm_point;
  crypto::ScopedEC_POINT openssl_point(EC_POINT_new(default_ec_group_.get()));

  const std::string expected_error = ".*Uninitialized input argument.*";
  EXPECT_DEATH_IF_SUPPORTED(
      TpmToOpensslEccPoint(tpm_point, default_ec_group_.get(), nullptr),
      expected_error);
  EXPECT_DEATH_IF_SUPPORTED(
      TpmToOpensslEccPoint(tpm_point, nullptr, openssl_point.get()),
      expected_error);
}

}  // namespace trunks
