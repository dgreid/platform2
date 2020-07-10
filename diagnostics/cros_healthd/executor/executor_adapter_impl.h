// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_IMPL_H_

#include "diagnostics/cros_healthd/executor/executor_adapter.h"
#include "mojo/cros_healthd_executor.mojom.h"

namespace diagnostics {

// Production implementation of the ExecutorAdapter interface.
class ExecutorAdapterImpl final : public ExecutorAdapter {
 public:
  ExecutorAdapterImpl();
  ExecutorAdapterImpl(const ExecutorAdapterImpl&) = delete;
  ExecutorAdapterImpl& operator=(const ExecutorAdapterImpl&) = delete;
  ~ExecutorAdapterImpl() override;

  // ExecutorAdapter overrides:
  void Connect(mojo::PlatformChannelEndpoint endpoint) override;

 private:
  // Mojo endpoint to call the executor's methods.
  chromeos::cros_healthd_executor::mojom::ExecutorPtr executor_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_IMPL_H_
