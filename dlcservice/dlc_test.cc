// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "dlcservice/prefs.h"
#include "dlcservice/system_state.h"
#include "dlcservice/test_utils.h"

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

}  // namespace dlcservice
