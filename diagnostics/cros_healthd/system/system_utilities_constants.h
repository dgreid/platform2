// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_UTILITIES_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_UTILITIES_CONSTANTS_H_

namespace diagnostics {

// Machines populated by uname().
extern const char kUnameMachineX86_64[];
extern const char kUnameMachineAArch64[];
extern const char kUnameMachineArmv7l[];

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_UTILITIES_CONSTANTS_H_
