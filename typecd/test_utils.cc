// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/test_utils.h"

#include <string>

#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>

namespace typecd {

bool CreateFakeAltMode(const base::FilePath& mode_path,
                       uint16_t svid,
                       uint32_t vdo,
                       uint32_t vdo_index) {
  if (!base::CreateDirectory(mode_path)) {
    PLOG(ERROR) << "Couldn't create directory: " << mode_path;
    return false;
  }

  auto mode_svid = base::StringPrintf("%x", svid);
  if (!base::WriteFile(mode_path.Append("svid"), mode_svid.c_str(),
                       mode_svid.length())) {
    PLOG(ERROR) << "Failed to create SVID in directory " << mode_path;
    return false;
  }

  auto mode_vdo = base::StringPrintf("%#x", vdo);
  if (!base::WriteFile(mode_path.Append("vdo"), mode_vdo.c_str(),
                       mode_vdo.length())) {
    PLOG(ERROR) << "Failed to create VDO in directory " << mode_path;
    return false;
  }

  auto mode_vdo_index = base::StringPrintf("%x", vdo_index);
  if (!base::WriteFile(mode_path.Append("mode"), mode_vdo_index.c_str(),
                       mode_vdo_index.length())) {
    PLOG(ERROR) << "Failed to create VDO mode index in directory " << mode_path;
    return false;
  }

  return true;
}

}  // namespace typecd
