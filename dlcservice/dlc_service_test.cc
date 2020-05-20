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
#include "dlcservice/prefs.h"
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

class DlcServiceTestObserver : public DlcServiceInterface::Observer {
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

    InitializeDlcService();
  }

  void InitializeDlcService() {
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                DoRegisterStatusUpdateAdvancedSignalHandler(_, _))
        .Times(1);
    EXPECT_CALL(*mock_session_manager_proxy_ptr_,
                DoRegisterSessionStateChangedSignalHandler(_, _))
        .Times(1);

    dlc_service_ = std::make_unique<DlcService>();

    dlc_service_test_observer_ = std::make_unique<DlcServiceTestObserver>();
    dlc_service_->AddObserver(dlc_service_test_observer_.get());

    dlc_service_->Initialize();
  }

  void Install(const DlcId& id) {
    EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                SetDlcActiveValue(true, id, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(id, _, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
    EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

    EXPECT_TRUE(dlc_service_->Install(id, kDefaultOmahaUrl, &err_));
    EXPECT_TRUE(dlc_service_->InstallCompleted({id}, &err_));

    StatusResult status_result;
    status_result.set_is_install(true);
    status_result.set_current_operation(Operation::IDLE);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

    CheckDlcState(id, DlcState::INSTALLED);
    auto install_status = dlc_service_test_observer_->GetInstallStatus();
    EXPECT_EQ(install_status.status(), Status::COMPLETED);
    EXPECT_EQ(install_status.state(), InstallStatus::IDLE);
  }

  void CheckDlcState(const DlcId& id, const DlcState::State& expected_state) {
    const auto* dlc = dlc_service_->GetDlc(id);
    EXPECT_NE(dlc, nullptr);
    EXPECT_EQ(expected_state, dlc->GetState().state());
  }

 protected:
  base::MessageLoopForIO base_loop_;
  brillo::BaseMessageLoop loop_{&base_loop_};

  std::unique_ptr<DlcService> dlc_service_;
  std::unique_ptr<DlcServiceTestObserver> dlc_service_test_observer_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DlcServiceTest);
};

TEST_F(DlcServiceTest, GetInstalledTest) {
  Install(kFirstDlc);

  const auto& dlcs = dlc_service_->GetInstalled();

  EXPECT_THAT(dlcs, ElementsAre(kFirstDlc));
  EXPECT_FALSE(dlc_service_->GetDlc(kFirstDlc)->GetRoot().value().empty());
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, GetExistingDlcs) {
  Install(kFirstDlc);

  SetUpDlcWithSlots(kSecondDlc);
  const auto& dlcs = dlc_service_->GetExistingDlcs();

  EXPECT_THAT(dlcs, ElementsAre(kFirstDlc, kSecondDlc));
}

TEST_F(DlcServiceTest, GetDlcsToUpdateTest) {
  Install(kFirstDlc);

  // Make second DLC marked as verified so we can get it in the list of DLCs
  // needed to be updated.
  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));
  const auto& dlcs = dlc_service_->GetDlcsToUpdate();

  EXPECT_THAT(dlcs, ElementsAre(kFirstDlc, kSecondDlc));
}

TEST_F(DlcServiceTest, GetInstalledMimicDlcserviceRebootWithoutVerifiedStamp) {
  Install(kFirstDlc);
  const auto& dlcs_before = dlc_service_->GetInstalled();
  EXPECT_THAT(dlcs_before, ElementsAre(kFirstDlc));
  EXPECT_FALSE(dlc_service_->GetDlc(kFirstDlc)->GetRoot().value().empty());

  // Create |kSecondDlc| image, but not verified after device reboot.
  SetUpDlcWithSlots(kSecondDlc);

  const auto& dlcs_after = dlc_service_->GetInstalled();
  EXPECT_THAT(dlcs_after, ElementsAre(kFirstDlc));
}

