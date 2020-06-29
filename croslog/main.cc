// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <base/command_line.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/files/file.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/process/launch.h>
#include <base/strings/string_number_conversions.h>

#include "croslog/config.h"
#include "croslog/severity.h"
#include "croslog/viewer_plaintext.h"

namespace {
static const char* kJournalctlCmdPath = "/usr/bin/journalctl";

void ShowUsage() {
  // TODO(yoshiki): Implement the usage.
  LOG(WARNING) << "Usage is not implemented yet.";
}

bool LaunchJournalctlAndWait(const croslog::Config& config) {
  std::vector<std::string> journalctl_command_line = {kJournalctlCmdPath};

  if (config.output != croslog::OutputMode::SHORT) {
    journalctl_command_line.push_back("--output");
    std::string output_mode;
    if (config.output == croslog::OutputMode::EXPORT)
      output_mode = "export";
    else if (config.output == croslog::OutputMode::JSON)
      output_mode = "json";
    else
      output_mode = "short";
    journalctl_command_line.push_back(output_mode);
  }

  if (config.lines >= 0) {
    journalctl_command_line.push_back("--lines");
    journalctl_command_line.push_back(base::NumberToString(config.lines));
  }

  if (config.boot.has_value()) {
    journalctl_command_line.push_back("--boot");
    if (!config.boot->empty())
      journalctl_command_line.push_back(config.boot.value());
  }

  if (!config.identifier.empty()) {
    journalctl_command_line.push_back("--identifier");
    journalctl_command_line.push_back(config.identifier);
  }

  if (config.severity != croslog::Severity::UNSPECIFIED) {
    journalctl_command_line.push_back("--priority");
    journalctl_command_line.push_back(
        base::NumberToString(static_cast<int>(config.severity)));
  }

  if (!config.grep.empty()) {
    journalctl_command_line.push_back("--grep");
    journalctl_command_line.push_back(config.grep);
  }

  if (!config.cursor.empty()) {
    journalctl_command_line.push_back("--cursor");
    journalctl_command_line.push_back(config.cursor);
  }

  if (!config.after_cursor.empty()) {
    journalctl_command_line.push_back("--after-cursor");
    journalctl_command_line.push_back(config.after_cursor);
  }

  if (config.show_cursor) {
    journalctl_command_line.push_back("--show-cursor");
  }

  if (config.quiet) {
    journalctl_command_line.push_back("--quiet");
  }

  if (config.no_pager) {
    journalctl_command_line.push_back("--no-pager");
  }

  if (config.follow) {
    journalctl_command_line.push_back("--follow");
  }

  base::Process process(
      base::LaunchProcess(journalctl_command_line, base::LaunchOptions()));
  if (!process.IsValid())
    return false;
  int exit_code = -1;
  return process.WaitForExit(&exit_code) && (exit_code == 0);
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);

  croslog::Config config;

  bool parse_result =
      config.ParseCommandLineArgs(base::CommandLine::ForCurrentProcess());
  if (!parse_result || config.show_help) {
    ShowUsage();
    return parse_result ? 0 : 1;
  }

  switch (config.source) {
    case croslog::SourceMode::JOURNAL_LOG:
      return LaunchJournalctlAndWait(config) ? 0 : 1;
    case croslog::SourceMode::PLAINTEXT_LOG: {
      // Do not use them directly.
      base::MessageLoopForIO message_loop_;
      base::AtExitManager at_exit_manager_;

      // TODO(yoshiki): Implement the reader of plaintext logs.
      croslog::ViewerPlaintext viewer(config);
      return viewer.Run() ? 0 : 1;
    }
  }
}
