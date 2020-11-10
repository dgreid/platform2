// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_CELLULAR_CONTROLLER_H_
#define POWER_MANAGER_POWERD_POLICY_CELLULAR_CONTROLLER_H_

#include <string>

#include <base/macros.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/policy/user_proximity_handler.h"
#include "power_manager/powerd/system/udev.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"

namespace power_manager {

class PrefsInterface;

namespace policy {

// CellularController initiates power-related changes to the cellular chipset.
class CellularController : public UserProximityHandler::Delegate {
 public:
  // Performs work on behalf of CellularController.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Updates the transmit power to |power| via the dynamic power reduction
    // signal controlled by the specified GPIO number.
    virtual void SetCellularTransmitPower(RadioTransmitPower power,
                                          int64_t dpr_gpio_number) = 0;
  };

  CellularController();
  CellularController(const CellularController&) = delete;
  CellularController& operator=(const CellularController&) = delete;

  ~CellularController() override;

  // Ownership of raw pointers remains with the caller.
  void Init(Delegate* delegate, PrefsInterface* prefs);

  // Called when the tablet mode changes.
  void HandleTabletModeChange(TabletMode mode);

  // UserProximityHandler::Delegate overrides:
  void ProximitySensorDetected(UserProximity proximity) override;
  void HandleProximityChange(UserProximity proximity) override;

 private:
  // Updates transmit power via |delegate_|.
  void UpdateTransmitPower();

  RadioTransmitPower DetermineTransmitPower() const;

  Delegate* delegate_ = nullptr;  // Not owned.

  TabletMode tablet_mode_ = TabletMode::UNSUPPORTED;
  UserProximity proximity_ = UserProximity::UNKNOWN;

  // True if powerd has been configured to set cellular transmit power in
  // response to tablet mode or proximity changes.
  bool set_transmit_power_for_tablet_mode_ = false;
  bool set_transmit_power_for_proximity_ = false;

  // GPIO number for the dynamic power reduction signal of a built-in cellular
  // modem.
  int64_t dpr_gpio_number_ = -1;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_CELLULAR_CONTROLLER_H_
