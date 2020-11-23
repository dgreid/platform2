// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml_benchmark/sysmetrics.h"

#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <re2/re2.h>

#include <string>

namespace {
static const char* kProcFile = "/proc/self/status";

int GetStatusField(const std::string field_name) {
  const std::string field_matcher = field_name + ":\\s+(\\d+)\\s+kB";
  std::string status;
  int value;

  CHECK(base::ReadFileToString(base::FilePath(kProcFile), &status))
      << "Could not read " << kProcFile;
  if (!RE2::PartialMatch(status, field_matcher, &value)) {
    LOG(ERROR) << "Couldn't parse " << field_name << " from " << kProcFile;
    return -1;
  }

  return value;
}
}  // namespace

namespace ml_benchmark {

int GetVMSizeBytes() {
  return GetStatusField("VmSize") * 1024;
}

int GetVMPeakBytes() {
  return GetStatusField("VmPeak") * 1024;
}

}  // namespace ml_benchmark
