// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mount_namespace.h"

#include <memory>
#include <string>

#include <base/files/file.h>
#include <base/logging.h>
#include <brillo/process/process.h>

namespace cryptohome {

MountNamespace::~MountNamespace() {
  if (exists_) {
    Destroy();
  }
}

bool MountNamespace::Create() {
  if (exists_) {
    LOG(ERROR) << "Non-root mount namespace at " << ns_path_.value()
               << " already exists, cannot create";
    return false;
  }

  std::unique_ptr<brillo::Process> unshare = platform_->CreateProcessInstance();
  unshare->AddArg("/usr/bin/unshare");

  std::string mount =
      base::StringPrintf("--mount=%s", ns_path_.value().c_str());
  unshare->AddArg(mount);
  unshare->AddArg("--propagation=unchanged");
  unshare->AddArg("--");
  unshare->AddArg("/bin/true");

  int rc = unshare->Run();
  if (rc != 0) {
    LOG(ERROR) << "Failed to run 'unshare " << mount
               << "--propagation=unchanged -- /bin/true'";
  }
  exists_ = rc == 0;
  return exists_;
}

bool MountNamespace::Destroy() {
  if (!exists_) {
    LOG(ERROR) << "Non-root mount namespace at " << ns_path_.value()
               << " does not exist, cannot destroy";
    return false;
  }

  bool was_busy;
  if (!platform_->Unmount(base::FilePath(ns_path_), false /* lazy */,
                          &was_busy)) {
    PLOG(ERROR) << "Failed to unmount " << ns_path_.value();
    if (was_busy) {
      LOG(ERROR) << ns_path_.value() << " was busy";
    }
    // If Unmount() fails, keep the object valid by keeping |exists_| set to
    // true.
    return false;
  } else {
    VLOG(1) << "Unmounted namespace at " << ns_path_.value();
  }
  exists_ = false;
  return true;
}

}  // namespace cryptohome
