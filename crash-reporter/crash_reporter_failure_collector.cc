// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_reporter_failure_collector.h"

#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>

#include "crash-reporter/util.h"

using base::FilePath;

namespace {
const char kExecName[] = "crash_reporter_failure";
}  // namespace

CrashReporterFailureCollector::CrashReporterFailureCollector()
    : CrashCollector("crash-reporter-failure-collector") {}

CrashReporterFailureCollector::~CrashReporterFailureCollector() {}

bool CrashReporterFailureCollector::Collect() {
  LOG(INFO) << "Detected crash_reporter failure";

  FilePath crash_directory;
  if (!GetCreatedCrashDirectoryByEuid(kRootUid, &crash_directory, nullptr)) {
    return false;
  }

  std::string dump_basename = FormatDumpBasename(kExecName, time(nullptr), 0);
  FilePath log_path = GetCrashPath(crash_directory, dump_basename, "log");
  FilePath meta_path = GetCrashPath(crash_directory, dump_basename, "meta");

  bool result = GetLogContents(log_config_path_, kExecName, log_path);
  if (result) {
    FinishCrash(meta_path, kExecName, log_path.BaseName().value());
  }
  return true;
}
