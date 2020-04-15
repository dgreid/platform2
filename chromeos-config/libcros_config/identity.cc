// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Look up identity information for the current device
// Also provide a way to fake identity for testing.

#include "chromeos-config/libcros_config/identity.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <base/system/sys_info.h>
#include "chromeos-config/libcros_config/cros_config_interface.h"
#include "chromeos-config/libcros_config/identity_arm.h"
#include "chromeos-config/libcros_config/identity_x86.h"

namespace brillo {

CrosConfigIdentity::CrosConfigIdentity() {}

CrosConfigIdentity::~CrosConfigIdentity() {}

SystemArchitecture CrosConfigIdentity::CurrentSystemArchitecture() {
  return CurrentSystemArchitecture(
      base::SysInfo::OperatingSystemArchitecture());
}

SystemArchitecture CrosConfigIdentity::CurrentSystemArchitecture(
    const std::string& arch) {
  if (arch == "x86" || arch == "x86_64")
    return SystemArchitecture::kX86;
  if (base::StartsWith(arch, "arm", base::CompareCase::SENSITIVE) ||
      base::StartsWith(arch, "aarch64", base::CompareCase::SENSITIVE))
    return SystemArchitecture::kArm;
  return SystemArchitecture::kUnknown;
}

std::unique_ptr<CrosConfigIdentity> CrosConfigIdentity::FromArchitecture(
    const SystemArchitecture& arch) {
  switch (arch) {
    case SystemArchitecture::kX86:
      return std::make_unique<CrosConfigIdentityX86>();
    case SystemArchitecture::kArm:
      return std::make_unique<CrosConfigIdentityArm>();
    case SystemArchitecture::kUnknown:
      return nullptr;
  }
}

bool CrosConfigIdentity::FakeVpdFileForTesting(const std::string& vpd_id,
                                               base::FilePath* vpd_file_out) {
  *vpd_file_out = base::FilePath("vpd");
  if (base::WriteFile(*vpd_file_out, vpd_id.c_str(), vpd_id.length()) !=
      vpd_id.length()) {
    CROS_CONFIG_LOG(ERROR) << "Failed to write VPD file";
    return false;
  }

  return true;
}

bool CrosConfigIdentity::ReadVpd(const base::FilePath& vpd_file) {
  if (!base::ReadFileToString(vpd_file, &vpd_id_)) {
    CROS_CONFIG_LOG(WARNING) << "No identifier in VPD";
    // This file is only used for whitelabels, so may be missing. Without it
    // we rely on just the name and SKU ID.
  }
  CROS_CONFIG_LOG(INFO) << "Read VPD identity from  " << vpd_file.MaybeAsASCII()
                        << ": " << vpd_id_;
  return true;
}

}  // namespace brillo
