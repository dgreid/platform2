// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor.h"

#include <memory>

#include <base/threading/thread_task_runner_handle.h>

namespace diagnostics {

Executor::Executor() {
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get() /* io_thread_task_runner */,
      mojo::core::ScopedIPCSupport::ShutdownPolicy::
          CLEAN /* blocking shutdown */);
}

Executor::~Executor() = default;

}  // namespace diagnostics
