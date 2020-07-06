// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROSLOG_VIEWER_PLAINTEXT_H_
#define CROSLOG_VIEWER_PLAINTEXT_H_

#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "re2/re2.h"

#include "croslog/config.h"
#include "croslog/file_change_watcher.h"
#include "croslog/multiplexer.h"

namespace croslog {

class ViewerPlaintext : public Multiplexer::Observer {
 public:
  explicit ViewerPlaintext(const croslog::Config& config);

  // Run the plaintext viewer. This may run the runloop to retrieve update
  // events.
  bool Run();

 private:
  FRIEND_TEST(ViewerPlaintextTest, ShouldFilterOutEntry);

  enum class CursorMode { UNSPECIFIED, SAME_AND_NEWER, NEWER };

  base::RunLoop run_loop_;
  base::Closure quit_closure_{run_loop_.QuitWhenIdleClosure()};

  const croslog::Config config_;
  base::Optional<RE2> config_grep_;
  CursorMode config_cursor_mode_ = CursorMode::UNSPECIFIED;
  base::Time config_cursor_time_;
  bool config_show_cursor_ = false;

  Multiplexer multiplexer_;

  void OnLogFileChanged() override;

  bool ShouldFilterOutEntry(const LogEntry& e);

  void ReadRemainingLogs();

  std::vector<std::pair<std::string, std::string>> GenerateKeyValues(
      const LogEntry& entry);

  void WriteLog(const LogEntry& entry);
  void WriteLogInExportFormat(const LogEntry& entry);
  void WriteLogInJsonFormat(const LogEntry& entry);
  void WriteOutput(const std::string& str);
  void WriteOutput(const char* str, size_t size);

  DISALLOW_COPY_AND_ASSIGN(ViewerPlaintext);
};

}  // namespace croslog

#endif  // CROSLOG_VIEWER_PLAINTEXT_H_
