// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_MOUNT_FACTORY_H_
#define CRYPTOHOME_STORAGE_MOUNT_FACTORY_H_

#include <string>
#include <vector>

#include "cryptohome/platform.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount.h"

namespace cryptohome {
class Mount;

// Provide a means for mocks to be injected anywhere that new Mount objects
// are created.
class MountFactory {
 public:
  MountFactory();
  virtual ~MountFactory();
  virtual Mount* New(Platform*, HomeDirs*);
};

}  // namespace cryptohome
#endif  // CRYPTOHOME_STORAGE_MOUNT_FACTORY_H_
