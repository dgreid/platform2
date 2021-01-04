// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/test/simple_test_clock.h>
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
using dlcservice::metrics::InstallResult;
using dlcservice::metrics::UninstallResult;
using std::string;
using std::vector;
using testing::_;
using testing::DoAll;
using testing::ElementsAre;
using testing::Invoke;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::WithArg;
using testing::WithArgs;
using update_engine::Operation;
using update_engine::StatusResult;

namespace dlcservice {

class DlcServiceTest : public BaseTest {
 public:
  DlcServiceTest() = default;

  void SetUp() override {
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
    dlc_service_->Initialize();

    StatusResult status;
    status.set_current_operation(Operation::IDLE);
    EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
        .WillOnce(DoAll(SetArgPointee<0>(status), Return(true)));
    // To set the update_engine status for the first time.
    dlc_service_->OnStatusUpdateAdvancedSignalConnected("", "", true);
  }

  // Successfully install a DLC.
  void Install(const DlcId& id) {
    EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
        .WillOnce(DoAll(
            WithArg<1>(Invoke(this, &DlcServiceTest::InstallWithUpdateEngine)),
            WithArgs<1, 2>(
                Invoke(dlc_service_.get(), &DlcService::InstallCompleted)),
            Return(true)));
    EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(id, _, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
    EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                SetDlcActiveValue(true, id, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_metrics_,
                SendInstallResult(InstallResult::kSuccessNewInstall));

    EXPECT_TRUE(dlc_service_->Install(id, kDefaultOmahaUrl, &err_));

    CheckDlcState(id, DlcState::INSTALLING);

    StatusResult status_result;
    status_result.set_is_install(true);
    status_result.set_current_operation(Operation::IDLE);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

    CheckDlcState(id, DlcState::INSTALLED);
  }

  void CheckDlcState(const DlcId& id,
                     const DlcState::State& expected_state,
                     const string& error_code = kErrorNone) {
    const auto* dlc = dlc_service_->GetDlc(id, &err_);
    EXPECT_NE(dlc, nullptr);
    EXPECT_EQ(expected_state, dlc->GetState().state());
    EXPECT_EQ(dlc->GetState().last_error_code(), error_code.c_str());
  }

 protected:
  std::unique_ptr<DlcService> dlc_service_;

 private:
  DlcServiceTest(const DlcServiceTest&) = delete;
  DlcServiceTest& operator=(const DlcServiceTest&) = delete;
};

TEST_F(DlcServiceTest, GetInstalledTest) {
  Install(kFirstDlc);

  const auto& dlcs = dlc_service_->GetInstalled();

  EXPECT_THAT(dlcs, ElementsAre(kFirstDlc));
  EXPECT_FALSE(
      dlc_service_->GetDlc(kFirstDlc, &err_)->GetRoot().value().empty());
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
  EXPECT_FALSE(
      dlc_service_->GetDlc(kFirstDlc, &err_)->GetRoot().value().empty());

  // Create |kSecondDlc| image, but not verified after device reboot.
  SetUpDlcWithSlots(kSecondDlc);

  const auto& dlcs_after = dlc_service_->GetInstalled();
  EXPECT_THAT(dlcs_after, ElementsAre(kFirstDlc));
}

TEST_F(DlcServiceTest, UninstallTestForUserDlc) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  // Uninstall should not set the DLC inactive.
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .Times(0);
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_, SendUninstallResult(UninstallResult::kSuccess));

  auto dlc_prefs_path = prefs_path_.Append("dlc").Append(kFirstDlc);
  EXPECT_TRUE(base::PathExists(dlc_prefs_path));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_TRUE(err_.get() == nullptr);
  // Uninstall should not delete the DLC right away.
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  EXPECT_TRUE(base::PathExists(dlc_prefs_path));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
  // Uninstall should not change the verified status.
  EXPECT_TRUE(dlc_service_->GetDlc(kFirstDlc, &err_)->IsVerified());
}

