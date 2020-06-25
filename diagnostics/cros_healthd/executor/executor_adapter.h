// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_H_

#include <mojo/public/cpp/platform/platform_channel_endpoint.h>

#include "mojo/cros_healthd_executor.mojom.h"

namespace diagnostics {

// Provides a convenient way to access the root-level executor.
class ExecutorAdapter final {
 public:
  ExecutorAdapter();
  ExecutorAdapter(const ExecutorAdapter&) = delete;
  ExecutorAdapter& operator=(const ExecutorAdapter&) = delete;
  ~ExecutorAdapter();

  // Establishes a Mojo connection with the executor.
  void Connect(mojo::PlatformChannelEndpoint endpoint);

 private:
  // Mojo endpoint to call the executor's methods.
  chromeos::cros_healthd_executor::mojom::ExecutorPtr executor_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_H_
