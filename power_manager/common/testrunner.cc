// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/test/task_environment.h>
#include <base/test/test_timeouts.h>

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  logging::InitLogging(settings);
  logging::SetMinLogLevel(logging::LOGGING_WARNING);
  base::AtExitManager at_exit_manager;
  TestTimeouts::Initialize();
  // TODO(crbug/1094927): Use SingleThreadkTaskEnvironment.
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY,
      base::test::TaskEnvironment::MainThreadType::IO);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
