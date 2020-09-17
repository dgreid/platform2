// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/procfs_utils.h"

#include <string>

#include "base/files/file_util.h"

namespace diagnostics {

const char kProcessCmdlineFile[] = "cmdline";
const char kProcessStatFile[] = "stat";
const char kProcessStatmFile[] = "statm";
const char kProcessStatusFile[] = "status";

base::FilePath GetProcProcessDirectoryPath(const base::FilePath& root_dir,
                                           pid_t pid) {
  return root_dir.Append("proc").Append(std::to_string(pid));
}

base::FilePath GetProcCpuInfoPath(const base::FilePath& root_dir) {
  return root_dir.Append("proc/cpuinfo");
}

base::FilePath GetProcStatPath(const base::FilePath& root_dir) {
  return root_dir.Append("proc/stat");
}

base::FilePath GetProcUptimePath(const base::FilePath& root_dir) {
  return root_dir.Append("proc/uptime");
}

}  // namespace diagnostics
