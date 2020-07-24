// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor.h"

#include <memory>
#include <utility>

#include <base/threading/thread_task_runner_handle.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "diagnostics/cros_healthd/executor/executor_constants.h"
#include "mojo/cros_healthd_executor.mojom.h"

namespace diagnostics {

namespace {

namespace executor_ipc = ::chromeos::cros_healthd_executor::mojom;

}  // namespace

Executor::Executor(mojo::PlatformChannelEndpoint endpoint) {
  DCHECK(endpoint.is_valid());

  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get() /* io_thread_task_runner */,
      mojo::core::ScopedIPCSupport::ShutdownPolicy::
          CLEAN /* blocking shutdown */);

  mojo::IncomingInvitation invitation =
      mojo::IncomingInvitation::Accept(std::move(endpoint));
  mojo::ScopedMessagePipeHandle pipe =
      invitation.ExtractMessagePipe(kExecutorPipeName);

  mojo_service_ = std::make_unique<ExecutorMojoService>(
      executor_ipc::ExecutorRequest(std::move(pipe)));
}

Executor::~Executor() = default;

}  // namespace diagnostics
