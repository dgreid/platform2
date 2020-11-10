// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class allows creation and destruction of a persistent mount namespace.

#ifndef CRYPTOHOME_MOUNT_NAMESPACE_H_
#define CRYPTOHOME_MOUNT_NAMESPACE_H_

#include <sys/types.h>

#include <base/files/file_path.h>
#include <base/macros.h>

#include "cryptohome/platform.h"

namespace cryptohome {

class MountNamespace {
  // Given that we cannot ensure that creation always succeeds this class is not
  // fully RAII, but once the namespace is created (with Create()), it will be
  // destroyed when the object goes out of scope.
 public:
  MountNamespace(const base::FilePath& ns_path, Platform* platform)
      : ns_path_(ns_path), exists_(false), platform_(platform) {}
  MountNamespace(const MountNamespace&) = delete;
  MountNamespace& operator=(const MountNamespace&) = delete;

  ~MountNamespace();

  base::FilePath path() const { return ns_path_; }

  bool Create();
  bool Destroy();

 private:
  base::FilePath ns_path_;
  bool exists_;
  Platform* platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOUNT_NAMESPACE_H_
