// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/mount_factory.h"

#include "cryptohome/platform.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount.h"

namespace cryptohome {

MountFactory::MountFactory() {}
MountFactory::~MountFactory() {}

Mount* MountFactory::New(Platform* platform, HomeDirs* homedirs) {
  return new Mount(platform, homedirs);
}
}  // namespace cryptohome