TEST_F(DlcServiceTest, PurgeTest) {
  Install(kFirstDlc);

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

TEST_F(DlcServiceTest, UninstallNotInstalledIsValid) {
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_, SendUninstallResult(UninstallResult::kSuccess));

  EXPECT_TRUE(dlc_service_->Uninstall(kSecondDlc, &err_));
  EXPECT_TRUE(err_.get() == nullptr);
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, PurgeNotInstalledIsValid) {
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc_service_->Purge(kSecondDlc, &err_));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, PurgeFailToSetDlcActiveValueFalse) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc_service_->Purge(kFirstDlc, &err_));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, UninstallInvalidDlcTest) {
  const auto& id = "invalid-dlc-id";
  EXPECT_CALL(*mock_metrics_,
              SendUninstallResult(UninstallResult::kFailedInvalidDlc));

  EXPECT_FALSE(dlc_service_->Uninstall(id, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorInvalidDlc);
}

TEST_F(DlcServiceTest, PurgeInvalidDlcTest) {
  const auto& id = "invalid-dlc-id";
  EXPECT_FALSE(dlc_service_->Purge(id, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorInvalidDlc);
}

TEST_F(DlcServiceTest, UninstallImageLoaderFailureTest) {
  Install(kFirstDlc);

  // |ImageLoader| not available.
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_, SendUninstallResult(UninstallResult::kSuccess));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_TRUE(err_.get() == nullptr);
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTest, PurgeUpdateEngineBusyFailureTest) {
  Install(kFirstDlc);

  StatusResult status_result;
  status_result.set_current_operation(Operation::CHECKING_FOR_UPDATE);
  SystemState::Get()->set_update_engine_status(status_result);

  EXPECT_FALSE(dlc_service_->Purge(kFirstDlc, &err_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

// Same behavior should be for purge.
TEST_F(DlcServiceTest, UninstallInstallingFails) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_,
              SendUninstallResult(UninstallResult::kFailedUpdateEngineBusy));

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_FALSE(dlc_service_->Uninstall(kSecondDlc, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorBusy);
}

TEST_F(DlcServiceTest, PurgeInstallingFails) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_FALSE(dlc_service_->Purge(kSecondDlc, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorBusy);
}

TEST_F(DlcServiceTest, UninstallInstallingButInstalledFails) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_, SendUninstallResult(UninstallResult::kSuccess));

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  // |kFirstDlc| was installed, so there should be no problem uninstalling it
  // |even if |kSecondDlc| is installing.
  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_TRUE(err_.get() == nullptr);
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTest, InstallInvalidDlcTest) {
  const string id = "bad-dlc-id";
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedInvalidDlc));
  EXPECT_FALSE(dlc_service_->Install(id, kDefaultOmahaUrl, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorInvalidDlc);
}

TEST_F(DlcServiceTest, InstallTest) {
  Install(kFirstDlc);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_THAT(dlc_service_->GetInstalled(), ElementsAre(kFirstDlc));

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  // Should remain same as it's not stamped verified.
  EXPECT_THAT(dlc_service_->GetInstalled(), ElementsAre(kFirstDlc));

  // TODO(ahassani): Add more install process liked |InstallCompleted|, etc.
}

TEST_F(DlcServiceTest, InstallAlreadyInstalledValid) {
  Install(kFirstDlc);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_,
              LoadDlcImage(kFirstDlc, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessAlreadyInstalled));

  EXPECT_TRUE(dlc_service_->Install(kFirstDlc, kDefaultOmahaUrl, &err_));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, InstallAlreadyInstalledWhileAnotherInstalling) {
  Install(kFirstDlc);

  // Keep |kSecondDlc| installing.
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  // |kFirstDlc| can quickly be installed again even though another install is
  // ongoing.
  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_,
              LoadDlcImage(kFirstDlc, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessAlreadyInstalled));

  EXPECT_TRUE(dlc_service_->Install(kFirstDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, InstallCannotSetDlcActiveValue) {
  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_,
              LoadDlcImage(kSecondDlc, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessNewInstall));

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  StatusResult status_result;
  status_result.set_is_install(true);
  status_result.set_current_operation(Operation::IDLE);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  CheckDlcState(kSecondDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, PeriodicInstallCheck) {
  vector<StatusResult> status_list;
  for (const auto& op :
       {Operation::CHECKING_FOR_UPDATE, Operation::DOWNLOADING}) {
    StatusResult status;
    status.set_current_operation(op);
    status.set_is_install(true);
    status_list.push_back(status);
  }
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(status_list[0]), Return(true)))
      .WillOnce(Return(false))
      .WillOnce(DoAll(SetArgPointee<0>(status_list[1]), Return(true)));

  // We need to make sure the state is intalling so, rescheduling periodic check
  // happens.
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  // The first time it should not get the status because enough time hasn't
  // passed yet.
  dlc_service_->SchedulePeriodicInstallCheck();
  EXPECT_EQ(SystemState::Get()->update_engine_status().current_operation(),
            Operation::IDLE);

  // Now advance clock and make sure that first time we do get status.
  clock_.Advance(base::TimeDelta::FromSeconds(11));
  loop_.RunOnce(false);
  EXPECT_EQ(SystemState::Get()->update_engine_status().current_operation(),
            Operation::CHECKING_FOR_UPDATE);

  // Now advance the clock even more, this time fail the get status. The status
  // should remain same.
  clock_.Advance(base::TimeDelta::FromSeconds(11));
  loop_.RunOnce(false);
  EXPECT_EQ(SystemState::Get()->update_engine_status().current_operation(),
            Operation::CHECKING_FOR_UPDATE);

  // Now advance a little bit more to see we got the new status.
  clock_.Advance(base::TimeDelta::FromSeconds(11));
  loop_.RunOnce(false);
  EXPECT_EQ(SystemState::Get()->update_engine_status().current_operation(),
            Operation::DOWNLOADING);
}

