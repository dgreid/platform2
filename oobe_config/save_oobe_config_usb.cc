// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/save_oobe_config_usb.h"

#include <stdlib.h>

#include <string>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <brillo/flag_helper.h>

#include "oobe_config/usb_utils.h"

using base::FileEnumerator;
using base::FilePath;
using std::string;
using std::vector;

namespace oobe_config {

SaveOobeConfigUsb::SaveOobeConfigUsb(const FilePath& device_stateful,
                                     const FilePath& usb_stateful,
                                     const FilePath& device_ids_dir,
                                     const FilePath& usb_device,
                                     const FilePath& private_key_file,
                                     const FilePath& public_key_file)
    : device_stateful_(device_stateful),
      usb_stateful_(usb_stateful),
      device_ids_dir_(device_ids_dir),
      usb_device_(usb_device),
      private_key_file_(private_key_file),
      public_key_file_(public_key_file) {}

bool SaveOobeConfigUsb::SaveInternal() const {
  // Check that input files and directories exist.
  for (const auto& dir : {device_stateful_, usb_stateful_, device_ids_dir_}) {
    if (!base::DirectoryExists(dir)) {
      LOG(ERROR) << "Directory " << dir.value() << " does not exist!";
      return false;
    }
  }
  for (const auto& file : {usb_device_, private_key_file_, public_key_file_}) {
    if (!base::PathExists(file)) {
      LOG(ERROR) << "File " << file.value() << " does not exist!";
      return false;
    }
  }

  // /stateful/unencrypted/oobe_auto_config might not exist on the target
  // device, so create it here.
  auto device_config_dir = device_stateful_.Append(kUnencryptedOobeConfigDir);
  if (!base::CreateDirectory(device_config_dir)) {
    PLOG(ERROR) << "Failed to create directory: " << device_config_dir.value();
    return false;
  }

  auto usb_config_dir = usb_stateful_.Append(kUnencryptedOobeConfigDir);
  auto config_file = usb_config_dir.Append(kConfigFile);
  if (!Sign(private_key_file_, config_file,
            device_config_dir.Append(kConfigFile).AddExtension("sig"))) {
    return false;
  }

  // If the media was provisioned for auto-enrollment, sign the domain name
  // as well.
  auto enrollment_domain_file = usb_config_dir.Append(kDomainFile);
  if (base::PathExists(enrollment_domain_file)) {
    CHECK(Sign(private_key_file_, enrollment_domain_file,
               device_config_dir.Append(kDomainFile).AddExtension("sig")));
  }

  // Sign the source stateful device name.
  FilePath mount_dev;
  if (!FindPersistentMountDevice(&mount_dev)) {
    return false;
  }
  if (!Sign(private_key_file_, mount_dev.value(),
            device_config_dir.Append(kUsbDevicePathSigFile))) {
    return false;
  }

  // Copy the public key into the target stateful for use in validation.
  auto public_key_on_device = device_config_dir.Append(oobe_config::kKeyFile);
  if (!base::CopyFile(public_key_file_, public_key_on_device)) {
    PLOG(ERROR) << "Failed to copy public key " << public_key_file_.value()
                << " to " << public_key_on_device.value();
    return false;
  }

  return true;
}

bool SaveOobeConfigUsb::FindPersistentMountDevice(FilePath* device) const {
  base::FileEnumerator by_id(device_ids_dir_, false,
                             base::FileEnumerator::FILES);
  for (auto link = by_id.Next(); !link.empty(); link = by_id.Next()) {
    // |link| points to something like:
    //   usb-_Some_Memory_<serial>-0:0-part1 -> ../../sdb1
    FilePath target;
    if (!base::NormalizeFilePath(link, &target)) {
      // Do not fail if it doesn't normalize for some reason. We will fail at
      // the end if not found.
      PLOG(WARNING) << "Failed to normalize path " << link.value()
                    << "; Ignoring";
    }
    if (target == usb_device_) {
      LOG(INFO) << link.value() << " points to " << target.value();
      *device = link;
      return true;
    }
  }
  LOG(ERROR) << "Couldn't find persistent device mapping for "
             << usb_device_.value();
  return false;
}

void SaveOobeConfigUsb::Cleanup() const {
  auto device_config_dir = device_stateful_.Append(kUnencryptedOobeConfigDir);
  if (!base::DeletePathRecursively(device_config_dir)) {
    PLOG(ERROR) << "Failed to delete directory: " << device_config_dir.value()
                << "; Just giving up!";
  }
}

bool SaveOobeConfigUsb::Save() const {
  if (!SaveInternal()) {
    LOG(ERROR) << "Failed to save the oobe_config files; cleaning up whatever"
               << " we created.";
    Cleanup();
    return false;
  }
  LOG(INFO) << "Saving OOBE config files was successful! hooray!";
  return true;
}

}  // namespace oobe_config
