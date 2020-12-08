// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory/memory_constants.h"

namespace diagnostics {

const char kMemoryRoutineSucceededMessage[] = "Memory routine passed.";
const char kMemoryRoutineRunningMessage[] = "Memory routine running";
const char kMemoryRoutineCancelledMessage[] = "Memory routine cancelled.";
const char kMemoryRoutineAllocatingLockingInvokingFailureMessage[] =
    "Error allocating or locking memory, or invoking the memtester binary.\n";
const char kMemoryRoutineStuckAddressTestFailureMessage[] =
    "Error during the stuck address test.\n";
const char kMemoryRoutineOtherTestFailureMessage[] =
    "Error during a test other than the stuck address test.\n";

}  // namespace diagnostics
