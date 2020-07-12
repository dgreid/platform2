// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mounter.h"

#include <sys/mount.h>

#include <utility>

#include <base/logging.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"

namespace cros_disks {

Mounter::Mounter(std::string filesystem_type)
    : filesystem_type_(std::move(filesystem_type)) {}

Mounter::~Mounter() = default;

MounterCompat::MounterCompat(std::unique_ptr<Mounter> mounter,
                             MountOptions mount_options)
    : Mounter(mounter->filesystem_type()),
      mounter_(std::move(mounter)),
      mount_options_(std::move(mount_options)) {}

MounterCompat::MounterCompat(std::string filesystem_type,
                             MountOptions mount_options)
    : Mounter(std::move(filesystem_type)),
      mount_options_(std::move(mount_options)) {}

MounterCompat::~MounterCompat() = default;

std::unique_ptr<MountPoint> MounterCompat::Mount(
    const std::string& source,
    const base::FilePath& target_path,
    std::vector<std::string> options,
    MountErrorType* error) const {
  CHECK(mounter_) << "Method must be overridden if mounter is not set";
  return mounter_->Mount(source, target_path, options, error);
}

bool MounterCompat::CanMount(const std::string& source,
                             const std::vector<std::string>& options,
                             base::FilePath* suggested_dir_name) const {
  *suggested_dir_name = base::FilePath("dir");
  return true;
}

}  // namespace cros_disks
