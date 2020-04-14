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

  void Init(const DlcId& id) { dlc_ = std::make_unique<DlcBase>(id); }

 protected:
  std::unique_ptr<DlcBase> dlc_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DlcBaseTest);
};

TEST_F(DlcBaseTest, MountableOnInitialization) {
  Init(kSecondDlc);
  SetUpDlcWithSlots(kSecondDlc);

  // Explicitly set |kDlcPrefVerified| here.
  auto pref =
      Prefs(DlcBase(kSecondDlc), SystemState::Get()->active_boot_slot());
  EXPECT_TRUE(pref.Create(kDlcPrefVerified));

  EXPECT_EQ(dlc_->GetState().state(), DlcState::NOT_INSTALLED);

  dlc_->Initialize();

  EXPECT_EQ(dlc_->GetState().state(), DlcState::MOUNTABLE);
}

}  // namespace dlcservice
