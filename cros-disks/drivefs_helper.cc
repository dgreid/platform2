// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/drivefs_helper.h"

#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/sandboxed_process.h"
#include "cros-disks/system_mounter.h"
#include "cros-disks/uri.h"

namespace cros_disks {
namespace {

const char kDataDirOptionPrefix[] = "datadir";
const char kIdentityOptionPrefix[] = "identity";
const char kMyFilesOptionPrefix[] = "myfiles";
const char kPathPrefixOptionPrefix[] = "prefix";

const char kHelperTool[] = "/opt/google/drive-file-stream/drivefs";
const char kType[] = "drivefs";
const char kDbusSocketPath[] = "/run/dbus";
const char kHomeBaseDir[] = "/home";

bool FindPathOption(const std::vector<std::string>& options,
                    const std::string& name,
                    base::FilePath* path) {
  std::string value;
  if (!GetParamValue(options, name, &value) || value.empty()) {
    return false;
  }
  *path = base::FilePath(value);
  return true;
}

bool ValidateDirectory(const Platform* platform, base::FilePath* dir) {
  if (dir->empty() || !dir->IsAbsolute() || dir->ReferencesParent()) {
    LOG(ERROR) << "Unsafe path " << quote(*dir);
    return false;
  }
  std::string path_string;
  if (!platform->GetRealPath(dir->value(), &path_string)) {
    LOG(ERROR) << "Unable to find real path of " << quote(*dir);
    return false;
  }

  *dir = base::FilePath(path_string);
  CHECK(dir->IsAbsolute() && !dir->ReferencesParent());

  if (!platform->DirectoryExists(dir->value())) {
    LOG(ERROR) << "Dir does not exist " << quote(*dir);
    return false;
  }

  uid_t current_uid;
  if (!platform->GetOwnership(dir->value(), &current_uid, nullptr)) {
    LOG(ERROR) << "Cannot access datadir " << quote(*dir);
    return false;
  }

  if (current_uid != kChronosUID) {
    LOG(ERROR) << "Wrong owner of datadir: " << current_uid;
    return false;
  }

  return true;
}

}  // namespace

DrivefsHelper::DrivefsHelper(const Platform* platform,
                             brillo::ProcessReaper* process_reaper)
    : FUSEMounterHelper(platform,
                        process_reaper,
                        kType,
                        /* nosymfollow= */ false,
                        &sandbox_factory_),
      sandbox_factory_(platform,
                       SandboxedExecutable{base::FilePath(kHelperTool)},
                       OwnerUser{kChronosUID, kChronosGID},
                       /* has_network_access= */ true) {}

DrivefsHelper::~DrivefsHelper() = default;

bool DrivefsHelper::CanMount(const std::string& source,
                             const std::vector<std::string>& params,
                             base::FilePath* suggested_name) const {
  const Uri uri = Uri::Parse(source);
  if (!uri.valid() || uri.scheme() != kType)
    return false;

  if (uri.path().empty())
    *suggested_name = base::FilePath(kType);
  else
    *suggested_name = base::FilePath(uri.path());
  return true;
}

MountErrorType DrivefsHelper::ConfigureSandbox(
    const std::string& source,
    const base::FilePath& target_path,
    std::vector<std::string> params,
    SandboxedProcess* sandbox) const {
  const Uri uri = Uri::Parse(source);
  if (!uri.valid() || uri.scheme() != kType) {
    LOG(ERROR) << "Inavlid source format " << quote(source);
    return MOUNT_ERROR_INVALID_DEVICE_PATH;
  }
  if (uri.path().empty()) {
    LOG(ERROR) << "Inavlid source " << quote(source);
    return MOUNT_ERROR_INVALID_DEVICE_PATH;
  }

  base::FilePath data_dir;
  if (!FindPathOption(params, kDataDirOptionPrefix, &data_dir)) {
    LOG(ERROR) << "No data directory provided";
    return MOUNT_ERROR_INVALID_MOUNT_OPTIONS;
  }
  if (!ValidateDirectory(platform(), &data_dir)) {
    return MOUNT_ERROR_INSUFFICIENT_PERMISSIONS;
  }

  const base::FilePath homedir(kHomeBaseDir);
  if (!homedir.IsParent(data_dir)) {
    LOG(ERROR) << "Unexpected location of " << quote(data_dir);
    return MOUNT_ERROR_INSUFFICIENT_PERMISSIONS;
  }

  base::FilePath my_files;
  if (FindPathOption(params, kMyFilesOptionPrefix, &my_files)) {
    if (!ValidateDirectory(platform(), &my_files)) {
      LOG(ERROR) << "User files inaccessible";
      return MOUNT_ERROR_INSUFFICIENT_PERMISSIONS;
    }
    if (!homedir.IsParent(my_files)) {
      LOG(ERROR) << "Unexpected location of " << quote(my_files);
      return MOUNT_ERROR_INSUFFICIENT_PERMISSIONS;
    }
  }

  // Bind datadir, user files and DBus communication socket into the sandbox.
  if (!sandbox->Mount("tmpfs", "/home", "tmpfs", "mode=0755,size=1M")) {
    LOG(ERROR) << "Cannot mount /home";
    return MOUNT_ERROR_INTERNAL;
  }
  if (!sandbox->BindMount(data_dir.value(), data_dir.value(), true, false)) {
    LOG(ERROR) << "Cannot bind " << quote(data_dir);
    return MOUNT_ERROR_INTERNAL;
  }
  if (!sandbox->BindMount(kDbusSocketPath, kDbusSocketPath, true, false)) {
    LOG(ERROR) << "Cannot bind " << quote(kDbusSocketPath);
    return MOUNT_ERROR_INTERNAL;
  }
  if (!my_files.empty()) {
    if (!sandbox->BindMount(my_files.value(), my_files.value(), true, true)) {
      LOG(ERROR) << "Cannot bind " << quote(my_files);
      return MOUNT_ERROR_INTERNAL;
    }
  }

  std::vector<std::string> args;
  SetParamValue(&args, "uid", base::NumberToString(kChronosUID));
  SetParamValue(&args, "gid", base::NumberToString(kChronosAccessGID));
  SetParamValue(&args, kDataDirOptionPrefix, data_dir.value());
  SetParamValue(&args, kIdentityOptionPrefix, uri.path());
  SetParamValue(&args, kPathPrefixOptionPrefix, target_path.value());
  if (!my_files.empty()) {
    SetParamValue(&args, kMyFilesOptionPrefix, my_files.value());
  }
  sandbox->AddArgument("-o");
  sandbox->AddArgument(base::JoinString(args, ","));

  return MOUNT_ERROR_NONE;
}

}  // namespace cros_disks
