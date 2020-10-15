// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_URANDOM_URANDOM_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_URANDOM_URANDOM_H_

#include <memory>

#include <base/time/time.h>
#include <base/optional.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"

namespace diagnostics {

// Fleet-wide default value for the urandom routine's parameter.
// TODO(crbug/1131609): get a better default value with some rationale behind
// it.
extern const base::TimeDelta kUrandomDefaultLengthSeconds;

std::unique_ptr<DiagnosticRoutine> CreateUrandomRoutine(
    const base::Optional<base::TimeDelta>& length_seconds);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_URANDOM_URANDOM_H_
