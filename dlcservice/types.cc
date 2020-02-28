// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/types.h"

namespace dlcservice {

DlcInfo::DlcInfo(DlcState::State state_in,
                 std::string root_in,
                 std::string err_code_in) {
  state.set_state(state_in);
  state.set_error_code(err_code_in);
  root = root_in;
}

}  // namespace dlcservice
