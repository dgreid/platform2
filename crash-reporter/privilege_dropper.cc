// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <grp.h>
#include <stdlib.h>
#include <unistd.h>

#include <base/logging.h>
#include <brillo/userdb_utils.h>

#include "crash-reporter/constants.h"
#include "crash-reporter/privilege_dropper.h"
#include "crash-reporter/vm_support.h"

ScopedPrivilegeDropper::ScopedPrivilegeDropper() {
  if (VmSupport::Get()) {
    LOG(WARNING) << "Not dropping privileges inside a VM";
    return;
  }

  if (getuid() != 0) {
    LOG(WARNING) << "Not dropping privileges because we are not root";
    return;
  }
  uid_t uid;
  gid_t gid, crash_access_gid, crash_user_access_gid;
  if (!brillo::userdb::GetUserInfo(constants::kCrashName, &uid, &gid)) {
    LOG(FATAL) << "Failed to get target UID/GID";
  }
  if (!brillo::userdb::GetGroupInfo(constants::kCrashGroupName,
                                    &crash_access_gid)) {
    LOG(FATAL) << "Failed to get crash-access gid";
  }
  if (!brillo::userdb::GetGroupInfo(constants::kCrashUserGroupName,
                                    &crash_user_access_gid)) {
    LOG(FATAL) << "Failed to get crash-access gid";
  }

  if (setresgid(gid, gid, 0) != 0) {
    PLOG(FATAL) << "Failed to set effective gid";
  }
  gid_t extra_grps[] = {crash_access_gid, crash_user_access_gid};
  if (setgroups(2, extra_grps) != 0) {
    PLOG(FATAL) << "Failed to set groups";
  }
  if (setresuid(uid, uid, 0) != 0) {
    PLOG(FATAL) << "Failed to set effective uid";
  }

  dropped_privs_ = true;
}

ScopedPrivilegeDropper::~ScopedPrivilegeDropper() {
  if (!dropped_privs_) {
    return;
  }

  if (setresgid(0, 0, 0) != 0) {
    PLOG(FATAL) << "Failed to restore GID";
  }
  if (setresuid(0, 0, 0) != 0) {
    PLOG(FATAL) << "Failed to restore UID";
  }
}
