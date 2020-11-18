// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/disk.h"

#include <algorithm>

namespace cros_disks {
namespace {

const char kUSBDriveName[] = "USB Drive";
const char kSDCardName[] = "SD Card";
const char kOpticalDiscName[] = "Optical Disc";
const char kMobileDeviceName[] = "Mobile Device";
const char kDVDName[] = "DVD";
const char kFallbackPresentationName[] = "External Drive";

}  // namespace

std::string Disk::GetPresentationName() const {
  if (!label.empty()) {
    std::string name = label;
    std::replace(name.begin(), name.end(), '/', '_');
    return name;
  }

  switch (media_type) {
    case DEVICE_MEDIA_USB:
      return kUSBDriveName;
    case DEVICE_MEDIA_SD:
      return kSDCardName;
    case DEVICE_MEDIA_OPTICAL_DISC:
      return kOpticalDiscName;
    case DEVICE_MEDIA_MOBILE:
      return kMobileDeviceName;
    case DEVICE_MEDIA_DVD:
      return kDVDName;
    default:
      return kFallbackPresentationName;
  }
}

}  // namespace cros_disks
