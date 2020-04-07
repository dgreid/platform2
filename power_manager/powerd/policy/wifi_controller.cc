// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/wifi_controller.h"

#include "power_manager/common/prefs.h"

namespace power_manager {
namespace policy {

const char WifiController::kUdevSubsystem[] = "net";
const char WifiController::kUdevDevtype[] = "wlan";

WifiController::WifiController() = default;

WifiController::~WifiController() {
  if (udev_)
    udev_->RemoveSubsystemObserver(kUdevSubsystem, this);
}

void WifiController::Init(Delegate* delegate,
                          PrefsInterface* prefs,
                          system::UdevInterface* udev,
                          TabletMode tablet_mode) {
  DCHECK(delegate);
  DCHECK(prefs);
  DCHECK(udev);

  delegate_ = delegate;
  udev_ = udev;
  tablet_mode_ = tablet_mode;

  prefs->GetBool(kSetWifiTransmitPowerForTabletModePref,
                 &set_transmit_power_for_tablet_mode_);
  prefs->GetBool(kSetWifiTransmitPowerForProximityPref,
                 &set_transmit_power_for_proximity_);

  if (set_transmit_power_for_tablet_mode_)
    update_power_input_source_ = UpdatePowerInputSource::TABLET_MODE;

  udev_->AddSubsystemObserver(kUdevSubsystem, this);
  UpdateTransmitPower();
}

void WifiController::HandleTabletModeChange(TabletMode mode) {
  if (tablet_mode_ == mode)
    return;

  tablet_mode_ = mode;
  UpdateTransmitPower();
}

void WifiController::HandleRegDomainChange(WifiRegDomain domain) {
  if (wifi_reg_domain_ == domain)
    return;

  wifi_reg_domain_ = domain;
  UpdateTransmitPower();
}

void WifiController::ProximitySensorDetected(UserProximity value) {
  if (!set_transmit_power_for_proximity_)
    return;

  update_power_input_source_ = UpdatePowerInputSource::PROXIMITY;
  LOG(INFO) << "Wifi power will be handled by proximity sensor";
  HandleProximityChange(value);
}

void WifiController::HandleProximityChange(UserProximity proximity) {
  if (proximity_ == proximity)
    return;

  proximity_ = proximity;
  UpdateTransmitPower();
}

void WifiController::OnUdevEvent(const system::UdevEvent& event) {
  DCHECK_EQ(event.device_info.subsystem, kUdevSubsystem);
  if (event.action == system::UdevEvent::Action::ADD &&
      event.device_info.devtype == kUdevDevtype)
    UpdateTransmitPower();
}

void WifiController::UpdateTransmitPower() {
  switch (update_power_input_source_) {
    case UpdatePowerInputSource::TABLET_MODE:
      UpdateTransmitPowerForTabletMode();
      break;
    case UpdatePowerInputSource::PROXIMITY:
      UpdateTransmitPowerForProximity();
      break;
    case UpdatePowerInputSource::NONE:
      break;
  }
}

void WifiController::UpdateTransmitPowerForTabletMode() {
  switch (tablet_mode_) {
    case TabletMode::UNSUPPORTED:
      break;
    case TabletMode::ON:
      delegate_->SetWifiTransmitPower(RadioTransmitPower::LOW,
                                      wifi_reg_domain_);
      break;
    case TabletMode::OFF:
      delegate_->SetWifiTransmitPower(RadioTransmitPower::HIGH,
                                      wifi_reg_domain_);
      break;
  }
}

void WifiController::UpdateTransmitPowerForProximity() {
  switch (proximity_) {
    case UserProximity::UNKNOWN:
      break;
    case UserProximity::NEAR:
      delegate_->SetWifiTransmitPower(RadioTransmitPower::LOW,
                                      wifi_reg_domain_);
      break;
    case UserProximity::FAR:
      delegate_->SetWifiTransmitPower(RadioTransmitPower::HIGH,
                                      wifi_reg_domain_);
      break;
  }
}

}  // namespace policy
}  // namespace power_manager
