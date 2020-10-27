// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/log_entry.h"

namespace croslog {

LogEntry::LogEntry(base::Time time,
                   Severity severity,
                   std::string&& tag,
                   int pid,
                   std::string&& message,
                   std::string&& entire_line)
    : time_(time),
      severity_(severity),
      tag_(std::move(tag)),
      pid_(pid),
      message_(std::move(message)),
      entire_line_(std::move(entire_line)) {}

void LogEntry::AppendLinesToMessage(const std::list<std::string>& lines) {
  for (const auto& line : lines) {
    message_ += "\n";
    message_ += line;
  }
}

}  // namespace croslog
