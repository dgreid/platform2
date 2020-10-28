// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/arc_disk_quota.h"

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

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
