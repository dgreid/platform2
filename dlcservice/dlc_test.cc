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

    SystemState::Initialize(
        std::move(mock_image_loader_proxy_),
        std::move(mock_update_engine_proxy_),
        std::move(mock_session_manager_proxy_), &mock_state_change_reporter_,
        std::make_unique<BootSlot>(move(mock_boot_device)), manifest_path_,
        preloaded_content_path_, content_path_, prefs_path_, users_path_,
        /*for_test=*/true);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DlcBaseTestRemovable);
};

TEST_F(DlcBaseTest, CreateDlc) {
  DlcBase dlc(kFirstDlc);
  dlc.Initialize();

  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc.Install(&err_));

  constexpr int expected_permissions = 0755;
  int permissions;
  base::FilePath module_path = JoinPaths(content_path_, kFirstDlc, kPackage);
  base::GetPosixFilePermissions(module_path, &permissions);
  EXPECT_EQ(permissions, expected_permissions);
  base::FilePath image_a_path =
      GetDlcImagePath(content_path_, kFirstDlc, kPackage, BootSlot::Slot::A);
  base::GetPosixFilePermissions(image_a_path.DirName(), &permissions);
  EXPECT_EQ(permissions, expected_permissions);
  base::FilePath image_b_path =
      GetDlcImagePath(content_path_, kFirstDlc, kPackage, BootSlot::Slot::B);
  base::GetPosixFilePermissions(image_b_path.DirName(), &permissions);
  EXPECT_EQ(permissions, expected_permissions);

  base::FilePath dlc_prefs_path = JoinPaths(prefs_path_, "dlc", kFirstDlc);
  EXPECT_TRUE(base::PathExists(dlc_prefs_path));
  base::GetPosixFilePermissions(dlc_prefs_path, &permissions);
  EXPECT_EQ(permissions, expected_permissions);

  EXPECT_EQ(dlc.GetState().state(), DlcState::INSTALLING);
}

TEST_F(DlcBaseTest, InstallWithUECompletion) {
  DlcBase dlc(kFirstDlc);
  dlc.Initialize();

  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(_, kFirstDlc, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc.Install(&err_));
  InstallWithUpdateEngine({kFirstDlc});
  // UE calls this.
  dlc.InstallCompleted(&err_);
  EXPECT_EQ(dlc.GetState().state(), DlcState::INSTALLING);

  dlc.FinishInstall(&err_);
  EXPECT_EQ(dlc.GetState().state(), DlcState::INSTALLED);
  EXPECT_TRUE(dlc.IsVerified());
}

TEST_F(DlcBaseTest, InstallWithoutUECompletion) {
  DlcBase dlc(kFirstDlc);
  dlc.Initialize();

  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(_, kFirstDlc, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc.Install(&err_));
  InstallWithUpdateEngine({kFirstDlc});
  // UE doesn't call InstallComplete anymore. But we still verify.
  EXPECT_EQ(dlc.GetState().state(), DlcState::INSTALLING);

  dlc.FinishInstall(&err_);
  EXPECT_EQ(dlc.GetState().state(), DlcState::INSTALLED);
  EXPECT_TRUE(dlc.IsVerified());
}

TEST_F(DlcBaseTest, InstallWhenInstalling) {
  DlcBase dlc(kFirstDlc);
  dlc.Initialize();

  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc.Install(&err_));
  EXPECT_EQ(dlc.GetState().state(), DlcState::INSTALLING);

  // A second install should do nothing.
  EXPECT_TRUE(dlc.Install(&err_));
  EXPECT_EQ(dlc.GetState().state(), DlcState::INSTALLING);
}

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
  dlc.is_verified_ = true;

  // Make sure the function recreates the inactive image.
  auto inactive_image_path =
      dlc.GetImagePath(SystemState::Get()->inactive_boot_slot());
  base::DeleteFile(inactive_image_path, /*recursive=*/false);
  EXPECT_FALSE(base::PathExists(inactive_image_path));

  auto prefs = Prefs(dlc, SystemState::Get()->inactive_boot_slot());
  EXPECT_TRUE(prefs.Create(kDlcPrefVerified));
  EXPECT_TRUE(dlc.MakeReadyForUpdate());
  EXPECT_TRUE(base::PathExists(inactive_image_path));
  EXPECT_FALSE(prefs.Exists(kDlcPrefVerified));
}

TEST_F(DlcBaseTest, MakeReadyForUpdateNotVerfied) {
  DlcBase dlc(kSecondDlc);
  dlc.Initialize();

  auto prefs = Prefs(dlc, SystemState::Get()->inactive_boot_slot());
  EXPECT_TRUE(prefs.Create(kDlcPrefVerified));
  // Since DLC is not verfied, it should return false.
  EXPECT_FALSE(dlc.MakeReadyForUpdate());
  EXPECT_FALSE(prefs.Exists(kDlcPrefVerified));
}

TEST_F(DlcBaseTest, BootingFromNonRemovableDeviceDeletesPreloadedDLCs) {
  DlcBase dlc(kThirdDlc);
  dlc.Initialize();
  // Place preloaded images.
  SetUpDlcPreloadedImage(kThirdDlc);

  auto image_path = JoinPaths(preloaded_content_path_, kThirdDlc, kPackage,
                              kDlcImageFileName);
  EXPECT_TRUE(base::PathExists(image_path));

  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(_, kThirdDlc, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc.Install(&err_));

  // Preloaded DLC image should be deleted.
  EXPECT_FALSE(base::PathExists(image_path));
}

