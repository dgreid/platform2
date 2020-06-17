// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  logging::InitLogging(settings);
  logging::SetMinLogLevel(logging::LOG_WARNING);
  base::AtExitManager at_exit_manager;
  base::MessageLoopForIO message_loop;
  base::FileDescriptorWatcher watcher(message_loop.task_runner());
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
