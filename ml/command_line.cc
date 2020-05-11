// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <base/at_exit.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/flag_helper.h>
#include <brillo/message_loops/base_message_loop.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "ml/simple.h"

// Starts environment to support Mojo
void StartMojo() {
  (new brillo::BaseMessageLoop())->SetAsCurrent();
  mojo::core::Init();
  mojo::core::ScopedIPCSupport _(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);
}

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  StartMojo();

  // TODO(avg): add flag to specify that NNAPI should be used
  DEFINE_double(x, 1.0, "First operand for add");
  DEFINE_double(y, 4.0, "Second operand for add");
  DEFINE_bool(nnapi, false, "Whether to use NNAPI");
  brillo::FlagHelper::Init(argc, argv, "ML Service commandline tool");

  // TODO(avg): add ability to run arbitrary models
  std::string processing = FLAGS_nnapi ? "NNAPI" : "CPU";
  std::cout << "Adding " << FLAGS_x << " and " << FLAGS_y << " with "
            << processing << std::endl;
  auto result = ml::simple::Add(FLAGS_x, FLAGS_y, FLAGS_nnapi);
  std::cout << "Status: " << result.status << std::endl;
  std::cout << "Sum: " << result.sum << std::endl;

  return 0;
}
