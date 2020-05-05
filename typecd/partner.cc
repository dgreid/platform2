// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/partner.h"

#include <base/logging.h>

namespace typecd {

void Partner::AddAltMode(int index, uint16_t svid, uint32_t vdo) {
  alt_modes_.emplace(index, std::make_unique<AltMode>(svid, vdo));
}

AltMode* Partner::GetAltMode(int index) {
  auto it = alt_modes_.find(index);
  if (it != alt_modes_.end())
    return it->second.get();

  return nullptr;
}

void Partner::UpdateAltModesFromSysfs() {
  NOTIMPLEMENTED();
}

}  // namespace typecd
