// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/run_loop.h>
#include <brillo/message_loops/base_message_loop.h>
#include <brillo/message_loops/message_loop_utils.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <update_engine/proto_bindings/update_engine.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dlcservice/dlc.h"
#include "dlcservice/dlc_service.h"
#include "dlcservice/test_utils.h"
#include "dlcservice/utils.h"

using brillo::ErrorPtr;
using std::string;
using std::vector;
using testing::_;
using testing::ElementsAre;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;
using update_engine::Operation;
using update_engine::StatusResult;

namespace dlcservice {

namespace {
MATCHER_P(ProtoHasUrl,
          url,
          string("The protobuf provided does not have url: ") + url) {
  return url == arg.omaha_url();
}

class DlcServiceTestObserver : public DlcService::Observer {
 public:
  DlcServiceTestObserver() = default;

  void SendInstallStatus(const InstallStatus& install_status) override {
    install_status_.emplace(install_status);
  }

  bool IsSendInstallStatusCalled() { return install_status_.has_value(); }

  InstallStatus GetInstallStatus() {
    EXPECT_TRUE(install_status_.has_value())
        << "SendInstallStatus() was not called.";
    base::Optional<InstallStatus> tmp;
    tmp.swap(install_status_);
    return *tmp;
  }

 private:
  base::Optional<InstallStatus> install_status_;

  DISALLOW_COPY_AND_ASSIGN(DlcServiceTestObserver);
};

}  // namespace

class DlcServiceTest : public BaseTest {
 public:
  DlcServiceTest() = default;

  void SetUp() override {
    loop_.SetAsCurrent();

    BaseTest::SetUp();

    ConstructDlcService();

    SetUpDlcWithSlots(kFirstDlc);
    InstallDlcs({kFirstDlc});
  }

  void ConstructDlcService() {
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                DoRegisterStatusUpdateAdvancedSignalHandler(_, _))
        .Times(1);

    dlc_service_ = std::make_unique<DlcService>();

    dlc_service_test_observer_ = std::make_unique<DlcServiceTestObserver>();
    dlc_service_->AddObserver(dlc_service_test_observer_.get());
  }

  void InstallDlcs(const DlcSet& ids) {
    EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
        .Times(ids.size())
        .WillRepeatedly(
            DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
    EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                SetDlcActiveValue(true, _, _, _))
        .Times(ids.size())
        .WillRepeatedly(Return(true));
    EXPECT_TRUE(dlc_service_->Install(ids, kDefaultOmahaUrl, &err_));
    for (const auto& id : ids) {
      CheckDlcState(id, DlcState::INSTALLED);
    }
    EXPECT_EQ(dlc_service_test_observer_->GetInstallStatus().status(),
              Status::COMPLETED);
  }

  void CheckDlcState(const DlcId& id_in,
                     const DlcState::State& state_in,
                     bool fail = false) {
    DlcState state;
    if (fail) {
      EXPECT_FALSE(dlc_service_->GetState(id_in, &state, &err_));
      return;
    }
    EXPECT_TRUE(dlc_service_->GetState(id_in, &state, &err_));
    EXPECT_EQ(state_in, state.state());
  }

 protected:
  base::MessageLoopForIO base_loop_;
  brillo::BaseMessageLoop loop_{&base_loop_};

  std::unique_ptr<DlcService> dlc_service_;
  std::unique_ptr<DlcServiceTestObserver> dlc_service_test_observer_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DlcServiceTest);
};

