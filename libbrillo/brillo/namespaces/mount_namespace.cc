// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class MountNamespace for libbrillo.

#include "brillo/namespaces/mount_namespace.h"

#include <sched.h>
#include <sys/mount.h>
#include <sys/types.h>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/namespaces/platform.h>

namespace {
constexpr char kProcNsPath[] = "/proc/self/ns/mnt";
}

namespace brillo {
MountNamespace::MountNamespace(const base::FilePath& ns_path,
                               Platform* platform)
    : ns_path_(ns_path), platform_(platform), exists_(false) {}

MountNamespace::~MountNamespace() {
  if (exists_)
    Destroy();
}

bool MountNamespace::Create() {
  if (platform_->FileSystemIsNsfs(ns_path_)) {
    LOG(ERROR) << "Mount namespace at " << ns_path_.value()
               << " already exists.";
    return false;
  }
  pid_t pid = platform_->Fork();
  if (pid < 0) {
    PLOG(ERROR) << "Fork failed.";
  } else if (pid == 0) {
    // Child.
    if (unshare(CLONE_NEWNS) == 0 &&
        mount(kProcNsPath, ns_path_.value().c_str(), nullptr, MS_BIND,
              nullptr) == 0) {
      exit(0);
    }
    exit(1);
  } else {
    // Parent.
    int status;
    if (platform_->Waitpid(pid, &status) < 0) {
      PLOG(ERROR) << "waitpid(" << pid << ") failed.";
      return false;
    }

    if (!WIFEXITED(status)) {
      LOG(ERROR) << "Child process did not exit normally.";
    } else if (WEXITSTATUS(status) != 0) {
      LOG(ERROR) << "Child process failed to create namespace.";
    } else {
      exists_ = true;
    }
  }
  return exists_;
}

bool MountNamespace::Destroy() {
  if (!exists_) {
    LOG(ERROR) << "Mount namespace at " << ns_path_.value()
               << "does not exist, cannot destroy";
    return false;
  }
  bool was_busy;
  if (!platform_->Unmount(ns_path_, false /*lazy*/, &was_busy)) {
    PLOG(ERROR) << "Failed to unmount " << ns_path_.value();
    if (was_busy) {
      LOG(ERROR) << ns_path_.value().c_str() << " was busy";
    }
    // If Unmount() fails, keep the object valid by keeping |exists_|
    // set to true.
    return false;
  } else {
    VLOG(1) << "Unmounted namespace at " << ns_path_.value();
  }
  exists_ = false;
  return true;
}

}  // namespace brillo
