// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dlcservice/boot/mock_boot_device.h"
#include "dlcservice/prefs.h"
#include "dlcservice/system_state.h"
#include "dlcservice/test_utils.h"
#include "dlcservice/utils.h"

using testing::_;
using testing::ElementsAre;
using testing::Return;
using testing::SetArgPointee;

namespace dlcservice {

class DlcBaseTest : public BaseTest {
 public:
  DlcBaseTest() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(DlcBaseTest);
};

class DlcBaseTestRemovable : public DlcBaseTest {
 public:
  DlcBaseTestRemovable() = default;

  void SetUp() override {
    SetUpFilesAndDirectories();

    auto mock_boot_device = std::make_unique<MockBootDevice>();
    EXPECT_CALL(*mock_boot_device, GetBootDevice())
        .WillOnce(Return("/dev/sdb5"));
    EXPECT_CALL(*mock_boot_device, IsRemovableDevice(_)).WillOnce(Return(true));

    SystemState::Initialize(std::move(mock_image_loader_proxy_),
                            std::move(mock_update_engine_proxy_),
                            std::make_unique<BootSlot>(move(mock_boot_device)),
                            manifest_path_, preloaded_content_path_,
                            content_path_, prefs_path_, /*for_test=*/true);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DlcBaseTestRemovable);
};

TEST_F(DlcBaseTest, VerifiedOnInitialization) {
  DlcBase dlc(kSecondDlc);

  // Explicitly set |kDlcPrefVerified| here.
  EXPECT_TRUE(Prefs(dlc, SystemState::Get()->active_boot_slot())
                  .Create(kDlcPrefVerified));
  EXPECT_EQ(dlc.GetState().state(), DlcState::NOT_INSTALLED);

  dlc.Initialize();
  EXPECT_TRUE(dlc.IsVerified());
}

TEST_F(DlcBaseTest, InstallCompleted) {
  DlcBase dlc(kSecondDlc);
  dlc.Initialize();

  EXPECT_FALSE(dlc.IsVerified());
  EXPECT_TRUE(dlc.InstallCompleted(&err_));
  EXPECT_TRUE(dlc.IsVerified());
}

TEST_F(DlcBaseTest, UpdateCompleted) {
  DlcBase dlc(kSecondDlc);
  dlc.Initialize();

  EXPECT_TRUE(dlc.UpdateCompleted(&err_));
  EXPECT_TRUE(Prefs(dlc, SystemState::Get()->inactive_boot_slot())
                  .Exists(kDlcPrefVerified));
}

TEST_F(DlcBaseTest, MakeReadyForUpdate) {
  DlcBase dlc(kSecondDlc);
  dlc.Initialize();

  auto prefs = Prefs(dlc, SystemState::Get()->inactive_boot_slot());
  EXPECT_TRUE(prefs.Create(kDlcPrefVerified));
  EXPECT_TRUE(dlc.MakeReadyForUpdate(&err_));
  EXPECT_FALSE(prefs.Exists(kDlcPrefVerified));
}

TEST_F(DlcBaseTest, BootingFromRemovableDeviceDeletesPreloadedDLCs) {
  DlcBase dlc(kSecondDlc);
  dlc.Initialize();
  SetUpDlcWithoutSlots(kSecondDlc);

  auto image_path = JoinPaths(preloaded_content_path_, kSecondDlc, kPackage,
                              kDlcImageFileName);
  EXPECT_TRUE(base::PathExists(image_path));

  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(_, kSecondDlc, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  dlc.PreloadImage();

  // Preloaded DLC image should be deleted.
  EXPECT_FALSE(base::PathExists(image_path));
}

TEST_F(DlcBaseTestRemovable, BootingFromNonRemovableDeviceKeepsPreloadedDLCs) {
  DlcBase dlc(kSecondDlc);
  dlc.Initialize();
  SetUpDlcWithoutSlots(kSecondDlc);

  auto image_path = JoinPaths(preloaded_content_path_, kSecondDlc, kPackage,
                              kDlcImageFileName);
  EXPECT_TRUE(base::PathExists(image_path));

  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(_, kSecondDlc, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  dlc.PreloadImage();

  // Preloaded DLC image should still exists.
  EXPECT_TRUE(base::PathExists(image_path));
}

TEST_F(DlcBaseTest, HasContent) {
  DlcBase dlc(kSecondDlc);
  dlc.Initialize();

  EXPECT_FALSE(dlc.HasContent());

  SetUpDlcWithSlots(kSecondDlc);
  EXPECT_TRUE(dlc.HasContent());
}

TEST_F(DlcBaseTest, GetUsedBytesOnDisk) {
  DlcBase dlc(kSecondDlc);
  dlc.Initialize();

  EXPECT_EQ(dlc.GetUsedBytesOnDisk(), 0);

  SetUpDlcWithSlots(kSecondDlc);
  uint64_t expected_size = 0;
  for (const auto& path : {dlc.GetImagePath(BootSlot::Slot::A),
                           dlc.GetImagePath(BootSlot::Slot::B)}) {
    expected_size += GetFileSize(path);
  }
  EXPECT_GT(expected_size, 0);

  EXPECT_EQ(dlc.GetUsedBytesOnDisk(), expected_size);
}

}  // namespace dlcservice