TEST_F(DlcServiceTest, PreloadAllowedDlcTest) {
  // The third DLC has pre-loaded flag on.
  SetUpDlcWithoutSlots(kThirdDlc);

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_THAT(dlc_service_->GetInstalled(), ElementsAre(kFirstDlc));

  dlc_service_->PreloadDlcs();

  EXPECT_THAT(dlc_service_->GetInstalled(), ElementsAre(kFirstDlc, kThirdDlc));
  EXPECT_FALSE(dlc_service_->GetDlc(kThirdDlc).GetRoot().value().empty());
  CheckDlcState(kThirdDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, PreloadAllowedWithBadPreinstalledDlcTest) {
  // The third DLC has pre-loaded flag on.
  SetUpDlcWithSlots(kThirdDlc);
  SetUpDlcWithoutSlots(kThirdDlc);

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_THAT(dlc_service_->GetInstalled(), ElementsAre(kFirstDlc));

  dlc_service_->PreloadDlcs();

  EXPECT_THAT(dlc_service_->GetInstalled(), ElementsAre(kFirstDlc, kThirdDlc));
  EXPECT_FALSE(dlc_service_->GetDlc(kThirdDlc).GetRoot().value().empty());
  CheckDlcState(kThirdDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, PreloadNotAllowedDlcTest) {
  SetUpDlcWithoutSlots(kSecondDlc);

  EXPECT_THAT(dlc_service_->GetInstalled(), ElementsAre(kFirstDlc));

  dlc_service_->PreloadDlcs();

  EXPECT_THAT(dlc_service_->GetInstalled(), ElementsAre(kFirstDlc));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest,
       MimicUpdateRebootInstallWherePreallocatedSizeIncreasedTest) {
  // Check A and B images.
  for (const auto& slot : {kDlcDirAName, kDlcDirBName})
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc, kPackage,
                                           slot, kDlcImageFileName)));
  base::FilePath inactive_img_path =
      GetDlcImagePath(content_path_, kFirstDlc, kPackage,
                      SystemState::Get()->inactive_boot_slot());
  imageloader::Manifest manifest;
  dlcservice::GetDlcManifest(manifest_path_, kFirstDlc, kPackage, &manifest);
  int64_t inactive_img_size = manifest.preallocated_size();
  int64_t new_inactive_img_size = inactive_img_size / 2;
  EXPECT_TRUE(new_inactive_img_size < inactive_img_size);

  ResizeImageFile(inactive_img_path, new_inactive_img_size);
  EXPECT_EQ(new_inactive_img_size, GetFileSize(inactive_img_path));

  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_TRUE(dlc_service_->Install({kFirstDlc}, kDefaultOmahaUrl, &err_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  EXPECT_EQ(inactive_img_size, GetFileSize(inactive_img_path));
}

TEST_F(DlcServiceTest, GetInstalledTest) {
  const auto& dlcs = dlc_service_->GetInstalled();

  EXPECT_THAT(dlcs, ElementsAre(kFirstDlc));
  EXPECT_FALSE(dlc_service_->GetDlc(kFirstDlc).GetRoot().value().empty());
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, UninstallTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallNotInstalledIsValidTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(false));
  EXPECT_TRUE(dlc_service_->Uninstall(kSecondDlc, &err_));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallFailToSetDlcActiveValueFalse) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallInvalidDlcTest) {
  const auto& id = "invalid-dlc-id";
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_FALSE(dlc_service_->Uninstall(id, &err_));
  CheckDlcState(id, DlcState::NOT_INSTALLED, /*fail=*/true);
}

