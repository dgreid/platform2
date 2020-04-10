// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/utils/cpu_utils.h"

namespace diagnostics {

namespace {

constexpr char kRelativeCpuinfoPath[] = "proc/cpuinfo";
constexpr char kFirstRelativeCpuinfoMaxFreqPath[] =
    "sys/devices/system/cpu/cpufreq/policy0/cpuinfo_max_freq";
constexpr char kSecondRelativeCpuinfoMaxFreqPath[] =
    "sys/devices/system/cpu/cpufreq/policy2/cpuinfo_max_freq";

constexpr char kBadCpuinfoContents[] =
    "processor\t: 0\nmodel name\t: Dank CPU 1 @ 8.90GHz\n\n";
constexpr char kFakeCpuinfoContents[] =
    "processor\t: 0\nmodel name\t: Dank CPU 1 @ 8.90GHz\nphysical id\t: 0\n\n"
    "processor\t: 1\nmodel name\t: Dank CPU 1 @ 8.90GHz\nphysical id\t: 0\n\n"
    "processor\t: 2\nmodel name\t: Dank CPU 2 @ 2.80GHz\nphysical id\t: 1\n\n";
constexpr char kFirstFakeModelName[] = "Dank CPU 1 @ 8.90GHz";
constexpr char kSecondFakeModelName[] = "Dank CPU 2 @ 2.80GHz";

constexpr uint32_t kFirstFakeMaxClockSpeed = 3400000;
constexpr uint32_t kSecondFakeMaxClockSpeed = 1600000;

}  // namespace

// Test that CPU info can be read when it exists.
TEST(CpuUtils, TestFetchCpuInfo) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeCpuinfoPath), kFakeCpuinfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kFirstRelativeCpuinfoMaxFreqPath),
      base::NumberToString(kFirstFakeMaxClockSpeed)));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kSecondRelativeCpuinfoMaxFreqPath),
      base::NumberToString(kSecondFakeMaxClockSpeed)));
  auto cpu_result = FetchCpuInfo(root_dir);
  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  EXPECT_EQ(cpu_info.size(), 2);
  EXPECT_EQ(cpu_info[0]->model_name, kFirstFakeModelName);
  EXPECT_EQ(cpu_info[0]->max_clock_speed_khz, kFirstFakeMaxClockSpeed);
  EXPECT_EQ(cpu_info[1]->model_name, kSecondFakeModelName);
  EXPECT_EQ(cpu_info[1]->max_clock_speed_khz, kSecondFakeMaxClockSpeed);
}

// Test that attempting to read CPU info that does not exist fails gracefully.
TEST(CpuUtils, TestFetchCpuInfoNoFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  auto cpu_result = FetchCpuInfo(temp_dir.GetPath());
  EXPECT_TRUE(cpu_result->is_error());
}

// Test that failing to parse CPU info fails gracefully.
TEST(CpuUtils, TestFetchCpuInfoBadCpuinfo) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeCpuinfoPath), kBadCpuinfoContents));
  auto cpu_result = FetchCpuInfo(root_dir);
  EXPECT_TRUE(cpu_result->is_error());
}

// Test that attempting to read a max frequency file that does not exist fails
// gracefully.
TEST(CpuUtils, TestFetchCpuInfoNoMaxFreqFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeCpuinfoPath), kFakeCpuinfoContents));
  auto cpu_result = FetchCpuInfo(root_dir);
  EXPECT_TRUE(cpu_result->is_error());
}

}  // namespace diagnostics
