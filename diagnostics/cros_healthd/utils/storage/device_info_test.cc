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
#include "mojo/cros_healthd_probe.mojom.h"

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}

TEST(StorageDeviceInfoTest, PopulateTest) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/nvme0n1";
  constexpr char kDevnode[] = "dev/node/path";
  constexpr char kSubsystem[] = "block:nvme";
  constexpr uint64_t kSize = 16 * 1024;
  constexpr uint64_t kBlockSize = 512;
  auto mock_platform = std::make_unique<StrictMock<MockPlatform>>();

  EXPECT_CALL(*mock_platform, GetDeviceSizeBytes(base::FilePath(kDevnode)))
      .WillOnce(Return(kSize));
  EXPECT_CALL(*mock_platform, GetDeviceBlockSizeBytes(base::FilePath(kDevnode)))
      .WillOnce(Return(kBlockSize));

  auto dev_info =
      StorageDeviceInfo::Create(base::FilePath(kPath), base::FilePath(kDevnode),
                                kSubsystem, mock_platform.get());
  mojo_ipc::NonRemovableBlockDeviceInfo info;
  EXPECT_TRUE(dev_info->PopulateDeviceInfo(&info).ok());

  EXPECT_EQ(kDevnode, info.path);
  EXPECT_EQ(kSubsystem, info.type);
  EXPECT_EQ(kSize, info.size);
  EXPECT_EQ(144, info.read_time_seconds_since_last_boot);
  EXPECT_EQ(22155, info.write_time_seconds_since_last_boot);
  EXPECT_EQ((uint64_t)35505772 * kBlockSize, info.bytes_read_since_last_boot);
  EXPECT_EQ((uint64_t)665648234 * kBlockSize,
            info.bytes_written_since_last_boot);
  EXPECT_EQ(4646, info.io_time_seconds_since_last_boot);
  EXPECT_EQ(200, info.discard_time_seconds_since_last_boot->value);
  EXPECT_EQ("test_nvme_model", info.name);
}

TEST(StorageDeviceInfoTest, PopulateLegacyTest) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/mmcblk0";
  constexpr char kDevnode[] = "dev/node/path";
  constexpr char kSubsystem[] = "block:mmc";
  auto mock_platform = std::make_unique<StrictMock<MockPlatform>>();

  auto dev_info =
      StorageDeviceInfo::Create(base::FilePath(kPath), base::FilePath(kDevnode),
                                kSubsystem, mock_platform.get());
  mojo_ipc::NonRemovableBlockDeviceInfo info;
  dev_info->PopulateLegacyFields(&info);

  EXPECT_EQ(0x1EAFBED5, info.serial);
  EXPECT_EQ(0xA5, info.manufacturer_id);
}

}  // namespace diagnostics
