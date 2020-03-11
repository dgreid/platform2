// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/types.h"

namespace dlcservice {

DlcInfo::DlcInfo(std::string root_in) {
  root = root_in;
}

}  // namespace dlcservice
