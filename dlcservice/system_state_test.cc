// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/system_state.h"
#include "dlcservice/test_utils.h"

namespace dlcservice {

class SystemStateTest : public BaseTest {};

TEST_F(SystemStateTest, GettersTest) {
  auto system_state = SystemState::Get();
  const auto temp_path = scoped_temp_dir_.GetPath();

  EXPECT_EQ(system_state->manifest_dir(), temp_path.Append("rootfs"));
  EXPECT_EQ(system_state->preloaded_content_dir(),
            temp_path.Append("preloaded_stateful"));
  EXPECT_EQ(system_state->content_dir(), temp_path.Append("stateful"));
  EXPECT_EQ(system_state->prefs_dir(), temp_path.Append("var_lib_dlcservice"));
  EXPECT_EQ(system_state->dlc_prefs_dir(),
            temp_path.Append("var_lib_dlcservice").Append("dlc"));
  EXPECT_EQ(system_state->active_boot_slot(), BootSlot::Slot::B);
  EXPECT_EQ(system_state->inactive_boot_slot(), BootSlot::Slot::A);
  EXPECT_FALSE(system_state->IsDeviceRemovable());
}

}  // namespace dlcservice
