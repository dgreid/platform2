// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/time/default_clock.h>
#include <brillo/syslog_logging.h>

#include "crash-reporter/crash_sender_base.h"
#include "crash-reporter/crash_sender_paths.h"
#include "crash-reporter/crash_sender_util.h"
#include "crash-reporter/crash_serializer.h"
#include "crash-reporter/paths.h"

int main(int argc, char* argv[]) {
  // Log to both stderr and syslog so that automated SSH connections can see
  // error output.
  brillo::OpenLog("crash_serializer", true);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);

  crash_serializer::Serializer::Options options;
  auto clock = std::make_unique<base::DefaultClock>();
  // TODO(mutexlox): Add a command-line flag to determine whether to fetch
  // cores.
  options.fetch_coredumps = false;

  crash_serializer::Serializer serializer(std::move(clock), options);

  // Get all crashes.
  std::vector<base::FilePath> crash_directories;
  crash_directories = serializer.GetUserCrashDirectories();
  crash_directories.push_back(paths::Get(paths::kSystemCrashDirectory));
  crash_directories.push_back(paths::Get(paths::kFallbackUserCrashDirectory));

  std::vector<util::MetaFile> reports_to_send;

  // Pick the reports to serialize.
  base::File lock_file(serializer.AcquireLockFileOrDie());
  for (const auto& directory : crash_directories) {
    serializer.PickCrashFiles(directory, &reports_to_send);
  }
  lock_file.Close();

  // Actually serialize them.
  serializer.SerializeCrashes(reports_to_send);
}