TEST_F(DlcServiceTest, InstallSchedulesPeriodicInstallCheck) {
  vector<StatusResult> status_list;
  for (const auto& op : {Operation::CHECKING_FOR_UPDATE, Operation::IDLE}) {
    StatusResult status;
    status.set_current_operation(op);
    status.set_is_install(true);
    status_list.push_back(status);
  }

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvanced(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(status_list[1]), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedToVerifyImage));

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  // The checking for update comes from signal.
  dlc_service_->OnStatusUpdateAdvancedSignal(status_list[0]);

  // Now advance clock and make sure that periodic install check is scheduled
  // and eventually called.
  clock_.Advance(base::TimeDelta::FromSeconds(11));
  loop_.RunOnce(false);

  // Since the update_engine status went back to IDLE, the install is complete
  // and it should fail.
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTest, InstallFailureCleansUp) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedUpdateEngineBusy));

  EXPECT_FALSE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorBusy);

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorBusy);
}

TEST_F(DlcServiceTest, InstallUrlTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              AttemptInstall(kDefaultOmahaUrl, _, _, _))
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
  EXPECT_TRUE(base::DeletePathRecursively(mount_path_root));

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessAlreadyInstalled));

  EXPECT_TRUE(dlc_service_->Install(kFirstDlc, kDefaultOmahaUrl, &err_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest, InstallFailsToCreateDirectory) {
  base::SetPosixFilePermissions(content_path_, 0444);
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedToCreateDirectory));

  // Install will fail because DlcBase::CreateDlc() will fail to create
  // directories inside |content_path_|, since the permissions don't allow
  // writing into |content_path_|.
  EXPECT_FALSE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorInternal);

  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTest, OnStatusUpdateSignalDlcRootTest) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessNewInstall));

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
  EXPECT_FALSE(
      dlc_service_->GetDlc(kFirstDlc, &err_)->GetRoot().value().empty());
  EXPECT_FALSE(
      dlc_service_->GetDlc(kSecondDlc, &err_)->GetRoot().value().empty());
}

TEST_F(DlcServiceTest, OnStatusUpdateSignalNoRemountTest) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessNewInstall));

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
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessNewInstall));

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
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(""), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedToMountImage));

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(true);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  EXPECT_FALSE(dlc_service_->GetDlc(kSecondDlc, &err_)->IsVerified());
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTest, ReportingFailureCleanupTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedInstallInUpdateEngine));

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
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTest, ReportingFailureSignalTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedInstallInUpdateEngine));

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

  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTest, ProbableUpdateEngineRestartCleanupTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedInstallInUpdateEngine));

  EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  StatusResult status_result;
  status_result.set_current_operation(Operation::IDLE);
  status_result.set_is_install(false);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTest, OnStatusUpdateSignalDownloadProgressTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kSecondDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessNewInstall));

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
  }

  status_result.set_current_operation(Operation::DOWNLOADING);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  status_result.set_current_operation(Operation::IDLE);
  dlc_service_->OnStatusUpdateAdvancedSignal(status_result);

  CheckDlcState(kSecondDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTest,
       OnStatusUpdateSignalSubsequentialBadOrNonInstalledDlcsNonBlocking) {
  for (int i = 0; i < 5; i++) {
    EXPECT_CALL(*mock_update_engine_proxy_ptr_, AttemptInstall(_, _, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
        .WillOnce(Return(false));
    EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
    EXPECT_CALL(*mock_metrics_,
                SendInstallResult(InstallResult::kFailedToMountImage));

    EXPECT_TRUE(dlc_service_->Install(kSecondDlc, kDefaultOmahaUrl, &err_));
    CheckDlcState(kSecondDlc, DlcState::INSTALLING);

    EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

    StatusResult status_result;
    status_result.set_is_install(true);
    status_result.set_current_operation(Operation::IDLE);
    dlc_service_->OnStatusUpdateAdvancedSignal(status_result);
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
    CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
  }
}

TEST_F(DlcServiceTest, InstallCompleted) {
  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));
  EXPECT_TRUE(dlc_service_->GetDlc(kSecondDlc, &err_)->IsVerified());
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
