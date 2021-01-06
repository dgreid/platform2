// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pciguard/pciguard_utils.h"

#include <base/command_line.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <brillo/syslog_logging.h>

#include <string>
#include <sysexits.h>

namespace pciguard {

namespace {

// Sysfs driver allowlist file (contains drivers that are allowlisted for
// external PCI devices)).
constexpr char kAllowlistPath[] = "/sys/bus/pci/drivers_allowlist";

// Sysfs PCI lockdown file. When set to 1, this prevents any driver to bind to
// external PCI devices (including allowlisted drivers).
constexpr char kExtPCILockdownPath[] =
    "/sys/bus/pci/drivers_allowlist_lockdown";

// Sysfs PCI rescan file. It rescans the PCI bus to discover any new devices.
constexpr char kPCIRescanPath[] = "/sys/bus/pci/rescan";

// Actual driver allowlist.
const char* kAllowlist[] = {
    // TODO(b/163121310): Finalize allowlist
    "pcieport",  // PCI Core services - AER, Hotplug etc.
    "xhci_hcd",  // XHCI host controller driver.
    "nvme",      // PCI Express NVME host controller driver.
    "ahci",      // AHCI driver.
    "igb",       // Intel Giga Bit Ethernet driver on TBT devices.
};

int SetAuthorizedAttribute(base::FilePath devpath, bool enable) {
  if (!PathExists(devpath)) {
    PLOG(ERROR) << "Path doesn't exist : " << devpath;
    return EXIT_FAILURE;
  }

  base::FilePath symlink;
  // Check it is a thunderbolt path
  if (!base::ReadSymbolicLink(devpath.Append("subsystem"), &symlink) ||
      !base::EndsWith(symlink.value(), "/bus/thunderbolt",
                      base::CompareCase::SENSITIVE)) {
    LOG(ERROR) << "Not a thunderbolt devpath: " << devpath;
    return EXIT_FAILURE;
  }

  base::FilePath authorized_path = devpath.Append("authorized");
  std::string authorized;

  // Proceed only if authorized file exists
  if (!base::ReadFileToString(authorized_path, &authorized))
    return EXIT_SUCCESS;

  // Nevermind if no need to change the state.
  if (!authorized.empty() &&
      ((enable && authorized[0] != '0') || (!enable && authorized[0] == '0')))
    return EXIT_SUCCESS;

  auto val = "0";
  if (enable) {
    LOG(INFO) << "Authorizing:" << devpath;
    val = "1";
  } else {
    LOG(INFO) << "Deauthorizing:" << devpath;
  }

  if (base::WriteFile(authorized_path, val, 1) != 1) {
    PLOG(ERROR) << "Couldn't write " << val << " to " << authorized_path;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int DeauthorizeThunderboltDev(base::FilePath devpath) {
  return SetAuthorizedAttribute(devpath, false);
}

}  // namespace

int OnInit(void) {
  if (!base::PathIsWritable(base::FilePath(kAllowlistPath)) ||
      !base::PathIsWritable(base::FilePath(kExtPCILockdownPath))) {
    PLOG(ERROR) << "Kernel is missing needed support for external PCI security";
    return EX_OSFILE;
  }

  if (base::WriteFile(base::FilePath(kExtPCILockdownPath), "1", 1) != 1) {
    PLOG(ERROR) << "Couldn't write 1 to " << kExtPCILockdownPath;
    return EX_IOERR;
  }

  const base::FilePath allowlist_file(kAllowlistPath);
  for (auto drvr_name : kAllowlist) {
    if (base::WriteFile(allowlist_file, drvr_name, sizeof(drvr_name)) ==
        sizeof(drvr_name))
      LOG(INFO) << "Allowed " << drvr_name;
    else
      PLOG(ERROR) << "Couldn't allow " << drvr_name;
  }
  return EX_OK;
}

int AuthorizeThunderboltDev(base::FilePath devpath) {
  return SetAuthorizedAttribute(devpath, true);
}

int AuthorizeAllDevices(void) {
  LOG(INFO) << "Authorizing all external PCI devices";

  // Allow drivers to bind to PCI devices. This also binds any PCI devices
  // that may have been hotplugged "into" external peripherals, while the
  // screen was locked.
  if (base::WriteFile(base::FilePath(kExtPCILockdownPath), "0", 1) != 1) {
    PLOG(ERROR) << "Couldn't write 0 to " << kExtPCILockdownPath;
    return EXIT_FAILURE;
  }

  int ret = EXIT_SUCCESS;

  // Add any PCI devices that we removed when the user had logged off.
  if (base::WriteFile(base::FilePath(kPCIRescanPath), "1", 1) != 1) {
    PLOG(ERROR) << "Couldn't write 1 to " << kPCIRescanPath;
    ret = EXIT_FAILURE;
  }

  base::FileEnumerator iter(base::FilePath("/sys/bus/thunderbolt/devices"),
                            false, base::FileEnumerator::DIRECTORIES);
  for (auto devpath = iter.Next(); !devpath.empty(); devpath = iter.Next()) {
    // Authorize the device. This takes care of any thunderbolt peripherals
    // that were added while the screen was locked.
    if (AuthorizeThunderboltDev(devpath))
      ret = EXIT_FAILURE;
  }

  return ret;
}

int DenyNewDevices(void) {
  LOG(INFO) << "Will deny all new external PCI devices";

  // Deny drivers to bind to any *new* external PCI devices.
  if (base::WriteFile(base::FilePath(kExtPCILockdownPath), "1", 1) != 1) {
    PLOG(ERROR) << "Couldn't write 1 to " << kExtPCILockdownPath;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int DeauthorizeAllDevices(void) {
  int ret = EXIT_SUCCESS;
  if (DenyNewDevices())
    return EXIT_FAILURE;

  LOG(INFO) << "Deauthorizing all external PCI devices";

  // Remove all untrusted (external) PCI devices.
  base::FileEnumerator iter(base::FilePath("/sys/bus/pci/devices"), false,
                            base::FileEnumerator::DIRECTORIES);
  for (auto devpath = iter.Next(); !devpath.empty(); devpath = iter.Next()) {
    std::string untrusted;

    // It is possible this device may already been have removed (as an effect
    // of its parent being removed).
    if (!PathExists(devpath))
      continue;

    // Proceed only if there is an "untrusted" file.
    if (!base::ReadFileToString(devpath.Append("untrusted"), &untrusted) ||
        untrusted.empty()) {
      PLOG(ERROR) << "Couldn't read " << devpath << "/untrusted";
      ret = EXIT_FAILURE;
      continue;
    }

    // Nevermind the trusted devices.
    if (untrusted[0] == '0')
      continue;

    // Remove untrusted device.
    if (base::WriteFile(devpath.Append("remove"), "1", 1) != 1) {
      PLOG(ERROR) << "Couldn't remove untrusted device " << devpath;
      ret = EXIT_FAILURE;
    }
  }

  // Deauthorize all thunderbolt devices.
  base::FileEnumerator tbt_iter(base::FilePath("/sys/bus/thunderbolt/devices"),
                                false, base::FileEnumerator::DIRECTORIES);
  for (auto devpath = tbt_iter.Next(); !devpath.empty();
       devpath = tbt_iter.Next()) {
    if (DeauthorizeThunderboltDev(devpath))
      ret = EXIT_FAILURE;
  }
  return ret;
}

}  // namespace pciguard
