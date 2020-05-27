// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/periodic_scheduler.h"

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/syslog_logging.h>

int main(int argc, char** argv) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  if (argc < 5) {
    LOG(ERROR) << "Usage: periodic_scheduler <period_seconds> <timeout_seconds>"
                  " <task_name> <task_binary>";
    return 1;
  }

  int64_t period = 0;
  if (!base::StringToInt64(std::string(argv[1]), &period)) {
    LOG(ERROR) << "Invalid value for delay";
    return 1;
  }

  int64_t timeout = 0;
  if (!base::StringToInt64(std::string(argv[2]), &timeout)) {
    LOG(ERROR) << "Invalid value for timeout";
    return 1;
  }

  std::string task_name(argv[3]);

  // Shift arguments.
  argc -= 4;
  argv += 4;

  std::vector<std::string> args(argv, argv + argc);

  PeriodicScheduler p(base::TimeDelta::FromSeconds(period),
                      base::TimeDelta::FromSeconds(timeout), task_name, args);

  return p.Run() == true ? 0 : 1;
}