TEST_F(DlcServiceTest, UninstallTest) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  auto dlc_prefs_path = prefs_path_.Append("dlc").Append(kFirstDlc);
  EXPECT_TRUE(base::PathExists(dlc_prefs_path));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  EXPECT_FALSE(base::PathExists(dlc_prefs_path));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, PurgeTest) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  auto dlc_prefs_path = prefs_path_.Append("dlc").Append(kFirstDlc);
  EXPECT_TRUE(base::PathExists(dlc_prefs_path));

  EXPECT_TRUE(dlc_service_->Purge(kFirstDlc, &err_));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  EXPECT_FALSE(base::PathExists(dlc_prefs_path));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallNotInstalledIsValidTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc_service_->Uninstall(kSecondDlc, &err_));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallFailToSetDlcActiveValueFalse) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallInvalidDlcTest) {
  const auto& id = "invalid-dlc-id";
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_FALSE(dlc_service_->Uninstall(id, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorInvalidDlc);
}

TEST_F(DlcServiceTest, UninstallUnmountFailureTest) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(false), Return(true)));

  EXPECT_FALSE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, UninstallImageLoaderFailureTest) {
  Install(kFirstDlc);

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
  Install(kFirstDlc);

  StatusResult status_result;
  status_result.set_current_operation(Operation::CHECKING_FOR_UPDATE);
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(status_result), Return(true)));

  EXPECT_FALSE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, UninstallUpdatedNeedRebootSuccessTest) {
  Install(kFirstDlc);

  StatusResult status_result;
  status_result.set_current_operation(Operation::UPDATED_NEED_REBOOT);
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(status_result), Return(true)));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallInstallingFails) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_FALSE(dlc_service_->Uninstall(kSecondDlc, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorBusy);
}

TEST_F(DlcServiceTest, UninstallInstallingButInstalledFails) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, SetDlcActiveValue(false, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, SetDlcActiveValue(true, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, InstallInvalidDlcTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));

  const string id = "bad-dlc-id";
  EXPECT_FALSE(dlc_service_->Install(id, kDefaultOmahaUrl, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorInvalidDlc);
}

TEST_F(DlcServiceTest, InstallTest) {
  Install(kFirstDlc);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  auto dlc_prefs_path = prefs_path_.Append("dlc").Append(kSecondDlc);
  EXPECT_FALSE(base::PathExists(dlc_prefs_path));

  EXPECT_THAT(dlc_service_->GetInstalled(), ElementsAre(kFirstDlc));

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  // Should remain same as it's not stamped verfied.
  EXPECT_THAT(dlc_service_->GetInstalled(), ElementsAre(kFirstDlc));

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

  EXPECT_TRUE(base::PathExists(dlc_prefs_path));
  base::GetPosixFilePermissions(dlc_prefs_path, &permissions);
  EXPECT_EQ(permissions, expected_permissions);

  // TODO(ahassani): Add more install process liked |InstallCompleted|, etc.
}

TEST_F(DlcServiceTest, InstallAlreadyInstalledValid) {
  Install(kFirstDlc);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc_service_->Install(kFirstDlc, kDefaultOmahaUrl, &err_));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, InstallCannotSetDlcActiveValue) {
  Install(kFirstDlc);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallUpdateEngineDownThenBackUpTest) {
  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_FALSE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallUpdateEngineBusyThenFreeTest) {
  Install(kFirstDlc);

  SetMountPath(mount_path_.value());
  StatusResult status_result;
  status_result.set_current_operation(Operation::UPDATED_NEED_REBOOT);
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(status_result), Return(true)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_FALSE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallFailureCleansUp) {
  Install(kFirstDlc);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_FALSE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, InstallUrlTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              AttemptInstall(kDefaultOmahaUrl, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTest, InstallAlreadyInstalledThatGotUnmountedTest) {
  Install(kFirstDlc);

  // TOOD(ahassani): Move these checks to InstallTest.
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  const auto mount_path_root = JoinPaths(mount_path_, "root");
  EXPECT_TRUE(base::PathExists(mount_path_root));
  EXPECT_TRUE(base::DeleteFile(mount_path_root, true));

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  dlc_service_->Install(kFirstDlc, kDefaultOmahaUrl, &err_);
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, OnStatusUpdateSignalDlcRootTest) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLED);

  const auto& dlcs_after = dlc_service_->GetInstalled();

  EXPECT_THAT(dlcs_after, ElementsAre(kFirstDlc, kSecondDlc));
  EXPECT_FALSE(dlc_service_->GetDlc(kFirstDlc)->GetRoot().value().empty());
  EXPECT_FALSE(dlc_service_->GetDlc(kSecondDlc)->GetRoot().value().empty());
}

