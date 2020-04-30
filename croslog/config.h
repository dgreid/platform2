// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROSLOG_CONFIG_H_
#define CROSLOG_CONFIG_H_

#include <string>

#include <base/command_line.h>
#include <base/optional.h>

namespace croslog {

enum class SourceMode {
  JOURNAL_LOG,
  PLAINTEXT_LOG
};

struct Config {
  bool ParseCommandLineArgs(const base::CommandLine* command_line);

  // Source of logs: see |SourceMode| enum class.
  SourceMode source = SourceMode::JOURNAL_LOG;
  // Formatting of logs which are shown.
  std::string output;
  // Number to limit the lines of logs shown
  std::string lines;
  // Boot ID to show messages only from the specific boot.
  base::Optional<std::string> boot;
  // String to show message for the specified syslog identifier
  // (SYSLOG_IDENTIFIER).
  std::string identifier;
  // String to filter output by message priorities or priority ranges.
  std::string priority;
  // Regexp string to filter output to entries where the MESSAGE= field matches
  // with.
  std::string grep;
  // Log cursor string to start showing entries from the specified location.
  std::string cursor;
  // Log cursor string to start showing entries after the specified location.
  std::string after_cursor;
  // Flag to show the cursor after the last entry.
  bool show_cursor = false;
  // Flag to suppress all informational messages.
  bool quiet = false;
  // Flag not to pipe output into a pager program.
  bool no_pager = false;
  // Flag to print a help text instead of logs.
  bool show_help = false;
};

}  // namespace croslog

#endif  // CROSLOG_CONFIG_H_
