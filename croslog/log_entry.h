// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROSLOG_LOG_ENTRY_H_
#define CROSLOG_LOG_ENTRY_H_

#include <string>
#include <utility>

#include "base/strings/string_piece.h"
#include "base/time/time.h"

#include "croslog/severity.h"

namespace croslog {

class LogEntry {
 public:
  LogEntry(base::Time time,
           Severity severity,
           std::string&& tag,
           int pid,
           std::string&& message,
           std::string&& entire_string);
  LogEntry(LogEntry&& other) = default;
  LogEntry(const LogEntry&) = delete;
  LogEntry& operator=(const LogEntry&) = delete;

  const std::string& entire_line() const { return entire_line_; }
  base::Time time() const { return time_; }
  Severity severity() const { return severity_; }
  const std::string& tag() const { return tag_; }
  const int pid() const { return pid_; }
  const std::string& message() const { return message_; }

 private:
  const base::Time time_;
  const Severity severity_;
  const std::string tag_;
  const int pid_;
  const std::string message_;
  const std::string entire_line_;
};

}  // namespace croslog

#endif  // CROSLOG_LOG_ENTRY_H_
