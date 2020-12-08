// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_MOCK_MOUNT_FACTORY_H_
#define CRYPTOHOME_STORAGE_MOCK_MOUNT_FACTORY_H_

#include "cryptohome/storage/mount_factory.h"

#include <gmock/gmock.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount.h"

using ::testing::_;

namespace cryptohome {
class Mount;
class MockMountFactory : public MountFactory {
 public:
  MockMountFactory() {
    ON_CALL(*this, New(_, _))
        .WillByDefault(testing::Invoke(this, &MockMountFactory::NewConcrete));
  }

  virtual ~MockMountFactory() {}
  MOCK_METHOD(Mount*, New, (Platform*, HomeDirs*), (override));

  // Backdoor to access real method, for delegating calls to parent class
  Mount* NewConcrete(Platform* platform, HomeDirs* homedirs) {
    return MountFactory::New(platform, homedirs);
  }
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_MOCK_MOUNT_FACTORY_H_
