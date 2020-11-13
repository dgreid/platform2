// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CPU_CACHE_CPU_CACHE_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CPU_CACHE_CPU_CACHE_H_

#include <cstdint>
#include <memory>

#include <base/optional.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"

namespace diagnostics {

std::unique_ptr<DiagnosticRoutine> CreateCpuCacheRoutine(
    const base::Optional<base::TimeDelta>& exec_duration);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CPU_CACHE_CPU_CACHE_H_
