// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>
#include "base/files/file_util.h"

#include "croslog/viewer_plaintext.h"

namespace croslog {

namespace {

const char* kLogSources[] = {
    // TOOD(yoshiki): add all sources.
    "/var/log/messages",
    "/var/log/net.log",
};

}  // anonymous namespace

ViewerPlaintext::ViewerPlaintext() {}

bool ViewerPlaintext::Run(const croslog::Config& config) {
  bool install_change_watcher = config.follow;
  for (size_t i = 0; i < base::size(kLogSources); i++) {
    multiplexer_.AddSource(base::FilePath(kLogSources[i]),
                           install_change_watcher);
  }

  multiplexer_.AddObserver(this);

  if (config.lines >= 0) {
    multiplexer_.SetLinesFromLast(config.lines);
  } else if (config.follow) {
    multiplexer_.SetLinesFromLast(10);
  }

  ReadRemainingLogs();

  if (config.follow) {
    // Wait for file changes.
    run_loop_.Run();
  }

  return true;
}

void ViewerPlaintext::OnLogFileChanged() {
  ReadRemainingLogs();
}

void ViewerPlaintext::ReadRemainingLogs() {
  while (true) {
    const RawLogLineUnsafe& s = multiplexer_.Forward();
    if (s.data() == nullptr)
      return;
    if (!s.empty())
      WriteOutput(s);
    if (s.empty() || s[s.size() - 1] != '\n')
      WriteOutput("\n", 1);
  }
}

void ViewerPlaintext::WriteOutput(const RawLogLineUnsafe& str) {
  WriteOutput(str.data(), str.size());
}

void ViewerPlaintext::WriteOutput(const char* str, size_t size) {
  bool write_stdout_result =
      base::WriteFileDescriptor(STDOUT_FILENO, str, size);
  CHECK(write_stdout_result);
}

}  // namespace croslog
