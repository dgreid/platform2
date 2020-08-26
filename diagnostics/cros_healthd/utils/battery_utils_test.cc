// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/battery_utils.h"

#include <cstdint>
#include <string>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/optional.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"

namespace diagnostics {

namespace {

constexpr uint32_t kChargeNowFileContents = 4031000;
constexpr uint32_t kChargeFullFileContents = 5042000;
constexpr uint32_t kExpectedChargePercent = 80;

}  // namespace

// Test that CalculateBatteryChargePercent() returns the correct battery charge
// percent.
TEST(BatteryUtils, ReturnsCorrectPercent) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath temp_dir_path = temp_dir.GetPath();
  EXPECT_TRUE(WriteFileAndCreateParentDirs(
      temp_dir_path.AppendASCII(kBatteryDirectoryPath)
          .AppendASCII(kBatteryChargeNowFileName),
      std::to_string(kChargeNowFileContents)));
  EXPECT_TRUE(WriteFileAndCreateParentDirs(
      temp_dir_path.AppendASCII(kBatteryDirectoryPath)
          .AppendASCII(kBatteryChargeFullFileName),
      std::to_string(kChargeFullFileContents)));

  base::Optional<uint32_t> charge_percent =
      CalculateBatteryChargePercent(temp_dir_path);

  ASSERT_TRUE(charge_percent.has_value());
  EXPECT_EQ(charge_percent.value(), kExpectedChargePercent);
}

// Test that CalculateBatteryChargePercent() handles a missing charge now file.
TEST(BatteryUtils, MissingChargeNow) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath temp_dir_path = temp_dir.GetPath();
  EXPECT_TRUE(WriteFileAndCreateParentDirs(
      temp_dir_path.AppendASCII(kBatteryDirectoryPath)
          .AppendASCII(kBatteryChargeFullFileName),
      std::to_string(kChargeFullFileContents)));

  base::Optional<uint32_t> charge_percent =
      CalculateBatteryChargePercent(temp_dir_path);

  EXPECT_FALSE(charge_percent.has_value());
}

// Test that CalculateBatteryChargePercent() handles a missing charge full file.
TEST(BatteryUtils, MissingChargeFull) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath temp_dir_path = temp_dir.GetPath();
  EXPECT_TRUE(WriteFileAndCreateParentDirs(
      temp_dir_path.AppendASCII(kBatteryDirectoryPath)
          .AppendASCII(kBatteryChargeNowFileName),
      std::to_string(kChargeNowFileContents)));

  base::Optional<uint32_t> charge_percent =
      CalculateBatteryChargePercent(temp_dir_path);

  EXPECT_FALSE(charge_percent.has_value());
}

}  // namespace diagnostics
