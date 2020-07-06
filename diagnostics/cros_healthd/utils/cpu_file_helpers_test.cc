// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/cpu_file_helpers.h"

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"

namespace diagnostics {

namespace {

// Production instances will use a root directory of "/".
constexpr char kProductionRootDir[] = "/";

// Logical ID to test with.
constexpr char kLogicalId[] = "13";

}  // namespace

TEST(CpuFileHelpers, GetCpuDirectoryPath) {
  const auto cpu_dir = GetCpuDirectoryPath(base::FilePath(kProductionRootDir));
  EXPECT_EQ(cpu_dir.value(), "/sys/devices/system/cpu");
}

TEST(CpuFileHelpers, GetCStateDirectoryPath) {
  const auto c_state_dir =
      GetCStateDirectoryPath(base::FilePath(kProductionRootDir), kLogicalId);
  EXPECT_EQ(c_state_dir.value(), "/sys/devices/system/cpu/cpu13/cpuidle");
}

TEST(CpuFileHelpers, GetCpuFreqPolicyDirectoryPath) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const std::string cpufreq_path = "sys/devices/system/cpu/cpufreq/policy13";

  const auto expected_policy_dir = temp_dir.GetPath().Append(cpufreq_path);
  ASSERT_TRUE(WriteFileAndCreateParentDirs(expected_policy_dir, ""));

  const auto freq_dir = GetCpuFreqDirectoryPath(temp_dir.GetPath(), kLogicalId);
  EXPECT_EQ(freq_dir, temp_dir.GetPath().Append(cpufreq_path));
}

TEST(CpuFileHelpers, GetCpuFreqDirectoryPath) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const std::string cpufreq_path = "sys/devices/system/cpu/cpu13/cpufreq";

  const auto expected_policy_dir = temp_dir.GetPath().Append(cpufreq_path);
  ASSERT_TRUE(WriteFileAndCreateParentDirs(expected_policy_dir, ""));

  const auto freq_dir = GetCpuFreqDirectoryPath(temp_dir.GetPath(), kLogicalId);
  EXPECT_EQ(freq_dir, temp_dir.GetPath().Append(cpufreq_path));
}

}  // namespace diagnostics
