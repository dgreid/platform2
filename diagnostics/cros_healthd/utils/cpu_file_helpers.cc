// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/cpu_file_helpers.h"

#include "base/files/file_util.h"

namespace diagnostics {

namespace {

// Relative path to the CPU directory.
constexpr char kRelativeCpuDir[] = "sys/devices/system/cpu";
// The cpu subdirectory of kRelativeCpuDir.
constexpr char kCpuSubdir[] = "cpu";
// The cpuidle subdirectory of kCpuSubdir.
constexpr char kCpuIdleSubdir[] = "cpuidle";
// The cpufreq subdirectory of a logical CPU kCpuSubdir or kRelativeCpuDir.
constexpr char kCpufreqSubdir[] = "cpufreq";
// The policy subdirectory of kCpufreqSubdir.
constexpr char kCpuPolicySubdir[] = "policy";

}  // namespace

const char kCpuPresentFile[] = "present";
const char kCStateNameFile[] = "name";
const char kCStateTimeFile[] = "time";
const char kCpuScalingMaxFreqFile[] = "scaling_max_freq";
const char kCpuScalingCurFreqFile[] = "scaling_cur_freq";
const char kCpuinfoMaxFreqFile[] = "cpuinfo_max_freq";

base::FilePath GetCpuDirectoryPath(const base::FilePath& root_dir) {
  return root_dir.Append(kRelativeCpuDir);
}

base::FilePath GetCStateDirectoryPath(const base::FilePath& root_dir,
                                      const std::string& logical_id) {
  return GetCpuDirectoryPath(root_dir)
      .Append(kCpuSubdir + logical_id)
      .Append(kCpuIdleSubdir);
}

base::FilePath GetCpuFreqDirectoryPath(const base::FilePath& root_dir,
                                       const std::string& logical_id) {
  auto policy_path = GetCpuDirectoryPath(root_dir)
                         .Append(kCpufreqSubdir)
                         .Append(kCpuPolicySubdir + logical_id);

  // If the CPU has a governing policy, return that path, otherwise return the
  // cpufreq directory for the given logical CPU.
  if (base::PathExists(policy_path)) {
    return policy_path;
  } else {
    return GetCpuDirectoryPath(root_dir)
        .Append(kCpuSubdir + logical_id)
        .Append(kCpufreqSubdir);
  }
}

}  // namespace diagnostics
