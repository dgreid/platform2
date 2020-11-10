// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_CROS_EC_HELPER_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_CROS_EC_HELPER_STUB_H_

#include <map>
#include <string>

#include <base/macros.h>

#include "power_manager/powerd/system/cros_ec_helper_interface.h"

namespace power_manager {
namespace system {

class CrosEcHelperStub : public CrosEcHelperInterface {
 public:
  CrosEcHelperStub();
  CrosEcHelperStub(const CrosEcHelperStub&) = delete;
  CrosEcHelperStub& operator=(const CrosEcHelperStub&) = delete;

  ~CrosEcHelperStub() override;

  // Implementation of EcHelperInterface.
  bool IsWakeAngleSupported() override;
  bool AllowWakeupAsTablet(bool enabled) override;

  bool IsWakeupAsTabletAllowed();

 private:
  bool wakeup_as_tablet_allowed_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_CROS_EC_HELPER_STUB_H_
