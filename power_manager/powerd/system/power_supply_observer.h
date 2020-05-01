// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_POWER_SUPPLY_OBSERVER_H_
#define POWER_MANAGER_POWERD_SYSTEM_POWER_SUPPLY_OBSERVER_H_

#include <base/observer_list_types.h>

namespace power_manager {
namespace system {

struct PowerStatus;

class PowerSupplyObserver : public base::CheckedObserver {
 public:
  virtual ~PowerSupplyObserver() {}

  // Called when the power status has been updated.
  virtual void OnPowerStatusUpdate() = 0;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_POWER_SUPPLY_OBSERVER_H_
