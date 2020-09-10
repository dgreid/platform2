// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/system_properties.h"

#include <vboot/crossystem.h>

namespace dlcservice {

bool SystemProperties::IsOfficialBuild() {
  return VbGetSystemPropertyInt("debug_build") == 0;
}

}  // namespace dlcservice
