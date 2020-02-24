// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "trunks/mock_tpm.h"
#include "trunks/tpm_cache_impl.h"
#include "trunks/tpm_generated.h"
#include "trunks/tpm_utility.h"

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace trunks {

// A test fixture for TpmCache tests.
class TpmCacheTest : public testing::Test {
 public:
  TpmCacheTest() : tpm_cache_impl_(&mock_tpm_) {}
  ~TpmCacheTest() override = default;

 protected:
  NiceMock<MockTpm> mock_tpm_;
  TpmCacheImpl tpm_cache_impl_;
};

TEST_F(TpmCacheTest, GetSaltingKeyPublicAreaSuccess) {
  TPMT_PUBLIC expected_pub_area;
  expected_pub_area.type = TPM_ALG_ECC;
  expected_pub_area.name_alg = TPM_ALG_SHA256;

  TPM2B_PUBLIC expected_pub_data;
  expected_pub_data.public_area = expected_pub_area;

  EXPECT_CALL(mock_tpm_, ReadPublicSync(kSaltingKey, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(expected_pub_data), Return(TPM_RC_SUCCESS)));

  // First query goes to the TPM.
  TPMT_PUBLIC actual_pub_area;
  EXPECT_EQ(tpm_cache_impl_.GetSaltingKeyPublicArea(&actual_pub_area),
            TPM_RC_SUCCESS);
  EXPECT_EQ(actual_pub_area.type, expected_pub_area.type);
  EXPECT_EQ(actual_pub_area.name_alg, expected_pub_area.name_alg);

  // Call again and see if it returns from cache directly.
  actual_pub_area.type = TPM_ALG_ERROR;
  actual_pub_area.name_alg = TPM_ALG_ERROR;
  EXPECT_EQ(tpm_cache_impl_.GetSaltingKeyPublicArea(&actual_pub_area),
            TPM_RC_SUCCESS);
  EXPECT_EQ(actual_pub_area.type, expected_pub_area.type);
  EXPECT_EQ(actual_pub_area.name_alg, expected_pub_area.name_alg);
}

TEST_F(TpmCacheTest, GetSaltingKeyPublicAreaBadInput) {
  EXPECT_CALL(mock_tpm_, ReadPublicSync(_, _, _, _, _, _)).Times(0);
  EXPECT_EQ(tpm_cache_impl_.GetSaltingKeyPublicArea(nullptr), TPM_RC_FAILURE);
}

TEST_F(TpmCacheTest, GetSaltingKeyPublicAreaTpmError) {
  EXPECT_CALL(mock_tpm_, ReadPublicSync(kSaltingKey, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));

  TPMT_PUBLIC pub_area;
  EXPECT_EQ(tpm_cache_impl_.GetSaltingKeyPublicArea(&pub_area), TPM_RC_FAILURE);
}

}  // namespace trunks
