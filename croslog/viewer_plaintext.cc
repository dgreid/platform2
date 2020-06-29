// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/viewer_plaintext.h"

#include <memory>
#include <unistd.h>
#include <utility>

#include "base/files/file_util.h"
#include "base/json/string_escape.h"
#include "base/strings/string_number_conversions.h"

#include "croslog/log_parser_syslog.h"
#include "croslog/severity.h"

namespace croslog {

namespace {

const char* kLogSources[] = {
    // TOOD(yoshiki): add all sources.
    "/var/log/messages",
    "/var/log/net.log",
};

}  // anonymous namespace

ViewerPlaintext::ViewerPlaintext(const croslog::Config& config)
    : config_(config) {
  if (config.grep.empty())
    return;

  config_grep_.emplace(config.grep);
  if (!config_grep_->ok())
    config_grep_.reset();
}

bool ViewerPlaintext::Run() {
  bool install_change_watcher = config_.follow;
  for (size_t i = 0; i < base::size(kLogSources); i++) {
    multiplexer_.AddSource(base::FilePath(kLogSources[i]),
                           std::make_unique<LogParserSyslog>(),
                           install_change_watcher);
  }

  if (config_.follow) {
    multiplexer_.AddObserver(this);
  }

  if (config_.lines >= 0) {
    multiplexer_.SetLinesFromLast(config_.lines);
  } else if (config_.follow) {
    multiplexer_.SetLinesFromLast(10);
  }

  ReadRemainingLogs();

  if (config_.follow) {
    // Wait for file changes.
    run_loop_.Run();

    multiplexer_.RemoveObserver(this);
  }

  return true;
}

void ViewerPlaintext::OnLogFileChanged() {
  ReadRemainingLogs();
}

bool ViewerPlaintext::ShouldFilterOutEntry(const LogEntry& e) {
  const std::string& tag = e.tag();
  if (!config_.identifier.empty() && config_.identifier != tag)
    return true;

  const Severity severity = e.severity();
  if (config_.severity != Severity::UNSPECIFIED && config_.severity < severity)
    return true;

  const std::string& message = e.message();
  if (config_grep_.has_value() && !RE2::PartialMatch(message, *config_grep_))
    return true;

  return false;
}

void ViewerPlaintext::ReadRemainingLogs() {
  while (true) {
    const MaybeLogEntry& e = multiplexer_.Forward();
    if (!e.has_value())
      break;

    if (ShouldFilterOutEntry(*e))
      continue;

    WriteLog(*e);
  }
}

std::vector<std::pair<std::string, std::string>>
ViewerPlaintext::GenerateKeyValues(const LogEntry& e) {
  std::vector<std::pair<std::string, std::string>> kvs;
  kvs.push_back(std::make_pair(
      "PRIORITY", base::NumberToString(static_cast<int>(e.severity()))));
  kvs.push_back(std::make_pair("SYSLOG_IDENTIFIER", e.tag()));
  if (e.pid() != -1) {
    kvs.push_back(std::make_pair("SYSLOG_PID", base::NumberToString(e.pid())));
    kvs.push_back(std::make_pair("_PID", base::NumberToString(e.pid())));
  }
  kvs.push_back(std::make_pair("MESSAGE", e.message()));
  return kvs;
}

void ViewerPlaintext::WriteLog(const LogEntry& entry) {
  if (config_.output == OutputMode::EXPORT)
    return WriteLogInExportFormat(entry);
  if (config_.output == OutputMode::JSON)
    return WriteLogInJsonFormat(entry);

  const std::string& s = entry.entire_line();
  WriteOutput(s);
  WriteOutput("\n", 1);
}

void ViewerPlaintext::WriteLogInExportFormat(const LogEntry& entry) {
  const auto&& kvs = GenerateKeyValues(entry);
  for (const auto& kv : kvs) {
    WriteOutput(kv.first);
    WriteOutput("=", 1);
    WriteOutput(kv.second);
    WriteOutput("\n", 1);
  }
  WriteOutput("\n", 1);
}

void ViewerPlaintext::WriteLogInJsonFormat(const LogEntry& entry) {
  const auto&& kvs = GenerateKeyValues(entry);
  bool first = true;
  WriteOutput("{", 1);
  for (const auto& kv : kvs) {
    std::string escaped_value;
    bool ret_value = base::EscapeJSONString(kv.second, true, &escaped_value);
    if (!ret_value)
      escaped_value = "<<INVALID>>";

    if (!first)
      WriteOutput(", \"", 3);
    else
      WriteOutput("\"", 1);
    // All keys are hard-corded and unnecessary to escape.
    WriteOutput(kv.first);
    WriteOutput("\": ", 3);
    WriteOutput(escaped_value);
    first = false;
  }
  WriteOutput("}\n", 2);
}

void ViewerPlaintext::WriteOutput(const std::string& str) {
  WriteOutput(str.data(), str.size());
}

void ViewerPlaintext::WriteOutput(const char* str, size_t size) {
  bool write_stdout_result =
      base::WriteFileDescriptor(STDOUT_FILENO, str, size);
  CHECK(write_stdout_result);
}

}  // namespace croslog
