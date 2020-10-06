// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <base/command_line.h>
#include <base/logging.h>
#include <base/task/single_thread_task_executor.h>

#include <brillo/syslog_logging.h>

#include "croslog/config.h"
#include "croslog/viewer_journal.h"
#include "croslog/viewer_plaintext.h"

namespace {

void ShowUsage() {
  // TODO(yoshiki): Implement the usage.
  LOG(WARNING) << "Usage is not implemented yet.";
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  const base::CommandLine* const command_line =
      base::CommandLine::ForCurrentProcess();

  // Configure the log destination. This should be placed before any code which
  // potentially write logs.
  int log_flags = brillo::kLogToStderr;
  // if the stdin is not tty, send logs to syslog as well.
  if (!isatty(0) || command_line->HasSwitch("send-syslog"))
    log_flags |= brillo::kLogToSyslog;
  brillo::InitLog(log_flags);

  croslog::Config config;
  bool parse_result = config.ParseCommandLineArgs(command_line);
  if (!parse_result || config.show_help) {
    ShowUsage();
    return parse_result ? 0 : 1;
  }

  switch (config.source) {
    case croslog::SourceMode::JOURNAL_LOG:
      croslog::ViewerJournal viewer;
      return viewer.Run(config) ? 0 : 1;
    case croslog::SourceMode::PLAINTEXT_LOG: {
      // Do not use them directly.
      base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
      base::AtExitManager at_exit_manager_;

      // TODO(yoshiki): Implement the reader of plaintext logs.
      croslog::ViewerPlaintext viewer(config);
      return viewer.Run() ? 0 : 1;
    }
  }
}
