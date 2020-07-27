// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/time/time.h>
#include <gtest/gtest.h>

#include "dlcservice/ref_count.h"
#include "dlcservice/test_utils.h"
#include "dlcservice/utils.h"

using dlcservice::metrics::InstallResult;
using std::string;
using testing::_;
using testing::ElementsAre;
using testing::Return;
using testing::SetArgPointee;

namespace dlcservice {

class DlcManagerTest : public BaseTest {
 public:
  DlcManagerTest() { dlc_manager_ = std::make_unique<DlcManager>(); }

  void Install(const DlcId& id) {
    EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(id, _, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
    EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                SetDlcActiveValue(true, id, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_metrics_,
                SendInstallResult(InstallResult::kSuccessNewInstall));

    bool external_install_needed = false;
    EXPECT_TRUE(dlc_manager_->Install(id, &external_install_needed, &err_));
    CheckDlcState(id, DlcState::INSTALLING);

    InstallWithUpdateEngine({id});
    EXPECT_TRUE(dlc_manager_->InstallCompleted({id}, &err_));
    EXPECT_TRUE(dlc_manager_->FinishInstall(id, &err_));
    CheckDlcState(id, DlcState::INSTALLED);
  }

  void Uninstall(const DlcId& id) {
    EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
    EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
    EXPECT_TRUE(dlc_manager_->Uninstall(id, &err_));
    CheckDlcState(id, DlcState::NOT_INSTALLED);
  }

  void CheckDlcState(const DlcId& id, const DlcState::State& expected_state) {
    const auto* dlc = dlc_manager_->GetDlc(id, &err_);
    EXPECT_NE(dlc, nullptr);
    EXPECT_EQ(expected_state, dlc->GetState().state());
  }

 protected:
  std::unique_ptr<DlcManager> dlc_manager_;

 private:
  DlcManagerTest(const DlcManagerTest&) = delete;
  DlcManagerTest& operator=(const DlcManagerTest&) = delete;
};

TEST_F(DlcManagerTest, PreloadAllowedDlcTest) {
  // The third DLC has pre-loaded flag on.
  SetUpDlcPreloadedImage(kThirdDlc);
  dlc_manager_->Initialize();

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessAlreadyInstalled));
  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  bool external_install_needed = false;
  EXPECT_TRUE(
      dlc_manager_->Install(kThirdDlc, &external_install_needed, &err_));
  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre(kThirdDlc));
  EXPECT_FALSE(
      dlc_manager_->GetDlc(kThirdDlc, &err_)->GetRoot().value().empty());
  CheckDlcState(kThirdDlc, DlcState::INSTALLED);
}

TEST_F(DlcManagerTest, PreloadAllowedWithBadPreinstalledDlcTest) {
  // The third DLC has pre-loaded flag on.
  SetUpDlcWithSlots(kThirdDlc);
  SetUpDlcPreloadedImage(kThirdDlc);
  dlc_manager_->Initialize();

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessAlreadyInstalled));

  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());
  bool external_install_needed = false;
  EXPECT_TRUE(
      dlc_manager_->Install(kThirdDlc, &external_install_needed, &err_));
  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre(kThirdDlc));
  EXPECT_FALSE(
      dlc_manager_->GetDlc(kThirdDlc, &err_)->GetRoot().value().empty());
  CheckDlcState(kThirdDlc, DlcState::INSTALLED);
}

TEST_F(DlcManagerTest, PreloadNotAllowedDlcTest) {
  SetUpDlcPreloadedImage(kSecondDlc);

  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());

  dlc_manager_->Initialize();

  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcManagerTest, UnsupportedContentDlcRemovalCheck) {
  auto id = "unsupported-dlc";
  for (const auto& slot : {BootSlot::Slot::A, BootSlot::Slot::B}) {
    auto image_path = GetDlcImagePath(content_path_, id, kPackage, slot);
    base::CreateDirectory(image_path.DirName());
    CreateFile(image_path, 1);
  }
  EXPECT_TRUE(base::CreateDirectory(JoinPaths(prefs_path_, "dlc", id)));

  EXPECT_TRUE(base::PathExists(JoinPaths(prefs_path_, "dlc", id)));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));

  dlc_manager_->Initialize();

  EXPECT_FALSE(base::PathExists(JoinPaths(prefs_path_, "dlc", id)));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
}

TEST_F(DlcManagerTest, UnsupportedPreloadedDlcRemovalCheck) {
  auto id = "unsupported-dlc";
  auto image_path =
      JoinPaths(preloaded_content_path_, id, kPackage, kDlcImageFileName);
  base::CreateDirectory(image_path.DirName());
  CreateFile(image_path, 1);

  EXPECT_TRUE(base::PathExists(JoinPaths(preloaded_content_path_, id)));
  dlc_manager_->Initialize();
  EXPECT_FALSE(base::PathExists(JoinPaths(preloaded_content_path_, id)));
}

TEST_F(DlcManagerTest, CleanupDanglingDlcs) {
  // The the clock to the system time so it doesn't start with 0;
  clock_.SetNow(base::Time::Now());

  dlc_manager_->Initialize();
  Install(kFirstDlc);

  // Make sure the ref count is not deleted.
  auto ref_count_path = JoinPaths(SystemState::Get()->dlc_prefs_dir(),
                                  kFirstDlc, kRefCountFileName);
  EXPECT_TRUE(base::PathExists(ref_count_path));
  Uninstall(kFirstDlc);
  EXPECT_TRUE(base::PathExists(ref_count_path));

  // Advance the time so the |kFirstDlc| becomes dangling.
  clock_.Advance(base::TimeDelta::FromDays(6));

  // Reinitialize the |dlc_manager_| so it initializes the |kFirstDlc| again.
  dlc_manager_->Initialize();
  // Install another DLC to make sure cleanup dangling doesn't remove them.
  Install(kSecondDlc);

  // These should happen when the |kFirstDlc| is purged.
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kFirstDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  // Advance by 31 minutes so it kicks in the cleanup method.
  clock_.Advance(base::TimeDelta::FromDays(31));
  loop_.RunOnce(false);

  // |kFirstDLC| should be gone by now.
  EXPECT_FALSE(base::PathExists(ref_count_path));
  // |kSecondDlc| should still be around.
  CheckDlcState(kSecondDlc, DlcState::INSTALLED);
}

}  // namespace dlcservice
