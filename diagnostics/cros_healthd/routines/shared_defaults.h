// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SHARED_DEFAULTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SHARED_DEFAULTS_H_

#include "base/time/time.h"

namespace diagnostics {

// Default runtime for routines which stress the CPU.
extern const base::TimeDelta kDefaultCpuStressRuntime;

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SHARED_DEFAULTS_H_
