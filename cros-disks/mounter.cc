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

Mounter::Mounter() = default;

Mounter::~Mounter() = default;

MounterCompat::MounterCompat(MountOptions mount_options,
                             std::unique_ptr<Mounter> mounter)
    : mounter_(std::move(mounter)), mount_options_(std::move(mount_options)) {}

MounterCompat::~MounterCompat() = default;

std::unique_ptr<MountPoint> MounterCompat::Mount(
    const std::string& source,
    const base::FilePath& target_path,
    std::vector<std::string> params,
    MountErrorType* error) const {
  CHECK(mounter_) << "Method must be overridden if mounter is not set";
  return mounter_->Mount(source, target_path, std::move(params), error);
}

bool MounterCompat::CanMount(const std::string& source,
                             const std::vector<std::string>& params,
                             base::FilePath* suggested_dir_name) const {
  *suggested_dir_name = base::FilePath("dir");
  return true;
}

}  // namespace cros_disks
