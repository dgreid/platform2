// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_MOJO_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_MOJO_SERVICE_H_

#include <mojo/public/cpp/bindings/binding.h>

#include "mojo/cros_healthd_executor.mojom.h"

namespace diagnostics {

// Production implementation of the
// chromeos::cros_healthd_executor::mojom::Executor Mojo interface.
class ExecutorMojoService final
    : public chromeos::cros_healthd_executor::mojom::Executor {
 public:
  explicit ExecutorMojoService(
      chromeos::cros_healthd_executor::mojom::ExecutorRequest request);
  ExecutorMojoService(const ExecutorMojoService&) = delete;
  ExecutorMojoService& operator=(const ExecutorMojoService&) = delete;

 private:
  // Provides a Mojo endpoint that cros_healthd can call to access the
  // executor's Mojo methods.
  mojo::Binding<chromeos::cros_healthd_executor::mojom::Executor> binding_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_MOJO_SERVICE_H_
