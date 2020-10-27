// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/log_parser_syslog.h"

#include <memory>
#include <string>
#include <utility>

#include "base/strings/string_number_conversions.h"

namespace {
// The length of time string like "2020-05-25T00:00:00.000000+00:00".
constexpr size_t kTimeStringLengthWithTimeZone = 32;
// The length of time string like "2020-05-25T00:00:00.000000Z".
constexpr size_t kTimeStringLengthUTC = 27;

int ParseTime(const std::string& entire_line, base::Time* time) {
  DCHECK_NE(nullptr, time);

  if (entire_line[26] == 'Z') {
    // Case of UTC time format like "2020-05-25T00:00:00.000000Z".
    std::string log_time = entire_line.substr(0, kTimeStringLengthUTC);

    bool result = base::Time::FromString(log_time.c_str(), time);
    if (!result)
      return -1;

    return kTimeStringLengthUTC;
  } else if (entire_line[26] == '+' || entire_line[26] == '-') {
    // Case of format with time-zone like "2020-05-25T00:00:00.000000+00:00".
    std::string log_time = entire_line.substr(0, kTimeStringLengthWithTimeZone);

    bool result = base::Time::FromString(log_time.c_str(), time);
    if (!result)
      return -1;

    return kTimeStringLengthWithTimeZone;
  }

  return -1;
}

}  // namespace

namespace croslog {

LogParserSyslog::LogParserSyslog() = default;

MaybeLogEntry LogParserSyslog::ParseInternal(std::string&& entire_line) {
  if (entire_line.empty()) {
    // Returns an invalid value if the line is invalid or empty.
    return base::nullopt;
  }

  if (entire_line.size() < kTimeStringLengthUTC) {
    // Parse failed. Maybe this line doesn't contains a header.
    return base::nullopt;
  }

  base::Time time;
  int message_start_pos = ParseTime(entire_line, &time);
  if (message_start_pos < 0) {
    // Parse failed. Maybe this line doesn't contains a header.
    return base::nullopt;
  }

  int pos = message_start_pos;
  if (entire_line[pos] != ' ') {
    // Parse failed. Maybe this line doesn't contains a header.
    return base::nullopt;
  }

  std::string severity_str;
  if (entire_line[pos] == ' ') {
    for (int i = pos + 1; i < entire_line.size(); i++) {
      if (entire_line[i] == ' ') {
        severity_str = entire_line.substr(pos + 1, i - pos - 1);
        pos = i;
        break;
      }
    }
  }

  Severity severity = Severity::UNSPECIFIED;
  if (!severity_str.empty()) {
    severity = SeverityFromString(severity_str);
  }

  std::string tag;
  if (entire_line[pos] == ' ') {
    for (int i = pos + 1; i < entire_line.size(); i++) {
      if (entire_line[i] == '[' || entire_line[i] == ':' ||
          entire_line[i] == ' ') {
        tag = entire_line.substr(pos + 1, i - pos - 1);
        pos = i;
        break;
      }
    }
  }

  int pid = -1;
  if (entire_line[pos] == '[') {
    for (int i = pos + 1; i < entire_line.size(); i++) {
      if (entire_line[i] == ']') {
        std::string pid_str = entire_line.substr(pos + 1, i - pos - 1);
        if (!base::StringToInt(pid_str, &pid))
          pid = -1;
        pos = i;
        break;
      }
    }
    DCHECK_EQ(']', entire_line[pos]);
    pos++;
  }

  if (entire_line.size() > pos && entire_line[pos] == ':')
    pos++;

  std::string message;
  if (entire_line.size() > pos) {
    if (entire_line[pos] != ' ') {
      // Parse failed. Maybe this line doesn't contains a header.
      return base::nullopt;
    }

    pos++;
    message = entire_line.substr(pos, entire_line.size() - pos);
  }

  return LogEntry{time, severity,           std::move(tag),
                  pid,  std::move(message), std::move(entire_line)};
}

}  // namespace croslog
