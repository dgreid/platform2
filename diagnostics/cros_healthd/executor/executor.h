// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_H_

#include <memory>

#include <base/memory/scoped_refptr.h>
#include <base/single_thread_task_runner.h>
#include <brillo/daemons/daemon.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/platform/platform_channel_endpoint.h>

#include "diagnostics/cros_healthd/executor/executor_mojo_service.h"

namespace diagnostics {

// Daemon class for cros_healthd's root-level executor.
class Executor final : public brillo::Daemon {
 public:
  explicit Executor(mojo::PlatformChannelEndpoint endpoint);
  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;
  ~Executor() override;

 private:
  // Used as the task runner for all Mojo IPCs.
  const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner_;
  // Necessary to establish Mojo communication with cros_healthd.
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  // Implements the executor's Mojo methods.
  std::unique_ptr<ExecutorMojoService> mojo_service_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_H_
