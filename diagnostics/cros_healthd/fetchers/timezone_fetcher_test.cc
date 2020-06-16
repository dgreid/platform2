// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/timezone_fetcher.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

constexpr char kLocaltimeFile[] = "var/lib/timezone/localtime";
constexpr char kZoneInfoPath[] = "usr/share/zoneinfo";
constexpr char kTimezoneRegion[] = "America/Denver";
constexpr char kPosixTimezoneFile[] = "MST.tzif";
constexpr char kPosixTimezoneOutput[] = "MST7MDT,M3.2.0,M11.1.0";
constexpr char kSrcPath[] = "cros_healthd/fetchers";

// Test the logic to get and parse the timezone information.
TEST(TimezoneUtils, TestGetTimezone) {
  // Create files and symlinks expected to be present for the localtime file.
  base::ScopedTempDir root;
  ASSERT_TRUE(root.CreateUniqueTempDir());
  base::FilePath timezone_file_path =
      root.GetPath().AppendASCII(kZoneInfoPath).AppendASCII(kTimezoneRegion);
  base::FilePath localtime_path = root.GetPath().AppendASCII(kLocaltimeFile);

  ASSERT_TRUE(
      WriteFileAndCreateSymbolicLink(timezone_file_path, "", localtime_path));

  base::FilePath test_file = base::FilePath(getenv("SRC"))
                                 .AppendASCII(kSrcPath)
                                 .AppendASCII(kPosixTimezoneFile);
  ASSERT_TRUE(base::CopyFile(test_file, timezone_file_path));

  auto timezone_result = FetchTimezoneInfo(root.GetPath());
  ASSERT_TRUE(timezone_result->is_timezone_info());
  const auto& timezone_info = timezone_result->get_timezone_info();
  EXPECT_EQ(timezone_info->posix, kPosixTimezoneOutput);
  EXPECT_EQ(timezone_info->region, kTimezoneRegion);
}

// Test that the function fails gracefully if the files do not exist.
TEST(TimezoneUtils, TestGetTimezoneFailure) {
  base::ScopedTempDir root;
  ASSERT_TRUE(root.CreateUniqueTempDir());

  auto timezone_result = FetchTimezoneInfo(root.GetPath());
  ASSERT_TRUE(timezone_result->is_error());
  EXPECT_EQ(timezone_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kFileReadError);
}

}  // namespace

}  // namespace diagnostics
