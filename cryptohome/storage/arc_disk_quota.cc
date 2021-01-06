// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/arc_disk_quota.h"

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/cryptohome.h>

#include "cryptohome/projectid_config.h"

namespace cryptohome {

ArcDiskQuota::ArcDiskQuota(HomeDirs* homedirs,
                           Platform* platform,
                           const base::FilePath& home)
    : homedirs_(homedirs), platform_(platform), home_(home) {}

ArcDiskQuota::~ArcDiskQuota() = default;

void ArcDiskQuota::Initialize() {
  device_ = GetDevice();
}

bool ArcDiskQuota::IsQuotaSupported() const {
  if (device_.empty()) {
    LOG(ERROR) << "No quota mount is found.";
    return false;
  }

  // TODO(risan): Support quota for more than 1 Android user,
  // after that, the following check could be removed.
  int cnt = homedirs_->GetUnmountedAndroidDataCount();
  if (cnt != 0) {
    LOG(ERROR) << "Quota is supported only if there are no unmounted Android "
                  "users. Found extra unmounted "
               << cnt << " Android users.";
    return false;
  }

  return true;
}

int64_t ArcDiskQuota::GetCurrentSpaceForUid(uid_t android_uid) const {
  if (android_uid < kAndroidUidStart || android_uid > kAndroidUidEnd) {
    LOG(ERROR) << "Android uid " << android_uid
               << " is outside the allowed query range";
    return -1;
  }
  if (device_.empty()) {
    LOG(ERROR) << "No quota mount is found";
    return -1;
  }
  uid_t real_uid = android_uid + kArcContainerShiftUid;
  int64_t current_space =
      platform_->GetQuotaCurrentSpaceForUid(device_, real_uid);
  if (current_space == -1) {
    PLOG(ERROR) << "Failed to get disk stats for uid: " << real_uid;
    return -1;
  }
  return current_space;
}

int64_t ArcDiskQuota::GetCurrentSpaceForGid(gid_t android_gid) const {
  if (android_gid < kAndroidGidStart || android_gid > kAndroidGidEnd) {
    LOG(ERROR) << "Android gid " << android_gid
               << " is outside the allowed query range";
    return -1;
  }
  if (device_.empty()) {
    LOG(ERROR) << "No quota mount is found";
    return -1;
  }
  gid_t real_gid = android_gid + kArcContainerShiftGid;
  int64_t current_space =
      platform_->GetQuotaCurrentSpaceForGid(device_, real_gid);
  if (current_space == -1) {
    PLOG(ERROR) << "Failed to get disk stats for gid: " << real_gid;
    return -1;
  }
  return current_space;
}

int64_t ArcDiskQuota::GetCurrentSpaceForProjectId(int project_id) const {
  if ((project_id < kProjectIdForAndroidFilesStart ||
       project_id > kProjectIdForAndroidFilesEnd) &&
      (project_id < kProjectIdForAndroidAppsStart ||
       project_id > kProjectIdForAndroidAppsEnd)) {
    LOG(ERROR) << "Project id " << project_id
               << " is outside the allowed query range";
    return -1;
  }
  if (device_.empty()) {
    LOG(ERROR) << "No quota mount is found";
    return -1;
  }
  int64_t current_space =
      platform_->GetQuotaCurrentSpaceForProjectId(device_, project_id);
  if (current_space == -1) {
    PLOG(ERROR) << "Failed to get disk stats for project id: " << project_id;
    return -1;
  }
  return current_space;
}

bool ArcDiskQuota::SetProjectId(int project_id,
                                SetProjectIdAllowedPathType parent_path,
                                const base::FilePath& child_path,
                                const std::string& obfuscated_username) const {
  if ((project_id < kProjectIdForAndroidFilesStart ||
       project_id > kProjectIdForAndroidFilesEnd) &&
      (project_id < kProjectIdForAndroidAppsStart ||
       project_id > kProjectIdForAndroidAppsEnd)) {
    LOG(ERROR) << "Project id " << project_id
               << " is outside the allowed query range";
    return false;
  }

  if (child_path.ReferencesParent()) {
    LOG(ERROR) << "child_path contains \"..\" : " << child_path;
    return false;
  }

  if (child_path.IsAbsolute()) {
    LOG(ERROR) << "child_path is an absolute path : " << child_path;
    return false;
  }

  if (!homedirs_->CryptohomeExists(obfuscated_username)) {
    LOG(ERROR) << "A cryptohome vault doesn't exist for : "
               << obfuscated_username;
    return false;
  }

  base::FilePath path;
  switch (parent_path) {
    case PATH_DOWNLOADS:
      // /home/user/<obfuscated_username>/Downloads/<child_path>
      path = brillo::cryptohome::home::GetUserPathPrefix()
                 .Append(obfuscated_username)
                 .Append(kUserDownloadsDir)
                 .Append(child_path);
      break;
    case PATH_ANDROID_DATA:
      // /home/root/<obfuscated_username>/android-data/<child_path>
      path = brillo::cryptohome::home::GetRootPathPrefix()
                 .Append(obfuscated_username)
                 .Append(kAndroidDataDir)
                 .Append(child_path);
      break;
  }

  if (path.empty()) {
    LOG(ERROR) << "Invalid parent path type : " << parent_path;
    return false;
  }

  return platform_->SetQuotaProjectId(project_id, path);
}

base::FilePath ArcDiskQuota::GetDevice() {
  std::string device;
  if (!platform_->FindFilesystemDevice(home_, &device)) {
    LOG(ERROR) << "Home device is not found.";
    return base::FilePath();
  }

  // Check if the device is mounted with quota option.
  if (platform_->GetQuotaCurrentSpaceForUid(base::FilePath(device), 0) < 0) {
    LOG(ERROR) << "Device is not mounted with quota feature enabled.";
    return base::FilePath();
  }

  return base::FilePath(device);
}

}  // namespace cryptohome
