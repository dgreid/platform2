// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_NAMESPACES_MOUNT_NAMESPACE_H_
#define LIBBRILLO_BRILLO_NAMESPACES_MOUNT_NAMESPACE_H_

#include "brillo/namespaces/platform.h"

#include <base/files/file_path.h>
#include <base/macros.h>
#include <brillo/brillo_export.h>

namespace brillo {

class BRILLO_EXPORT MountNamespace {
  // A class to create a persistent mount namespace bound to a specific path.
  // A new mount namespace is unshared from the mount namespace of the calling
  // process when Create() is called; the namespace of the calling process
  // remains unchanged. Recurring creation on a path is not allowed.
  //
  // Given that we cannot ensure that creation always succeeds this class is not
  // fully RAII, but once the namespace is created (with Create()), it will be
  // destroyed when the object goes out of scope.
 public:
  MountNamespace(const base::FilePath& ns_path, Platform* platform);
  ~MountNamespace();

  bool Create();
  bool Destroy();
  const base::FilePath& path() const { return ns_path_; }

 private:
  base::FilePath ns_path_;
  Platform* platform_;
  bool exists_;

  DISALLOW_COPY_AND_ASSIGN(MountNamespace);
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_NAMESPACES_MOUNT_NAMESPACE_H_
