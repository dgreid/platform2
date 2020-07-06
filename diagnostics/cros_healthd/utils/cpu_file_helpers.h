// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_CPU_FILE_HELPERS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_CPU_FILE_HELPERS_H_

#include <string>

#include <base/files/file_path.h>

namespace diagnostics {

// File read from the CPU directory.
extern const char kCpuPresentFile[];
// Files read from the C-state directory.
extern const char kCStateNameFile[];
extern const char kCStateTimeFile[];
// Files read from the CPU policy directory.
extern const char kCpuScalingMaxFreqFile[];
extern const char kCpuScalingCurFreqFile[];
extern const char kCpuinfoMaxFreqFile[];

// Returns an absolute path to the CPU directory. On a real device, this will be
// /sys/devices/system/cpu.
base::FilePath GetCpuDirectoryPath(const base::FilePath& root_dir);

// Returns an absolute path to the C-state directory for the logical CPU with ID
// |logical_id|. On a real device, this will be
// /sys/devices/system/cpu/cpu|logical_id|/cpuidle.
base::FilePath GetCStateDirectoryPath(const base::FilePath& root_dir,
                                      const std::string& logical_id);

// Returns an absolute path to the CPU freq directory for the logical CPU with
// ID |logical_id|. On a real device, this will be
// /sys/devices/system/cpu/cpufreq/policy|logical_id| if the CPU has a governing
// policy, or sys/devices/system/cpu/|logical_id|/cpufreq without.
base::FilePath GetCpuFreqDirectoryPath(const base::FilePath& root_dir,
                                       const std::string& logical_id);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_CPU_FILE_HELPERS_H_
