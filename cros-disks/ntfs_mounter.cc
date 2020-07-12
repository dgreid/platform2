// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/ntfs_mounter.h"

#include <utility>

#include "cros-disks/platform.h"

namespace cros_disks {

const char NTFSMounter::kMounterType[] = "ntfs";

NTFSMounter::NTFSMounter(std::string filesystem_type,
                         MountOptions mount_options,
                         const Platform* platform,
                         brillo::ProcessReaper* process_reaper)
    : FUSEMounter({.filesystem_type = std::move(filesystem_type),
                   .mount_options = std::move(mount_options),
                   .mount_program = "/usr/bin/ntfs-3g",
                   .mount_user = "ntfs-3g",
                   .platform = platform,
                   .process_reaper = process_reaper}) {}

}  // namespace cros_disks
