// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_CROS_EC_HELPER_INTERFACE_H_
#define POWER_MANAGER_POWERD_SYSTEM_CROS_EC_HELPER_INTERFACE_H_

#include <base/macros.h>

namespace power_manager {
namespace system {

// Helper class to manipulate EC settings.
class CrosEcHelperInterface {
 public:
  CrosEcHelperInterface() {}
  CrosEcHelperInterface(const CrosEcHelperInterface&) = delete;
  CrosEcHelperInterface& operator=(const CrosEcHelperInterface&) = delete;

  virtual ~CrosEcHelperInterface() {}

  // Checks whether EC supports setting wake angle.
  virtual bool IsWakeAngleSupported() = 0;

  // Controls whether the EC will allow keyboard wakeups in tablet mode.
  // Returns true on success.
  virtual bool AllowWakeupAsTablet(bool enabled) = 0;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_CROS_EC_HELPER_INTERFACE_H_
