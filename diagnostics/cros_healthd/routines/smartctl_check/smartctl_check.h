// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SMARTCTL_CHECK_SMARTCTL_CHECK_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SMARTCTL_CHECK_SMARTCTL_CHECK_H_

#include <memory>

#include "diagnostics/cros_healthd/routines/subproc_routine.h"

namespace diagnostics {

std::unique_ptr<DiagnosticRoutine> CreateSmartctlCheckRoutine();

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SMARTCTL_CHECK_SMARTCTL_CHECK_H_
