// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_NAMESPACES_MOCK_PLATFORM_H_
#define LIBBRILLO_BRILLO_NAMESPACES_MOCK_PLATFORM_H_

#include "brillo/namespaces/platform.h"

#include <base/files/file_path.h>
#include <gmock/gmock.h>

namespace brillo {

class MockPlatform : public Platform {
 public:
  MockPlatform() {}
  virtual ~MockPlatform() {}

  MOCK_METHOD(bool, Unmount, (const base::FilePath&, bool, bool*), (override));
  MOCK_METHOD(pid_t, Fork, (), (override));
  MOCK_METHOD(pid_t, Waitpid, (pid_t, int*), (override));
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_NAMESPACES_MOCK_PLATFORM_H_
