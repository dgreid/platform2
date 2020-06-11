// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/files/file_path.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/utils/storage/device_info.h"
#include "diagnostics/cros_healthd/utils/storage/mock/mock_platform.h"
#include "diagnostics/cros_healthd/utils/storage/statusor.h"

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace diagnostics {

TEST(StorageDeviceInfoTest, OkData) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/nvme0n1";
  constexpr char kDevnode[] = "dev/node/path";
  constexpr char kSubsystem[] = "block:nvme";
  constexpr uint64_t kSize = 42;
  constexpr uint64_t kErrorCode = 15;
  constexpr char kErrorMessage[] = "ERROR";
  auto mock_platform = std::make_unique<StrictMock<MockPlatform>>();

  EXPECT_CALL(*mock_platform.get(),
              GetDeviceSizeBytes(base::FilePath(kDevnode)))
      .WillOnce(Return(kSize));
  EXPECT_CALL(*mock_platform.get(),
              GetDeviceBlockSizeBytes(base::FilePath(kDevnode)))
      .WillOnce(Return(Status(kErrorCode, kErrorMessage)));

  StorageDeviceInfo dev_info(base::FilePath(kPath), base::FilePath(kDevnode),
                             kSubsystem, std::move(mock_platform));
  EXPECT_EQ(kPath, dev_info.GetSysPath().value());
  EXPECT_EQ(kDevnode, dev_info.GetDevNodePath().value());
  EXPECT_EQ(kSubsystem, dev_info.GetSubsystem());
  EXPECT_EQ("nvme0n1", dev_info.GetDeviceName());
  EXPECT_EQ("test_nvme_model", dev_info.GetModel());

  auto res = dev_info.GetSizeBytes();
  ASSERT_TRUE(res.ok());
  EXPECT_EQ(kSize, res.value());

  res = dev_info.GetBlockSizeBytes();
  ASSERT_FALSE(res.ok());
  EXPECT_EQ(kErrorCode, res.status().code());
  EXPECT_EQ(kErrorMessage, res.status().message());
}

}  // namespace diagnostics
