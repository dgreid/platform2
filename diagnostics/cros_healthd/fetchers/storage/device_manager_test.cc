// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/storage/device_manager.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_device.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/fetchers/storage/mock/mock_device_lister.h"
#include "diagnostics/cros_healthd/fetchers/storage/mock/mock_device_resolver.h"
#include "diagnostics/cros_healthd/fetchers/storage/mock/mock_platform.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::UnorderedElementsAre;

// Tests that the StorageDeviceInfo structures are correctly populated and
// preserved between fetch calls.
TEST(StorageDeviceManagerTest, NoRecreation) {
  const base::FilePath kFakeRoot("cros_healthd/fetchers/storage/testdata/");
  const base::FilePath kNvme = kFakeRoot.Append("sys/block/nvme0n1");
  const base::FilePath kEmmc = kFakeRoot.Append("sys/block/mmcblk0");
  const base::FilePath kNvmeDev = kFakeRoot.Append("dev/nvme0n1");
  const base::FilePath kEmmcDev = kFakeRoot.Append("dev/mmcblk0");
  const std::string kBlockClass = "block";
  const std::string kNvmeClass = "nvme";
  const std::string kEmmcClass = "mmc";
  constexpr mojo_ipc::StorageDevicePurpose kNvmePurpose =
      mojo_ipc::StorageDevicePurpose::kSwapDevice;
  constexpr mojo_ipc::StorageDevicePurpose kEmmcPurpose =
      mojo_ipc::StorageDevicePurpose::kBootDevice;
  const uint64_t kNvmeSize = 1024;
  const uint64_t kEmmcSize = 768;
  const uint64_t kBlockSize = 512;
  std::vector<std::string> listed = {"mmcblk0", "nvme0n1"};

  auto mock_platform = std::make_unique<StrictMock<MockPlatform>>();

  // TODO(dlunev) querying size shall be cached as well and allow WillOnce.
  EXPECT_CALL(*mock_platform, GetDeviceSizeBytes(kNvmeDev))
      .WillRepeatedly(Return(kNvmeSize));
  EXPECT_CALL(*mock_platform, GetDeviceBlockSizeBytes(kNvmeDev))
      .WillRepeatedly(Return(kBlockSize));
  EXPECT_CALL(*mock_platform, GetDeviceSizeBytes(kEmmcDev))
      .WillRepeatedly(Return(kEmmcSize));
  EXPECT_CALL(*mock_platform, GetDeviceBlockSizeBytes(kEmmcDev))
      .WillRepeatedly(Return(kBlockSize));

  auto mock_nvme_udev = std::make_unique<StrictMock<brillo::MockUdevDevice>>();
  auto mock_nvme_parent_udev =
      std::make_unique<StrictMock<brillo::MockUdevDevice>>();
  EXPECT_CALL(*mock_nvme_udev, GetDeviceNode())
      .Times(1)
      .WillOnce(Return(kNvmeDev.value().c_str()));
  EXPECT_CALL(*mock_nvme_udev, GetSubsystem())
      .Times(1)
      .WillOnce(Return(kBlockClass.c_str()));
  EXPECT_CALL(*mock_nvme_parent_udev, GetSubsystem())
      .Times(1)
      .WillOnce(Return(kNvmeClass.c_str()));
  EXPECT_CALL(*mock_nvme_parent_udev, GetParent())
      .Times(1)
      .WillOnce(Return(ByMove(nullptr)));
  EXPECT_CALL(*mock_nvme_udev, GetParent())
      .Times(1)
      .WillOnce(Return(ByMove(std::move(mock_nvme_parent_udev))));

  auto mock_emmc_udev = std::make_unique<StrictMock<brillo::MockUdevDevice>>();
  auto mock_emmc_parent_udev =
      std::make_unique<StrictMock<brillo::MockUdevDevice>>();
  EXPECT_CALL(*mock_emmc_udev, GetDeviceNode())
      .Times(1)
      .WillOnce(Return(kEmmcDev.value().c_str()));
  EXPECT_CALL(*mock_emmc_udev, GetSubsystem())
      .Times(1)
      .WillOnce(Return(kBlockClass.c_str()));
  EXPECT_CALL(*mock_emmc_parent_udev, GetSubsystem())
      .Times(1)
      .WillOnce(Return(kEmmcClass.c_str()));
  EXPECT_CALL(*mock_emmc_parent_udev, GetParent())
      .Times(1)
      .WillOnce(Return(ByMove(nullptr)));
  EXPECT_CALL(*mock_emmc_udev, GetParent())
      .Times(1)
      .WillOnce(Return(ByMove(std::move(mock_emmc_parent_udev))));

  auto mock_udev = std::make_unique<StrictMock<brillo::MockUdev>>();
  EXPECT_CALL(*mock_udev, CreateDeviceFromSysPath(_))
      .Times(2)
      .WillOnce(Return(ByMove(std::move(mock_emmc_udev))))
      .WillOnce(Return(ByMove(std::move(mock_nvme_udev))));

  auto mock_resolver =
      std::make_unique<StrictMock<MockStorageDeviceResolver>>();
  EXPECT_CALL(*mock_resolver, GetDevicePurpose(_))
      .WillOnce(Return(kNvmePurpose))
      .WillOnce(Return(kEmmcPurpose));

  auto mock_lister = std::make_unique<StrictMock<MockStorageDeviceLister>>();
  EXPECT_CALL(*mock_lister, ListDevices(base::FilePath(kFakeRoot)))
      .WillRepeatedly(Return(listed));

  StorageDeviceManager manager(std::move(mock_lister), std::move(mock_resolver),
                               std::move(mock_udev), std::move(mock_platform));

  // Do multiple cycles. If the device info preservation is not working,
  // the WillOnce of udev mock will fail.
  for (int i = 0; i < 5; i++) {
    auto result_or = manager.FetchDevicesInfo(kFakeRoot);
    ASSERT_TRUE(result_or.ok()) << result_or.status().message();
    auto& result = result_or.value();

    std::vector<std::string> result_devs;
    for (const auto& info_ptr : result) {
      result_devs.push_back(info_ptr->path);
    }
    EXPECT_THAT(result_devs,
                UnorderedElementsAre(kNvmeDev.value(), kEmmcDev.value()));
  }
}

}  // namespace diagnostics
