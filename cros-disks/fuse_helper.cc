// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/fuse_helper.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/memory/ptr_util.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/system_mounter.h"
#include "cros-disks/uri.h"

namespace cros_disks {

const char FUSEHelper::kFilesUser[] = "chronos";
const char FUSEHelper::kFilesGroup[] = "chronos-access";
const char FUSEHelper::kOptionAllowOther[] = "allow_other";
const char FUSEHelper::kOptionDefaultPermissions[] = "default_permissions";

FUSEHelper::FUSEHelper(const std::string& fuse_type,
                       const Platform* platform,
                       brillo::ProcessReaper* process_reaper,
                       const base::FilePath& mount_program_path,
                       const std::string& mount_user)
    : fuse_type_(fuse_type),
      platform_(platform),
      process_reaper_(process_reaper),
      mount_program_path_(mount_program_path),
      mount_user_(mount_user) {}

FUSEHelper::~FUSEHelper() = default;

bool FUSEHelper::CanMount(const Uri& source) const {
  return source.scheme() == type() && !source.path().empty();
}

std::string FUSEHelper::GetTargetSuffix(const Uri& source) const {
  std::string path = source.path();
  std::replace(path.begin(), path.end(), '/', '$');
  std::replace(path.begin(), path.end(), '.', '_');
  return path;
}

std::unique_ptr<FUSEMounter> FUSEHelper::CreateMounter(
    const base::FilePath& working_dir,
    const Uri& source,
    const base::FilePath& target_path,
    const std::vector<std::string>& options) const {
  MountOptions mount_options;
  mount_options.Initialize(options, false, "", "");
  return base::WrapUnique(
      new FUSEMounter({.filesystem_type = type(),
                       .mount_options = std::move(mount_options),
                       .mount_program = program_path().value(),
                       .mount_user = user(),
                       .platform = platform(),
                       .process_reaper = process_reaper()}));
}

}  // namespace cros_disks
