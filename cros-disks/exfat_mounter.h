// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_EXFAT_MOUNTER_H_
#define CROS_DISKS_EXFAT_MOUNTER_H_

#include <string>

#include "cros-disks/fuse_mounter.h"

namespace cros_disks {

// A class for mounting a device file using exfat-fuse.
class ExFATMounter : public FUSEMounter {
 public:
  // A unique type identifier of this derived mounter class.
  static const char kMounterType[];

  ExFATMounter(std::string filesystem_type,
               MountOptions mount_options,
               const Platform* platform,
               brillo::ProcessReaper* process_reaper);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_EXFAT_MOUNTER_H_