TEST_F(DlcServiceTest, UninstallUnmountFailureTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(false), Return(true)));

  EXPECT_FALSE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, UninstallImageLoaderFailureTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(Return(false));

  // |ImageLoader| not available.
  EXPECT_FALSE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, UninstallUpdateEngineBusyFailureTest) {
  StatusResult status_result;
  status_result.set_current_operation(Operation::CHECKING_FOR_UPDATE);
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(status_result), Return(true)));

  EXPECT_FALSE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, UninstallUpdatedNeedRebootSuccessTest) {
  StatusResult status_result;
  status_result.set_current_operation(Operation::UPDATED_NEED_REBOOT);
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(status_result), Return(true)));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallInstallingFails) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install({kSecondDlc}, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_FALSE(dlc_service_->Uninstall(kSecondDlc, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorBusy);
}

TEST_F(DlcServiceTest, UninstallInstallingButInstalledFails) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, SetDlcActiveValue(true, _, _, _))
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(
      dlc_service_->Install({kFirstDlc, kSecondDlc}, kDefaultOmahaUrl, &err_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, InstallEmptyDlcModuleListTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_FALSE(dlc_service_->Install({}, kDefaultOmahaUrl, &err_));
}

TEST_F(DlcServiceTest, InstallInvalidDlcTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));

  const string id = "bad-dlc-id";
  EXPECT_FALSE(dlc_service_->Install({id}, kDefaultOmahaUrl, &err_));
  CheckDlcState(id, DlcState::NOT_INSTALLED, /*fail=*/true);
}

TEST_F(DlcServiceTest, InstallTest) {
  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install({kSecondDlc}, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  constexpr int expected_permissions = 0755;
  int permissions;
  base::FilePath module_path = JoinPaths(content_path_, kSecondDlc, kPackage);
  base::GetPosixFilePermissions(module_path, &permissions);
  EXPECT_EQ(permissions, expected_permissions);
  base::FilePath image_a_path =
      GetDlcImagePath(content_path_, kSecondDlc, kPackage, BootSlot::Slot::A);
  base::GetPosixFilePermissions(image_a_path.DirName(), &permissions);
  EXPECT_EQ(permissions, expected_permissions);
  base::FilePath image_b_path =
      GetDlcImagePath(content_path_, kSecondDlc, kPackage, BootSlot::Slot::B);
  base::GetPosixFilePermissions(image_b_path.DirName(), &permissions);
  EXPECT_EQ(permissions, expected_permissions);
}

TEST_F(DlcServiceTest, InstallAlreadyInstalledValid) {
  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install({kFirstDlc}, kDefaultOmahaUrl, &err_));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, InstallDuplicatesSucceeds) {
  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(
      dlc_service_->Install({kSecondDlc, kSecondDlc}, kDefaultOmahaUrl, &err_));

  for (const auto& id : {kFirstDlc, kSecondDlc})
    for (const auto& path : {JoinPaths(content_path_, id)})
      EXPECT_TRUE(base::PathExists(path));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallAlreadyInstalledAndDuplicatesSucceeds) {
  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(dlc_service_->Install({kFirstDlc, kSecondDlc, kSecondDlc},
                                    kDefaultOmahaUrl, &err_));

  for (const auto& id : {kFirstDlc, kSecondDlc})
    for (const auto& path : {JoinPaths(content_path_, id)})
      EXPECT_TRUE(base::PathExists(path));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallCannotSetDlcActiveValue) {
  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(false));

  EXPECT_TRUE(dlc_service_->Install({kSecondDlc}, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallUpdateEngineDownThenBackUpTest) {
  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_FALSE(dlc_service_->Install({kSecondDlc}, kDefaultOmahaUrl, &err_));
  EXPECT_TRUE(dlc_service_->Install({kSecondDlc}, kDefaultOmahaUrl, &err_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallUpdateEngineBusyThenFreeTest) {
  SetMountPath(mount_path_.value());
  StatusResult status_result;
  status_result.set_current_operation(Operation::UPDATED_NEED_REBOOT);
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(status_result), Return(true)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_FALSE(dlc_service_->Install({kSecondDlc}, kDefaultOmahaUrl, &err_));
  EXPECT_TRUE(dlc_service_->Install({kSecondDlc}, kDefaultOmahaUrl, &err_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallFailureCleansUp) {
  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));

  EXPECT_FALSE(
      dlc_service_->Install({kSecondDlc, kThirdDlc}, kDefaultOmahaUrl, &err_));

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kThirdDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
  CheckDlcState(kThirdDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, InstallUrlTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              AttemptInstall(ProtoHasUrl(kDefaultOmahaUrl), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  dlc_service_->Install({kSecondDlc}, kDefaultOmahaUrl, &err_);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallAlreadyInstalledThatGotUnmountedTest) {
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  const auto mount_path_root = JoinPaths(mount_path_, "root");
  EXPECT_TRUE(base::PathExists(mount_path_root));
  EXPECT_TRUE(base::DeleteFile(mount_path_root, true));

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));

  dlc_service_->Install({kFirstDlc}, kDefaultOmahaUrl, &err_);
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, OnStatusUpdateAdvancedSignalDlcRootTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));

  const DlcSet ids = {kSecondDlc, kThirdDlc};
  EXPECT_TRUE(dlc_service_->Install(ids, kDefaultOmahaUrl, &err_));

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLED);
  }

  const auto& dlcs_after = dlc_service_->GetInstalled();

  EXPECT_THAT(dlcs_after, ElementsAre(kFirstDlc, kSecondDlc, kThirdDlc));
  EXPECT_FALSE(dlc_service_->GetDlc(kFirstDlc).GetRoot().value().empty());
  for (const auto& id : dlcs_after)
    EXPECT_FALSE(dlc_service_->GetDlc(id).GetRoot().value().empty());
}