TEST_F(DlcBaseTestRemovable, BootingFromRemovableDeviceKeepsPreloadedDLCs) {
  DlcBase dlc(kThirdDlc);
  dlc.Initialize();
  // Place preloaded images.
  SetUpDlcPreloadedImage(kThirdDlc);

  auto image_path = JoinPaths(preloaded_content_path_, kThirdDlc, kPackage,
                              kDlcImageFileName);
  EXPECT_TRUE(base::PathExists(image_path));

  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(_, kThirdDlc, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc.Install(&err_));

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

TEST_F(DlcBaseTest, ImageOnDiskButNotVerifiedInstalls) {
  DlcBase dlc(kSecondDlc);
  dlc.Initialize();

  SetUpDlcWithSlots(kSecondDlc);
  InstallWithUpdateEngine({kSecondDlc});

  EXPECT_EQ(dlc.GetState().state(), DlcState::NOT_INSTALLED);
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc.Install(&err_));
  EXPECT_TRUE(dlc.IsInstalling());
}

TEST_F(DlcBaseTest, ImageOnDiskVerifiedInstalls) {
  DlcBase dlc(kSecondDlc);
  EXPECT_TRUE(Prefs(dlc, SystemState::Get()->active_boot_slot())
                  .Create(kDlcPrefVerified));
  SetUpDlcWithSlots(kSecondDlc);
  InstallWithUpdateEngine({kSecondDlc});

  dlc.Initialize();

  EXPECT_EQ(dlc.GetState().state(), DlcState::NOT_INSTALLED);
  EXPECT_CALL(*mock_image_loader_proxy_ptr_,
              LoadDlcImage(kSecondDlc, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(_, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc.Install(&err_));
  EXPECT_TRUE(dlc.IsInstalled());
}

TEST_F(DlcBaseTest, VerifyDlcImageOnUEFailureToCompleteInstall) {
  DlcBase dlc(kSecondDlc);
  dlc.Initialize();

  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(_, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_,
              LoadDlcImage(kSecondDlc, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc.Install(&err_));
  EXPECT_TRUE(dlc.IsInstalling());

  // Intentionally skip over setting verified mark before |FinishInstall()|.
  InstallWithUpdateEngine({kSecondDlc});

  EXPECT_TRUE(dlc.FinishInstall(&err_));
  EXPECT_TRUE(dlc.IsInstalled());
}

TEST_F(DlcBaseTest, DefaultState) {
  DlcBase dlc(kFirstDlc);
  dlc.Initialize();
  dlc.mount_point_ = base::FilePath("foo-path");

  DlcState state = dlc.GetState();
  EXPECT_EQ(state.id(), kFirstDlc);
  EXPECT_EQ(state.state(), DlcState::NOT_INSTALLED);
  EXPECT_EQ(state.progress(), 0);
  EXPECT_EQ(state.root_path(), "");
}

TEST_F(DlcBaseTest, ChangeStateNotInstalled) {
  DlcBase dlc(kFirstDlc);
  dlc.Initialize();
  dlc.mount_point_ = base::FilePath("foo-path");

  EXPECT_CALL(
      mock_state_change_reporter_,
      DlcStateChanged(CheckDlcStateProto(DlcState::NOT_INSTALLED, 0, "")));
  dlc.ChangeState(DlcState::NOT_INSTALLED);
}

TEST_F(DlcBaseTest, ChangeStateInstalling) {
  DlcBase dlc(kFirstDlc);
  dlc.Initialize();
  dlc.mount_point_ = base::FilePath("foo-path");

  EXPECT_CALL(mock_state_change_reporter_,
              DlcStateChanged(CheckDlcStateProto(DlcState::INSTALLING, 0, "")));
  dlc.ChangeState(DlcState::INSTALLING);
}

TEST_F(DlcBaseTest, ChangeStateInstalled) {
  DlcBase dlc(kFirstDlc);
  dlc.Initialize();
  dlc.mount_point_ = base::FilePath("foo-path");

  EXPECT_CALL(mock_state_change_reporter_,
              DlcStateChanged(
                  CheckDlcStateProto(DlcState::INSTALLED, 1.0, "foo-path")));
  dlc.ChangeState(DlcState::INSTALLED);
}

TEST_F(DlcBaseTest, ChangeProgress) {
  DlcBase dlc(kFirstDlc);
  dlc.Initialize();

  // Any state other than installing should not change the progress.
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(0);
  dlc.ChangeProgress(0.5);

  EXPECT_CALL(mock_state_change_reporter_,
              DlcStateChanged(CheckDlcStateProto(DlcState::INSTALLING, 0, "")));
  dlc.ChangeState(DlcState::INSTALLING);

  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(CheckDlcStateProto(
                                               DlcState::INSTALLING, 0.5, "")));
  dlc.ChangeProgress(0.5);

  // Lower progress should not send signal.
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(0);
  dlc.ChangeProgress(0.3);

  // Same progress should not send the signal.
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(0);
  dlc.ChangeProgress(0.5);
}

}  // namespace dlcservice
