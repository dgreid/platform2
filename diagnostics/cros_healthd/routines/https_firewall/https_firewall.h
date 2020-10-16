// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_HTTPS_FIREWALL_HTTPS_FIREWALL_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_HTTPS_FIREWALL_HTTPS_FIREWALL_H_

#include <memory>

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"

namespace diagnostics {

// Status messages reported by the HTTPS firewall routine.
extern const char kHttpsFirewallRoutineNoProblemMessage[];
extern const char
    kHttpsFirewallRoutineHighDnsResolutionFailureRateProblemMessage[];
extern const char kHttpsFirewallRoutineFirewallDetectedProblemMessage[];
extern const char kHttpsFirewallRoutinePotentialFirewallProblemMessage[];
extern const char kHttpsFirewallRoutineNotRunMessage[];

// Creates an instance of the HTTPS firewall routine.
std::unique_ptr<DiagnosticRoutine> CreateHttpsFirewallRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_HTTPS_FIREWALL_HTTPS_FIREWALL_H_
