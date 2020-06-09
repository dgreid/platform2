// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/alt_mode.h"

#include <string>

#include <base/strings/string_util.h>

namespace typecd {

// static
std::unique_ptr<AltMode> AltMode::CreateAltMode(const base::FilePath& syspath) {
  auto alt_mode = std::make_unique<AltMode>(syspath);

  if (!alt_mode->UpdateValuesFromSysfs()) {
    return nullptr;
  }

  return alt_mode;
}

bool AltMode::UpdateValuesFromSysfs() {
  // Create the various sysfs file paths.
  auto svid_path = syspath_.Append("svid");
  auto vdo_path = syspath_.Append("vdo");
  auto mode_index_path = syspath_.Append("mode");

  // Only proceed if we can read all the attributes.
  std::string svid_str;
  std::string vdo_str;
  std::string mode_index_str;
  if (!base::ReadFileToString(svid_path, &svid_str)) {
    LOG(ERROR) << "Couldn't read SVID from path " << svid_path;
    return false;
  }
  base::TrimWhitespaceASCII(svid_str, base::TRIM_TRAILING, &svid_str);

  if (!base::ReadFileToString(vdo_path, &vdo_str)) {
    LOG(ERROR) << "Couldn't read VDO from path " << vdo_path;
    return false;
  }
  base::TrimWhitespaceASCII(vdo_str, base::TRIM_TRAILING, &vdo_str);

  if (!base::ReadFileToString(mode_index_path, &mode_index_str)) {
    LOG(ERROR) << "Couldn't read mode index from path " << mode_index_path;
    return false;
  }
  base::TrimWhitespaceASCII(mode_index_str, base::TRIM_TRAILING,
                            &mode_index_str);

  uint32_t svid;
  if (!base::HexStringToUInt(svid_str.c_str(), &svid)) {
    LOG(ERROR) << "Error parsing svid " << svid_str;
    return false;
  }

  uint32_t vdo;
  if (!base::HexStringToUInt(vdo_str, &vdo)) {
    LOG(ERROR) << "Error parsing vdo " << vdo_str;
    return false;
  }

  uint32_t mode_index;
  if (!base::HexStringToUInt(mode_index_str, &mode_index)) {
    LOG(ERROR) << "Error parsing mode " << mode_index_str;
    return false;
  }

  svid_ = svid;
  vdo_ = vdo;
  mode_index_ = mode_index;

  return true;
}

}  // namespace typecd
