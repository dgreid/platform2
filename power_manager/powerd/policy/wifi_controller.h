// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_WIFI_CONTROLLER_H_
#define POWER_MANAGER_POWERD_POLICY_WIFI_CONTROLLER_H_

#include <string>

#include <base/macros.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/policy/user_proximity_handler.h"
#include "power_manager/powerd/system/udev.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"

namespace power_manager {

class PrefsInterface;

namespace policy {

// WifiController initiates power-related changes to the wifi chipset.
class WifiController : public system::UdevSubsystemObserver,
                       public UserProximityHandler::Delegate {
 public:
  // Performs work on behalf of WifiController.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Updates the wifi transmit power to |power|.
    virtual void SetWifiTransmitPower(RadioTransmitPower power,
                                      WifiRegDomain domain) = 0;
  };

  // Net subsystem and wlan devtype for udev events.
  static const char kUdevSubsystem[];
  static const char kUdevDevtype[];

  WifiController();
  WifiController(const WifiController&) = delete;
  WifiController& operator=(const WifiController&) = delete;

  ~WifiController() override;

  // Ownership of raw pointers remains with the caller.
  void Init(Delegate* delegate,
            PrefsInterface* prefs,
            system::UdevInterface* udev,
            TabletMode tablet_mode);

  // Called when the tablet mode changes.
  void HandleTabletModeChange(TabletMode mode);

  void HandleRegDomainChange(WifiRegDomain domain);

  // UserProximityHandler::Delegate overrides:
  void ProximitySensorDetected(UserProximity proximity) override;
  void HandleProximityChange(UserProximity proximity) override;

  // system::UdevSubsystemObserver:
  void OnUdevEvent(const system::UdevEvent& event) override;

 private:
  enum class UpdatePowerInputSource {
    NONE,
    TABLET_MODE,
    PROXIMITY,
  };

  // Updates transmit power via |delegate_|. Ends up invoking either one of
  // UpdateTransmitPowerFor*() depending on |update_power_input_source_|.
  void UpdateTransmitPower();

  void UpdateTransmitPowerForTabletMode();
  void UpdateTransmitPowerForProximity();

  UpdatePowerInputSource update_power_input_source_ =
      UpdatePowerInputSource::NONE;
  Delegate* delegate_ = nullptr;           // Not owned.
  system::UdevInterface* udev_ = nullptr;  // Not owned.

  TabletMode tablet_mode_ = TabletMode::UNSUPPORTED;
  WifiRegDomain wifi_reg_domain_ = WifiRegDomain::NONE;
  UserProximity proximity_ = UserProximity::UNKNOWN;

  // True if powerd has been configured to set wifi transmit power in response
  // to tablet mode changes.
  bool set_transmit_power_for_tablet_mode_ = false;

  // True if powerd has been configured to set wifi transmit power in response
  // to proximity changes.
  bool set_transmit_power_for_proximity_ = false;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_WIFI_CONTROLLER_H_
