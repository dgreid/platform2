// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_URANDOM_URANDOM_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_URANDOM_URANDOM_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/macros.h>
#include <base/process/process.h>

#include "diagnostics/cros_healthd/routines/diag_process_adapter.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"

namespace diagnostics {

std::unique_ptr<DiagnosticRoutine> CreateUrandomRoutine(
    uint32_t length_seconds);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_URANDOM_URANDOM_H_
