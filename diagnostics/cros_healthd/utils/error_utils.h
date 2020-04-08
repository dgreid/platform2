// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_ERROR_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_ERROR_UTILS_H_

#include <string>

#include <base/logging.h>

#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// This helper function takes an error type and string error message and returns
// a ProbeError. In addition, the error message is logged to LOG(ERROR).
inline chromeos::cros_healthd::mojom::ProbeErrorPtr CreateAndLogProbeError(
    chromeos::cros_healthd::mojom::ErrorType type, const std::string& msg) {
  auto error = chromeos::cros_healthd::mojom::ProbeError::New(type, msg);
  LOG(ERROR) << msg;
  return error;
}

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_ERROR_UTILS_H_