TEST_F(DlcServiceTest, OnStatusUpdateAdvancedSignalNoRemountTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));

  const DlcSet ids = {kFirstDlc, kSecondDlc};
  EXPECT_TRUE(dlc_service_->Install(ids, kDefaultOmahaUrl, &err_));

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));

  for (const string& id : ids)
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  for (const string& id : ids)
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
}

TEST_F(DlcServiceTest, OnStatusUpdateAdvancedSignalTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));

  const DlcSet ids = {kSecondDlc, kThirdDlc};
  EXPECT_TRUE(dlc_service_->Install(ids, kDefaultOmahaUrl, &err_));

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)))
      .WillOnce(DoAll(SetArgPointee<3>(""), Return(true)));

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLED);
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kThirdDlc)));
  CheckDlcState(kThirdDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, ReportingFailureCleanupTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));

  const DlcSet ids = {kSecondDlc, kThirdDlc};
  EXPECT_TRUE(dlc_service_->Install(ids, kDefaultOmahaUrl, &err_));

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  {
    StatusResult status_result;
    status_result.set_current_operation(Operation::REPORTING_ERROR_EVENT);
    status_result.set_is_install(true);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  }
  {
    StatusResult status_result;
    status_result.set_current_operation(Operation::IDLE);
    status_result.set_is_install(false);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  }

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  for (const string& id : ids) {
    EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::NOT_INSTALLED);
  }
}

TEST_F(DlcServiceTest, ReportingFailureSignalTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));

  const DlcSet ids = {kSecondDlc, kThirdDlc};
  EXPECT_TRUE(dlc_service_->Install(ids, kDefaultOmahaUrl, &err_));

  for (const auto& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  {
    StatusResult status_result;
    status_result.set_current_operation(Operation::REPORTING_ERROR_EVENT);
    status_result.set_is_install(true);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  }
  {
    StatusResult status_result;
    status_result.set_current_operation(Operation::IDLE);
    status_result.set_is_install(false);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  }

  EXPECT_EQ(dlc_service_test_observer_->GetInstallStatus().status(),
            Status::FAILED);

  for (const auto& id : ids)
    CheckDlcState(id, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, ProbableUpdateEngineRestartCleanupTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));

  const DlcSet ids = {kSecondDlc, kThirdDlc};
  EXPECT_TRUE(dlc_service_->Install(ids, kDefaultOmahaUrl, &err_));

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(false);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  for (const string& id : ids) {
    EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::NOT_INSTALLED);
  }
}

