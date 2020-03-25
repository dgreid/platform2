// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_GRPC_ASYNC_ADAPTER_TIME_UTIL_H_
#define DIAGNOSTICS_GRPC_ASYNC_ADAPTER_TIME_UTIL_H_

#include <base/time/time.h>
#include <grpcpp/support/time.h>

namespace diagnostics {

gpr_timespec GprTimespecWithDeltaFromNow(base::TimeDelta delta);

}

#endif  // DIAGNOSTICS_GRPC_ASYNC_ADAPTER_TIME_UTIL_H_
