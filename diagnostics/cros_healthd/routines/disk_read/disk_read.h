// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_DISK_READ_DISK_READ_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_DISK_READ_DISK_READ_H_

#include <cstdint>
#include <memory>

#include <base/time/time.h>
#include <brillo/process/process.h>

#include "diagnostics/cros_healthd/routines/subproc_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

std::unique_ptr<DiagnosticRoutine> CreateDiskReadRoutine(
    chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
    base::TimeDelta exec_duration,
    uint32_t file_size_mb);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_DISK_READ_DISK_READ_H_
