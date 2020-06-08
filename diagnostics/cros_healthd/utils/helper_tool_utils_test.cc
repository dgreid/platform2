// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/helper_tool_utils.h"

#include <string>

#include <gtest/gtest.h>

namespace diagnostics {

TEST(HelperUtilsTest, GetHelperPath) {
  std::string full_path;

  EXPECT_TRUE(GetHelperPath("", &full_path));
  EXPECT_EQ("/usr/libexec/healthd/helpers/", full_path);

  EXPECT_TRUE(GetHelperPath("test/me", &full_path));
  EXPECT_EQ("/usr/libexec/healthd/helpers/test/me", full_path);
}

}  // namespace diagnostics
