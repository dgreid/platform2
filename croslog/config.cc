// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/config.h"

#include <base/logging.h>

namespace croslog {

bool Config::ParseCommandLineArgs(const base::CommandLine* command_line) {
  if (command_line->HasSwitch("help") || command_line->HasSwitch("h")) {
    show_help = true;
  }

  if (command_line->HasSwitch("source")) {
    const std::string& argument = command_line->GetSwitchValueASCII("source");
    if (argument.empty()) {
      LOG(ERROR) << "\"mode\" argument must have a value.";
      return false;
    } else if (argument == "journal") {
      source = SourceMode::JOURNAL_LOG;
    } else if (argument == "plaintext") {
      source = SourceMode::PLAINTEXT_LOG;
    } else {
      LOG(ERROR) << "Specified 'source' argument is invalid. "
          << "It must be 'journal' or 'plaintext'.";
      return false;
    }
  }

  if (command_line->HasSwitch("output"))
    output = command_line->GetSwitchValueASCII("output");

  if (command_line->HasSwitch("lines"))
    lines = command_line->GetSwitchValueASCII("lines");

  if (command_line->HasSwitch("boot"))
    boot = command_line->GetSwitchValueASCII("boot");

  if (command_line->HasSwitch("identifier"))
    identifier = command_line->GetSwitchValueASCII("identifier");

  if (command_line->HasSwitch("priority"))
    priority = command_line->GetSwitchValueASCII("priority");

  if (command_line->HasSwitch("grep"))
    grep = command_line->GetSwitchValueASCII("grep");

  if (command_line->HasSwitch("cursor"))
    cursor = command_line->GetSwitchValueASCII("cursor");

  if (command_line->HasSwitch("after-cursor"))
    after_cursor = command_line->GetSwitchValueASCII("after-cursor");

  if (command_line->HasSwitch("show-cursor"))
    show_cursor = true;

  if (command_line->HasSwitch("quiet"))
    quiet = true;

  if (command_line->HasSwitch("no-pager"))
    no_pager = true;

  return true;
}

}  // namespace croslog
