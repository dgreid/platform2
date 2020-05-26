// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROSLOG_LOG_ENTRY_H_
#define CROSLOG_LOG_ENTRY_H_

#include <string>
#include <utility>

#include "base/strings/string_piece.h"
#include "base/time/time.h"

namespace croslog {

class LogEntry {
 public:
  LogEntry(base::Time time, std::string&& entire_string);

  const std::string& entire_line() const { return entire_line_; }
  base::Time time() const { return time_; }

 private:
  base::Time time_;
  std::string entire_line_;
};

}  // namespace croslog

#endif  // CROSLOG_LOG_ENTRY_H_
