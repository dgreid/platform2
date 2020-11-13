// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/shared_defaults.h"

namespace diagnostics {

const base::TimeDelta kDefaultCpuStressRuntime =
    base::TimeDelta::FromMinutes(1);

}  // namespace diagnostics
