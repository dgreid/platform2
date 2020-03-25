// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/grpc_async_adapter/time_util.h"

#include <grpc/support/time.h>

namespace diagnostics {

gpr_timespec GprTimespecWithDeltaFromNow(base::TimeDelta delta) {
  return gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_millis(delta.InMilliseconds(), GPR_TIMESPAN));
}

}  // namespace diagnostics
