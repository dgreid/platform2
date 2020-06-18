// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <string>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <brillo/message_loops/base_message_loop.h>

#include "system-proxy/server_proxy.h"

int main(int argc, char* argv[]) {
  base::MessageLoopForIO message_loop;
  base::FileDescriptorWatcher watcher(message_loop.task_runner());
  base::RunLoop run_loop;

  system_proxy::ServerProxy server(run_loop.QuitClosure());
  server.Init();
  run_loop.Run();
  return 0;
}
