// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_MOUNT_FACTORY_H_
#define CRYPTOHOME_MOCK_MOUNT_FACTORY_H_

#include "cryptohome/mount_factory.h"

#include <gmock/gmock.h>

#include "cryptohome/homedirs.h"
#include "cryptohome/mount.h"
#include "cryptohome/platform.h"

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

#endif  // CRYPTOHOME_MOCK_MOUNT_FACTORY_H_
