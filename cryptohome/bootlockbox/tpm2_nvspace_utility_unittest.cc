// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/bootlockbox/tpm2_nvspace_utility.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/trunks_factory_for_test.h>

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace {

// A helper function to serialize a uint16_t.
std::string uint16_to_string(uint16_t value) {
  const char* bytes = reinterpret_cast<const char*>(&value);
  return std::string(bytes, sizeof(uint16_t));
}

}  // namespace

namespace cryptohome {

class TPM2NVSpaceUtilityTest : public testing::Test {
 public:
  void SetUp() override {
    factory_.set_tpm_utility(&mock_trunks_tpm_utility_);
    nvspace_utility_ =
        std::make_unique<TPM2NVSpaceUtility>(&mock_tpm_nvram_, &factory_);
    nvspace_utility_->Initialize();
  }

 protected:
  NiceMock<org::chromium::TpmNvramProxyMock> mock_tpm_nvram_;
  trunks::TrunksFactoryForTest factory_;
  NiceMock<trunks::MockTpmUtility> mock_trunks_tpm_utility_;
  std::unique_ptr<TPM2NVSpaceUtility> nvspace_utility_;
};

TEST_F(TPM2NVSpaceUtilityTest, DefineNVSpaceSuccess) {
  EXPECT_CALL(mock_tpm_nvram_, DefineSpace(_, _, _, _))
      .WillOnce(Invoke([](const tpm_manager::DefineSpaceRequest& request,
                          tpm_manager::DefineSpaceReply* reply,
                          brillo::ErrorPtr*, int) {
        EXPECT_TRUE(request.has_index());
        EXPECT_EQ(kBootLockboxNVRamIndex, request.index());
        EXPECT_TRUE(request.has_size());
        EXPECT_EQ(kNVSpaceSize, request.size());
        reply->set_result(tpm_manager::NVRAM_RESULT_SUCCESS);
        return true;
      }));
  EXPECT_TRUE(nvspace_utility_->DefineNVSpace());
}

TEST_F(TPM2NVSpaceUtilityTest, DefineNVSpaceFail) {
  EXPECT_CALL(mock_tpm_nvram_, DefineSpace(_, _, _, _))
      .WillOnce(Invoke([](const tpm_manager::DefineSpaceRequest& request,
                          tpm_manager::DefineSpaceReply* reply,
                          brillo::ErrorPtr*, int) {
        reply->set_result(tpm_manager::NVRAM_RESULT_IPC_ERROR);
        return true;
      }));
  EXPECT_FALSE(nvspace_utility_->DefineNVSpace());
}

TEST_F(TPM2NVSpaceUtilityTest, DefineNVSpaceBeforeOwned) {
  EXPECT_CALL(mock_trunks_tpm_utility_,
              DefineNVSpace(kBootLockboxNVRamIndex, kNVSpaceSize, _,
                            kWellKnownPassword, "", _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_TRUE(nvspace_utility_->DefineNVSpaceBeforeOwned());
}

TEST_F(TPM2NVSpaceUtilityTest, ReadNVSpaceLengthFail) {
  EXPECT_CALL(mock_trunks_tpm_utility_, ReadNVSpace(_, _, _, _, _, _))
      .WillOnce(
          Invoke([](uint32_t index, uint32_t offset, size_t num_bytes,
                    bool using_owner_authorization, std::string* nvram_data,
                    trunks::AuthorizationDelegate* delegate) -> trunks::TPM_RC {
            *nvram_data = uint16_to_string(1) /* version */ +
                          uint16_to_string(0) /* flags */ +
                          std::string(3, '\x3');
            return trunks::TPM_RC_SUCCESS;  // return success to trigger error.
          }));
  std::string data;
  NVSpaceState state;
  EXPECT_FALSE(nvspace_utility_->ReadNVSpace(&data, &state));
}

TEST_F(TPM2NVSpaceUtilityTest, ReadNVSpaceVersionFail) {
  EXPECT_CALL(mock_trunks_tpm_utility_, ReadNVSpace(_, _, _, _, _, _))
      .WillOnce(
          Invoke([](uint32_t index, uint32_t offset, size_t num_bytes,
                    bool using_owner_authorization, std::string* nvram_data,
                    trunks::AuthorizationDelegate* delegate) -> trunks::TPM_RC {
            BootLockboxNVSpace data;
            data.version = 2;
            *nvram_data =
                std::string(reinterpret_cast<char*>(&data), kNVSpaceSize);
            return trunks::TPM_RC_SUCCESS;
          }));
  std::string data;
  NVSpaceState state;
  EXPECT_FALSE(nvspace_utility_->ReadNVSpace(&data, &state));
}

TEST_F(TPM2NVSpaceUtilityTest, ReadNVSpaceSuccess) {
  std::string test_digest(SHA256_DIGEST_LENGTH, 'a');
  EXPECT_CALL(mock_trunks_tpm_utility_, ReadNVSpace(_, _, _, _, _, _))
      .WillOnce(Invoke(
          [test_digest](
              uint32_t index, uint32_t offset, size_t num_bytes,
              bool using_owner_authorization, std::string* nvram_data,
              trunks::AuthorizationDelegate* delegate) -> trunks::TPM_RC {
            BootLockboxNVSpace data;
            data.version = 1;
            data.flags = 0;
            memcpy(data.digest, test_digest.c_str(), SHA256_DIGEST_LENGTH);
            *nvram_data =
                std::string(reinterpret_cast<char*>(&data), kNVSpaceSize);
            return trunks::TPM_RC_SUCCESS;
          }));
  std::string data;
  NVSpaceState state;
  EXPECT_TRUE(nvspace_utility_->ReadNVSpace(&data, &state));
  EXPECT_EQ(data, test_digest);
}

TEST_F(TPM2NVSpaceUtilityTest, WriteNVSpaceSuccess) {
  std::string nvram_data(SHA256_DIGEST_LENGTH, 'a');
  EXPECT_CALL(mock_trunks_tpm_utility_,
              WriteNVSpace(kBootLockboxNVRamIndex, 0,
                           uint16_to_string(1) /* version */ +
                               uint16_to_string(0) /* flags */ + nvram_data,
                           false, false, _));
  EXPECT_TRUE(nvspace_utility_->WriteNVSpace(nvram_data));
}

TEST_F(TPM2NVSpaceUtilityTest, WriteNVSpaceInvalidLength) {
  std::string nvram_data = "data of invalid length";
  EXPECT_CALL(mock_trunks_tpm_utility_, WriteNVSpace(_, _, _, _, _, _))
      .Times(0);
  EXPECT_FALSE(nvspace_utility_->WriteNVSpace(nvram_data));
}

TEST_F(TPM2NVSpaceUtilityTest, LockNVSpace) {
  EXPECT_CALL(mock_trunks_tpm_utility_,
              LockNVSpace(kBootLockboxNVRamIndex, false, true, false, _));
  EXPECT_TRUE(nvspace_utility_->LockNVSpace());
}

}  // namespace cryptohome