TEST_F(DlcServiceTest, UpdateEngineFailSafeTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));

  const DlcSet ids = {kSecondDlc};
  EXPECT_TRUE(dlc_service_->Install(ids, kDefaultOmahaUrl, &err_));

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  MessageLoopRunUntil(
      &loop_, base::TimeDelta::FromSeconds(DlcService::kUECheckTimeout * 2),
      base::Bind([]() { return false; }));

  for (const string& id : ids) {
    EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::NOT_INSTALLED);
  }
}

TEST_F(DlcServiceTest, UpdateEngineFailAfterSignalsSafeTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));

  const DlcSet ids = {kSecondDlc};
  EXPECT_TRUE(dlc_service_->Install(ids, kDefaultOmahaUrl, &err_));

  for (const string& id : ids) {
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::INSTALLING);
  }

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));

  StatusResult status_result;
  status_result.set_current_operation(Operation::DOWNLOADING);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  MessageLoopRunUntil(
      &loop_, base::TimeDelta::FromSeconds(DlcService::kUECheckTimeout * 2),
      base::Bind([]() { return false; }));

  for (const string& id : ids) {
    EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::NOT_INSTALLED);
  }
}

TEST_F(DlcServiceTest, OnStatusUpdateAdvancedSignalDownloadProgressTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));

  const DlcSet ids = {kSecondDlc, kThirdDlc};
  EXPECT_TRUE(dlc_service_->Install(ids, kDefaultOmahaUrl, &err_));

  for (const auto& id : ids)
    CheckDlcState(id, DlcState::INSTALLING);

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));

  StatusResult status_result;
  status_result.set_is_install(true);

  const vector<Operation> install_operation_sequence = {
      Operation::CHECKING_FOR_UPDATE, Operation::UPDATE_AVAILABLE,
      Operation::FINALIZING};

  for (const auto& op : install_operation_sequence) {
    status_result.set_current_operation(op);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
    EXPECT_FALSE(dlc_service_test_observer_->IsSendInstallStatusCalled());
  }

  status_result.set_current_operation(Operation::DOWNLOADING);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  EXPECT_EQ(dlc_service_test_observer_->GetInstallStatus().status(),
            Status::RUNNING);

  status_result.set_current_operation(Operation::IDLE);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  EXPECT_EQ(dlc_service_test_observer_->GetInstallStatus().status(),
            Status::COMPLETED);

  for (const auto& id : ids)
    CheckDlcState(id, DlcState::INSTALLED);
}

TEST_F(
    DlcServiceTest,
    OnStatusUpdateAdvancedSignalSubsequentialBadOrNonInstalledDlcsNonBlocking) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));

  for (int i = 0; i < 5; i++) {
    EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                SetDlcActiveValue(true, kSecondDlc, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                SetDlcActiveValue(false, kSecondDlc, _, _))
        .WillOnce(Return(true));

    const DlcSet ids = {kSecondDlc};
    EXPECT_TRUE(dlc_service_->Install(ids, kDefaultOmahaUrl, &err_));
    for (const auto& id : ids)
      CheckDlcState(id, DlcState::INSTALLING);

    EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
        .WillOnce(Return(false));
    StatusResult status_result;
    status_result.set_is_install(true);
    status_result.set_current_operation(Operation::IDLE);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
    for (const auto& id : ids) {
      EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
      CheckDlcState(id, DlcState::NOT_INSTALLED);
    }
  }
}

TEST_F(DlcServiceTest, PeriodCheckUpdateEngineInstallSignalRaceChecker) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));

  const DlcSet ids = {kSecondDlc, kThirdDlc};
  EXPECT_TRUE(dlc_service_->Install(ids, kDefaultOmahaUrl, &err_));

  MessageLoopRunUntil(
      &loop_, base::TimeDelta::FromSeconds(DlcService::kUECheckTimeout * 5),
      base::Bind([]() { return false; }));

  for (const string& id : ids) {
    EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
    CheckDlcState(id, DlcState::NOT_INSTALLED);
  }
}

}  // namespace dlcservice
