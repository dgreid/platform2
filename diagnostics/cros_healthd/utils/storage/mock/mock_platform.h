// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_MOCK_MOCK_PLATFORM_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_MOCK_MOCK_PLATFORM_H_

#include <string>

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/utils/storage/platform.h"

namespace diagnostics {

class MockPlatform : public Platform {
 public:
  MockPlatform() = default;
  MockPlatform(const MockPlatform&) = delete;
  MockPlatform(MockPlatform&&) = delete;
  MockPlatform& operator=(const MockPlatform&) = delete;
  MockPlatform& operator=(MockPlatform&&) = delete;
  ~MockPlatform() override = default;

  MOCK_METHOD(std::string, GetRootDeviceName, (), (const, override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_MOCK_MOCK_PLATFORM_H_
