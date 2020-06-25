// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_CONSTANTS_H_

namespace diagnostics {

// Used to retrieve the message pipe from the Mojo invitation sent between
// cros_healthd and the executor.
extern const char kExecutorPipeName[];

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_CONSTANTS_H_
