// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor_adapter.h"

#include <utility>

#include <base/logging.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "diagnostics/cros_healthd/executor/executor_constants.h"

namespace diagnostics {

namespace {

namespace executor_ipc = ::chromeos::cros_healthd_executor::mojom;

}  // namespace

ExecutorAdapter::ExecutorAdapter() = default;
ExecutorAdapter::~ExecutorAdapter() = default;

void ExecutorAdapter::Connect(mojo::PlatformChannelEndpoint endpoint) {
  DCHECK(endpoint.is_valid());

  // Accept an invitation from the executor.
  mojo::IncomingInvitation invitation =
      mojo::IncomingInvitation::Accept(std::move(endpoint));
  mojo::ScopedMessagePipeHandle pipe =
      invitation.ExtractMessagePipe(kExecutorPipeName);

  executor_.Bind(
      executor_ipc::ExecutorPtrInfo(std::move(pipe), 0u /* version */));
}

}  // namespace diagnostics
