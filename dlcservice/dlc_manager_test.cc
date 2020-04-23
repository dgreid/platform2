// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "dlcservice/test_utils.h"

using testing::_;
using testing::ElementsAre;
using testing::Return;
using testing::SetArgPointee;

namespace dlcservice {

class DlcManagerTest : public BaseTest {
 public:
  DlcManagerTest() { dlc_manager_ = std::make_unique<DlcManager>(); }

  void CheckDlcState(const DlcId& id_in,
                     const DlcState::State& state_in,
                     bool fail = false) {
    DlcState state;
    if (fail) {
      EXPECT_FALSE(dlc_manager_->GetDlcState(id_in, &state, &err_));
      return;
    }
    EXPECT_TRUE(dlc_manager_->GetDlcState(id_in, &state, &err_));
    EXPECT_EQ(state_in, state.state());
  }

 protected:
  std::unique_ptr<DlcManager> dlc_manager_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DlcManagerTest);
};

TEST_F(DlcManagerTest, PreloadAllowedDlcTest) {
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
  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());

  dlc_manager_->Initialize();

  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre(kThirdDlc));
  EXPECT_FALSE(dlc_manager_->GetDlc(kThirdDlc)->GetRoot().value().empty());
  CheckDlcState(kThirdDlc, DlcState::INSTALLED);
}

TEST_F(DlcManagerTest, PreloadAllowedWithBadPreinstalledDlcTest) {
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
  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());

  dlc_manager_->Initialize();

  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre(kThirdDlc));
  EXPECT_FALSE(dlc_manager_->GetDlc(kThirdDlc)->GetRoot().value().empty());
  CheckDlcState(kThirdDlc, DlcState::INSTALLED);
}

TEST_F(DlcManagerTest, PreloadNotAllowedDlcTest) {
  SetUpDlcWithoutSlots(kSecondDlc);

  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());

  dlc_manager_->Initialize();

  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

}  // namespace dlcservice
