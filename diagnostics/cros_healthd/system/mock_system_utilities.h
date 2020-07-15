// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_SYSTEM_UTILITIES_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_SYSTEM_UTILITIES_H_

#include <sys/utsname.h>

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/system/system_utilities.h"

namespace diagnostics {

// Mock implementation of the SystemUtilities interface.
class MockSystemUtilities final : public SystemUtilities {
 public:
  MockSystemUtilities();
  MockSystemUtilities(const MockSystemUtilities&) = delete;
  MockSystemUtilities& operator=(const MockSystemUtilities&) = delete;
  ~MockSystemUtilities() override;

  // SystemUtilities overrides:
  MOCK_METHOD(int, Uname, (struct utsname*), (override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_SYSTEM_UTILITIES_H_
