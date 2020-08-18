// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor_adapter_impl.h"

#include <utility>

#include <base/logging.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "diagnostics/cros_healthd/executor/executor_constants.h"

namespace diagnostics {

namespace {

namespace executor_ipc = ::chromeos::cros_healthd_executor::mojom;

}  // namespace

ExecutorAdapterImpl::ExecutorAdapterImpl() = default;
ExecutorAdapterImpl::~ExecutorAdapterImpl() = default;

void ExecutorAdapterImpl::Connect(mojo::PlatformChannelEndpoint endpoint) {
  DCHECK(endpoint.is_valid());

  mojo::OutgoingInvitation invitation;
  // Attach a message pipe to be extracted by the receiver. The other end of the
  // pipe is returned for us to use locally.
  mojo::ScopedMessagePipeHandle pipe =
      invitation.AttachMessagePipe(kExecutorPipeName);

  executor_.Bind(
      executor_ipc::ExecutorPtrInfo(std::move(pipe), 0u /* version */));

  mojo::OutgoingInvitation::Send(std::move(invitation),
                                 base::kNullProcessHandle, std::move(endpoint));
}

void ExecutorAdapterImpl::GetFanSpeed(Executor::GetFanSpeedCallback callback) {
  DCHECK(executor_.is_bound());

  executor_->GetFanSpeed(std::move(callback));
}

void ExecutorAdapterImpl::RunMemtester(
    Executor::RunMemtesterCallback callback) {
  DCHECK(executor_.is_bound());

  executor_->RunMemtester(std::move(callback));
}

}  // namespace diagnostics
