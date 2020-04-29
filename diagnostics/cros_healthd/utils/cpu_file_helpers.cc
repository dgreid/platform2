// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/cpu_file_helpers.h"

namespace diagnostics {

namespace {

// Relative path to the CPU directory.
constexpr char kRelativeCpuDir[] = "sys/devices/system/cpu";
// The cpu subdirectory of kRelativeCpuDir.
constexpr char kCpuSubdir[] = "cpu";
// The cpuidle subdirectory of kCpuSubdir.
constexpr char kCpuIdleSubdir[] = "cpuidle";
// The cpufreq/policy subdirectory of kRelativeCpuDir.
constexpr char kCpufreqPolicySubdir[] = "cpufreq/policy";

}  // namespace

const char kCpuPresentFile[] = "present";
const char kCStateNameFile[] = "name";
const char kCStateTimeFile[] = "time";
const char kCpuPolicyScalingMaxFreqFile[] = "scaling_max_freq";
const char kCpuPolicyScalingCurFreqFile[] = "scaling_cur_freq";
const char kCpuPolicyCpuinfoMaxFreqFile[] = "cpuinfo_max_freq";

base::FilePath GetCpuDirectoryPath(const base::FilePath& root_dir) {
  return root_dir.Append(kRelativeCpuDir);
}

base::FilePath GetCStateDirectoryPath(const base::FilePath& root_dir,
                                      const std::string& logical_id) {
  return GetCpuDirectoryPath(root_dir)
      .Append(kCpuSubdir + logical_id)
      .Append(kCpuIdleSubdir);
}

base::FilePath GetCpuPolicyDirectoryPath(const base::FilePath& root_dir,
                                         const std::string& logical_id) {
  return GetCpuDirectoryPath(root_dir).Append(kCpufreqPolicySubdir +
                                              logical_id);
}

base::FilePath GetProcCpuInfoPath(const base::FilePath& root_dir) {
  return root_dir.Append("proc/cpuinfo");
}

base::FilePath GetProcStatPath(const base::FilePath& root_dir) {
  return root_dir.Append("proc/stat");
}

}  // namespace diagnostics
