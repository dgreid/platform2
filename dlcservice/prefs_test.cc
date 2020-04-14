// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/dlc.h"
#include "dlcservice/prefs.h"
#include "dlcservice/system_state.h"
#include "dlcservice/test_utils.h"
#include "dlcservice/utils.h"

using base::FilePath;

namespace dlcservice {

class PrefsTest : public BaseTest {};

TEST_F(PrefsTest, DlcBaseCreateAndDelete) {
  auto active_boot_slot = SystemState::Get()->active_boot_slot();
  auto prefs = Prefs(DlcBase("id"), active_boot_slot);
  string key = "key";
  EXPECT_TRUE(prefs.Create(key));
  EXPECT_TRUE(prefs.Exists(key));
  auto prefs_non_dlcbase =
      Prefs(JoinPaths(SystemState::Get()->dlc_prefs_dir(), "id",
                      BootSlot::ToString(active_boot_slot)));
  EXPECT_TRUE(prefs_non_dlcbase.Exists(key));
}

TEST_F(PrefsTest, CreateAndDelete) {
  auto prefs = Prefs(FilePath(SystemState::Get()->prefs_dir()));
  string key = "key";
  EXPECT_TRUE(prefs.Create(key));
  EXPECT_TRUE(prefs.Exists(key));
  EXPECT_TRUE(prefs.Delete(key));
  EXPECT_FALSE(prefs.Exists(key));
}

TEST_F(PrefsTest, SetAndGetThenDelete) {
  auto prefs = Prefs(FilePath(SystemState::Get()->prefs_dir()));
  string key = "key", value = "value";
  EXPECT_TRUE(prefs.SetKey(key, value));
  string actual_value;
  EXPECT_TRUE(prefs.GetKey(key, &actual_value));
  EXPECT_EQ(value, actual_value);
  EXPECT_TRUE(prefs.Delete(key));
  EXPECT_FALSE(prefs.Exists(key));
}

TEST_F(PrefsTest, RepeatedSet) {
  auto prefs = Prefs(FilePath(SystemState::Get()->prefs_dir()));
  string key = "key", value = "value";
  EXPECT_TRUE(prefs.SetKey(key, value));
  EXPECT_TRUE(prefs.SetKey(key, value));
}

}  // namespace dlcservice