// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <base/command_line.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>

#include "shill/cli/top_command.h"

// Allow for LOG(INFO) to look like plain output, while providing the usual
// file and line information for both verbose and warning/error messages.
bool HandleMessage(int severity,
                   const char* /* file */,
                   int /* line */,
                   size_t message_start,
                   const std::string& message) {
  bool skip_header = severity == logging::LOGGING_INFO;
  const char* str = message.c_str();
  if (skip_header) {
    str += message_start;
  }

  if (severity <= logging::LOGGING_INFO) {
    std::cout << str << std::flush;
    return true;
  }

  std::cerr << str;
  return severity != logging::LOGGING_FATAL;
}

int main(int argc, char** argv) {
  DEFINE_int32(log_level, 0,
               "Logging level - 0: LOG(INFO), 1: LOG(WARNING), 2: LOG(ERROR), "
               "-1: VLOG(1), -2: VLOG(2), ...");
  brillo::FlagHelper::Init(argc, argv, "Shill Command Line Interface");
  logging::SetLogMessageHandler(HandleMessage);
  logging::SetMinLogLevel(FLAGS_log_level);

  std::vector<std::string> args =
      base::CommandLine::ForCurrentProcess()->GetArgs();

  shill_cli::TopCommand cmd;
  return !cmd.Run(args.cbegin(), args.cend(), argv[0]);
}
