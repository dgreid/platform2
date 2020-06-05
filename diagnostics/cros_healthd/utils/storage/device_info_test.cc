// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>

#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/utils/storage/device_info.h"

namespace diagnostics {

TEST(StorageDeviceInfoTest, SimpleTest) {
  constexpr char path[] = "test/sys/path";
  constexpr char devnode[] = "dev/node/path";
  constexpr char subsystem[] = "test_subsystem";
  StorageDeviceInfo dev_info(base::FilePath(path), base::FilePath(devnode),
                             subsystem);
  EXPECT_EQ(path, dev_info.GetSysPath().value());
  EXPECT_EQ(devnode, dev_info.GetDevNodePath().value());
  EXPECT_EQ(subsystem, dev_info.GetSubsystem());
}

}  // namespace diagnostics