TEST_F(DlcServiceTest, OnStatusUpdateSignalNoRemountTest) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
}

TEST_F(DlcServiceTest, OnStatusUpdateSignalTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, MountFailureTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(""), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kThirdDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, ReportingFailureCleanupTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

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

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, ReportingFailureSignalTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

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

  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, ProbableUpdateEngineRestartCleanupTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(false);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UpdateEngineFailSafeTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  MessageLoopRunUntil(
      &loop_, base::TimeDelta::FromSeconds(DlcService::kUECheckTimeout * 2),
      base::Bind([]() { return false; }));

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UpdateEngineFailAfterSignalsSafeTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  StatusResult status_result;
  status_result.set_current_operation(Operation::DOWNLOADING);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  MessageLoopRunUntil(
      &loop_, base::TimeDelta::FromSeconds(DlcService::kUECheckTimeout * 2),
      base::Bind([]() { return false; }));

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, OnStatusUpdateSignalDownloadProgressTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

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

  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  status_result.set_current_operation(Operation::IDLE);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
  auto install_status = dlc_service_test_observer_->GetInstallStatus();
  EXPECT_EQ(install_status.status(), Status::COMPLETED);
  EXPECT_EQ(install_status.state(), InstallStatus::IDLE);

  CheckDlcState(kSecondDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest,
       OnStatusUpdateSignalSubsequentialBadOrNonInstalledDlcsNonBlocking) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));

  for (int i = 0; i < 5; i++) {
    EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                SetDlcActiveValue(true, kSecondDlc, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                SetDlcActiveValue(false, kSecondDlc, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
        .WillOnce(Return(false));
    EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

    EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
    CheckDlcState(kSecondDlc, DlcState::INSTALLING);

    EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

    StatusResult status_result;
    status_result.set_is_install(true);
    status_result.set_current_operation(Operation::IDLE);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
    EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
    CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
  }
}

TEST_F(DlcServiceTest, PeriodCheckUpdateEngineInstallSignalRaceChecker) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  MessageLoopRunUntil(
      &loop_, base::TimeDelta::FromSeconds(DlcService::kUECheckTimeout * 5),
      base::Bind([]() { return false; }));

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, InstallCompleted) {
  auto active_boot_slot = SystemState::Get()->active_boot_slot();
  EXPECT_FALSE(
      Prefs(DlcBase(kSecondDlc), active_boot_slot).Exists(kDlcPrefVerified));
  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));
  EXPECT_TRUE(
      Prefs(DlcBase(kSecondDlc), active_boot_slot).Exists(kDlcPrefVerified));
}

TEST_F(DlcServiceTest, UpdateCompleted) {
  auto inactive_boot_slot = SystemState::Get()->inactive_boot_slot();
  EXPECT_FALSE(
      Prefs(DlcBase(kSecondDlc), inactive_boot_slot).Exists(kDlcPrefVerified));
  EXPECT_TRUE(dlc_service_->UpdateCompleted({kFirstDlc, kSecondDlc}, &err_));
  EXPECT_TRUE(
      Prefs(DlcBase(kSecondDlc), inactive_boot_slot).Exists(kDlcPrefVerified));
}

}  // namespace dlcservice
