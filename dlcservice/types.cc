// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/types.h"

namespace dlcservice {

DlcInfo::DlcInfo(DlcState::State state_in, DlcRoot root_in) {
  root = root_in;
  state.set_state(state_in);
}

}  // namespace dlcservice
