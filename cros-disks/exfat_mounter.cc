// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/exfat_mounter.h"

#include <utility>

#include "cros-disks/platform.h"

namespace cros_disks {

const char ExFATMounter::kMounterType[] = "exfat";

ExFATMounter::ExFATMounter(std::string filesystem_type,
                           MountOptions mount_options,
                           const Platform* platform,
                           brillo::ProcessReaper* process_reaper)
    : FUSEMounter({.filesystem_type = std::move(filesystem_type),
                   .mount_options = std::move(mount_options),
                   .mount_program = "/usr/sbin/mount.exfat-fuse",
                   .mount_user = "fuse-exfat",
                   .platform = platform,
                   .process_reaper = process_reaper}) {}

}  // namespace cros_disks
