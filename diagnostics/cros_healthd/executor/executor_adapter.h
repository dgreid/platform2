// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_H_

#include <mojo/public/cpp/platform/platform_channel_endpoint.h>

#include "mojo/cros_healthd_executor.mojom.h"

namespace diagnostics {

// Provides a convenient way to access the root-level executor.
class ExecutorAdapter {
 public:
  using Executor = chromeos::cros_healthd_executor::mojom::Executor;

  virtual ~ExecutorAdapter() = default;

  // Establishes a Mojo connection with the executor.
  virtual void Connect(mojo::PlatformChannelEndpoint endpoint) = 0;

  // Returns the speed of each of the device's fans.
  virtual void GetFanSpeed(Executor::GetFanSpeedCallback callback) = 0;

  // Instructs the executor to run the memtester executable.
  virtual void RunMemtester(Executor::RunMemtesterCallback callback) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_H_
