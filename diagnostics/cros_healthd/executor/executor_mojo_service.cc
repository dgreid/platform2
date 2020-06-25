// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor_mojo_service.h"

#include <utility>

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd_executor::mojom;

}  // namespace

ExecutorMojoService::ExecutorMojoService(mojo_ipc::ExecutorRequest request)
    : binding_{this /* impl */, std::move(request)} {}

}  // namespace diagnostics
