// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_MOCK_SYSTEM_PROPERTIES_H_
#define DLCSERVICE_MOCK_SYSTEM_PROPERTIES_H_

#include <gmock/gmock.h>

#include "dlcservice/system_properties.h"

namespace dlcservice {

class MockSystemProperties : public SystemProperties {
 public:
  MockSystemProperties() = default;

  MOCK_METHOD(bool, IsOfficialBuild, (), (override));

 private:
  MockSystemProperties(const MockSystemProperties&) = delete;
  MockSystemProperties& operator=(const MockSystemProperties&) = delete;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_MOCK_SYSTEM_PROPERTIES_H_
