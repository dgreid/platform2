// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_PROCFS_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_PROCFS_UTILS_H_

#include <cstdint>

#include <base/files/file_path.h>

namespace diagnostics {

// Files read from a process subdirectory of procfs.
extern const char kProcessCmdlineFile[];
extern const char kProcessStatFile[];
extern const char kProcessStatusFile[];

// Returns an absolute path to the procfs subdirectory containing files related
// to the process with ID |pid|. On a real device, this will be /proc/|pid|.
base::FilePath GetProcProcessDirectoryPath(const base::FilePath& root_dir,
                                           int32_t pid);

// Returns an absolute path to the cpuinfo file in procfs. On a real device,
// this will be /proc/cpuinfo.
base::FilePath GetProcCpuInfoPath(const base::FilePath& root_dir);

// Returns an absolute path to the stat file in procfs. On a real device, this
// will be /proc/stat.
base::FilePath GetProcStatPath(const base::FilePath& root_dir);

// Returns an absolute path to the uptime file in procfs. On a real device, this
// will be /proc/uptime.
base::FilePath GetProcUptimePath(const base::FilePath& root_dir);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_PROCFS_UTILS_H_
