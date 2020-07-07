// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_H_

#include <memory>

#include <brillo/daemons/daemon.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

namespace diagnostics {

// Daemon class for cros_healthd's root-level executor.
class Executor final : public brillo::Daemon {
 public:
  Executor();
  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;
  ~Executor() override;

 private:
  // Necessary to establish Mojo communication with cros_healthd.
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_H_
