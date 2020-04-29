// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/cpu_file_helpers.h"

#include <base/files/file_path.h>
#include <gtest/gtest.h>

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

TEST(CpuFileHelpers, GetCpuPolicyDirectoryPath) {
  const auto policy_dir =
      GetCpuPolicyDirectoryPath(base::FilePath(kProductionRootDir), kLogicalId);
  EXPECT_EQ(policy_dir.value(), "/sys/devices/system/cpu/cpufreq/policy13");
}

TEST(CpuFileHelpers, GetProcCpuInfoPath) {
  const auto cpuinfo_path =
      GetProcCpuInfoPath(base::FilePath(kProductionRootDir));
  EXPECT_EQ(cpuinfo_path.value(), "/proc/cpuinfo");
}

TEST(CpuFileHelpers, GetProcStatPath) {
  const auto stat_path = GetProcStatPath(base::FilePath(kProductionRootDir));
  EXPECT_EQ(stat_path.value(), "/proc/stat");
}

}  // namespace diagnostics
