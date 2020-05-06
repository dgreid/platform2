// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/disk_cleanup_routines.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/mock_homedirs.h"
#include "cryptohome/mock_platform.h"

using ::testing::StrictMock;

namespace cryptohome {

TEST(DiskCleanupRoutines, Init) {
  StrictMock<MockPlatform> platform_;
  StrictMock<MockHomeDirs> homedirs_;

  DiskCleanupRoutines routines(&homedirs_, &platform_);
}

}  // namespace cryptohome
