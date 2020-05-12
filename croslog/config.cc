// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/config.h"

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"

namespace croslog {

bool Config::ParseCommandLineArgs(const base::CommandLine* command_line) {
  bool result = true;

  if (command_line->HasSwitch("help") || command_line->HasSwitch("h")) {
    show_help = true;
  }

  if (command_line->HasSwitch("source")) {
    const std::string& argument = command_line->GetSwitchValueASCII("source");
    if (argument.empty()) {
      LOG(ERROR) << "\"mode\" argument must have a value.";
      result = false;
    } else if (argument == "journal") {
      source = SourceMode::JOURNAL_LOG;
    } else if (argument == "plaintext") {
      source = SourceMode::PLAINTEXT_LOG;
    } else {
      LOG(ERROR) << "Specified 'source' argument is invalid. "
                 << "It must be 'journal' or 'plaintext'.";
      result = false;
    }
  }

  if (command_line->HasSwitch("output"))
    output = command_line->GetSwitchValueASCII("output");

  if (command_line->HasSwitch("lines")) {
    const std::string& lines_str = command_line->GetSwitchValueASCII("lines");
    if (lines_str.empty()) {
      // Default value when the argument is specified without a value.
      lines = 10;
    } else if (base::ToLowerASCII(lines_str) == "all") {
      // Doesn't limit (same as the default behavioru without the argument).
      lines = -1;
    } else if (base::StringToInt(lines_str, &lines)) {
      if (lines < 0) {
        LOG(ERROR) << "--lines argument value must be positive.";
        result = false;
      }
    } else {
      LOG(ERROR) << "--lines argument must be a number.";
      result = false;
    }
  }

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

  if (command_line->HasSwitch("follow"))
    follow = true;

  return result;
}

}  // namespace croslog
