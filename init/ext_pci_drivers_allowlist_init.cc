// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

namespace {

// The path to the sysfs allowlist file.
constexpr char kAllowlistPath[] = "/sys/bus/pci/drivers_allowlist";

// Actual driver allowlist.
constexpr const char* kAllowlist[] = {
    // TODO(b/163121310): This list is only for development and may
    // be cleared or pruned before the launch/FSI.
    "pcieport",  // PCI Core services - AER, Hotplug etc.
    "xhci_hcd",  // XHCI host controller driver.
    "nvme",      // PCI Express NVME host controller driver.
};

}  // namespace.

int main(int argc, char* argv[]) {
  const base::FilePath allowlist_file(kAllowlistPath);

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  if (argc != 1) {
    LOG(ERROR) << "Invalid parameters";
    return 1;
  }

  if (base::PathIsWritable(allowlist_file)) {
    LOG(INFO) << "Kernel supports " << kAllowlistPath
              << ", will install allowlist";
  } else {
    LOG(INFO) << "Kernel doesn't support " << kAllowlistPath
              << ", skip installing allowlist";
    return 0;
  }

  int ret = 0;
  for (const char* drvr_name : kAllowlist) {
    if (base::WriteFile(allowlist_file, drvr_name, sizeof(drvr_name)) ==
        sizeof(drvr_name)) {
      LOG(INFO) << "Allowed " << drvr_name;
    } else {
      PLOG(ERROR) << "Couldn't allow " << drvr_name;
      ret = 1;
    }
  }
  return ret;
}
