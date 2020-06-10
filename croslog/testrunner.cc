// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_loop.h"
#include "common-mk/testrunner.h"

int main(int argc, char** argv) {
  // Declaring MessageLoop here, since the singleton object FileChangeWatcher
  // depends on the message loop implicitly.
  base::MessageLoopForIO message_loop_;

  auto runner = platform2::TestRunner(argc, argv);
  return runner.Run();
}
