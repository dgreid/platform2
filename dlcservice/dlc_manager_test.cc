// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "dlcservice/test_utils.h"
#include "dlcservice/utils.h"

using testing::_;
using testing::ElementsAre;
using testing::Return;
using testing::SetArgPointee;

namespace dlcservice {

class DlcManagerTest : public BaseTest {
 public:
  DlcManagerTest() { dlc_manager_ = std::make_unique<DlcManager>(); }

  void CheckDlcState(const DlcId& id, const DlcState::State& expected_state) {
    const auto* dlc = dlc_manager_->GetDlc(id);
    EXPECT_NE(dlc, nullptr);
    EXPECT_EQ(expected_state, dlc->GetState().state());
  }

 protected:
  std::unique_ptr<DlcManager> dlc_manager_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DlcManagerTest);
};

TEST_F(DlcManagerTest, PreloadAllowedDlcTest) {
  // The third DLC has pre-loaded flag on.
  SetUpDlcPreloadedImage(kThirdDlc);
  dlc_manager_->Initialize();

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());

  EXPECT_TRUE(dlc_manager_->InitInstall(kThirdDlc, &err_));

  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre(kThirdDlc));
  EXPECT_FALSE(dlc_manager_->GetDlc(kThirdDlc)->GetRoot().value().empty());
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
              SetDlcActiveValue(false, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());

  EXPECT_TRUE(dlc_manager_->InitInstall(kThirdDlc, &err_));

  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre(kThirdDlc));
  EXPECT_FALSE(dlc_manager_->GetDlc(kThirdDlc)->GetRoot().value().empty());
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

}  // namespace dlcservice
