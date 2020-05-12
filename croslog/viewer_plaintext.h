// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROSLOG_VIEWER_PLAINTEXT_H_
#define CROSLOG_VIEWER_PLAINTEXT_H_

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"

#include "croslog/config.h"
#include "croslog/file_change_watcher.h"
#include "croslog/multiplexer.h"

namespace croslog {

class ViewerPlaintext : public Multiplexer::Observer {
 public:
  ViewerPlaintext();

  // Run the plaintext viewer. This may run the runloop to retrieve update
  // events.
  bool Run(const croslog::Config& config);

 private:
  // Do not use them directly.
  base::AtExitManager at_exit_manager_;
  base::MessageLoopForIO message_loop_;

  base::RunLoop run_loop_;
  base::Closure quit_closure_{run_loop_.QuitWhenIdleClosure()};

  void OnLogFileChanged() override;

  void ReadRemainingLogs();

  void WriteOutput(const RawLogLineUnsafe& str);
  void WriteOutput(const char* str, size_t size);

  Multiplexer multiplexer_;

  DISALLOW_COPY_AND_ASSIGN(ViewerPlaintext);
};

}  // namespace croslog

#endif  // CROSLOG_VIEWER_PLAINTEXT_H_
