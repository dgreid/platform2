// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_DIAG_ROUTINE_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_DIAG_ROUTINE_H_

#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// An interface for creating a diagnostic routine, which can be run and
// controlled by the platform.
class DiagnosticRoutine {
 public:
  // Note that the instance of this object may be destroyed before the routine
  // is finished - the implementation must ensure that the destructor
  // terminates all background processes in that case.
  virtual ~DiagnosticRoutine() = default;

  // Starts the diagnostic routine. This function should only be called a
  // single time per instance of DiagnosticRoutine.
  virtual void Start() = 0;
  // This function should only be called to resume interactive routines that are
  // currently in the ROUTINE_STATUS_WAITING state.
  virtual void Resume() = 0;
  // Cancels an active diagnostics routine. Information (status, output, user
  // message) of a cancelled routine can still be accessed, but the routine
  // cannot be restarted.
  virtual void Cancel() = 0;
  // Populates |response| with the current status of the diagnostic routine.
  virtual void PopulateStatusUpdate(
      chromeos::cros_healthd::mojom::RoutineUpdate* response,
      bool include_output) = 0;
  virtual chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum
  GetStatus() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_DIAG_ROUTINE_H_
