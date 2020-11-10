// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_EXTERNAL_BACKLIGHT_CONTROLLER_H_
#define POWER_MANAGER_POWERD_POLICY_EXTERNAL_BACKLIGHT_CONTROLLER_H_

#include <map>
#include <memory>
#include <vector>

#include <base/compiler_specific.h>
#include <base/macros.h>
#include <base/observer_list.h>

#include "power_manager/powerd/policy/backlight_controller.h"
#include "power_manager/powerd/system/display/display_watcher_observer.h"
#include "power_manager/proto_bindings/backlight.pb.h"

namespace power_manager {

namespace system {
class DBusWrapperInterface;
struct DisplayInfo;
class DisplayPowerSetterInterface;
class DisplayWatcherInterface;
class ExternalDisplay;
}  // namespace system

namespace policy {

// Controls the brightness of an external display on machines that lack internal
// displays.
class ExternalBacklightController : public BacklightController,
                                    public system::DisplayWatcherObserver {
 public:
  ExternalBacklightController();
  ExternalBacklightController(const ExternalBacklightController&) = delete;
  ExternalBacklightController& operator=(const ExternalBacklightController&) =
      delete;

  ~ExternalBacklightController() override;

  // Initializes the object. Ownership of raw pointers remains with the caller.
  void Init(system::DisplayWatcherInterface* display_watcher,
            system::DisplayPowerSetterInterface* display_power_setter,
            system::DBusWrapperInterface* dbus_wrapper);

  // BacklightController implementation:
  void AddObserver(BacklightControllerObserver* observer) override;
  void RemoveObserver(BacklightControllerObserver* observer) override;
  void HandlePowerSourceChange(PowerSource source) override;
  void HandleDisplayModeChange(DisplayMode mode) override;
  void HandleSessionStateChange(SessionState state) override;
  void HandlePowerButtonPress() override;
  void HandleLidStateChange(LidState state) override;
  void HandleUserActivity(UserActivityType type) override;
  void HandleVideoActivity(bool is_fullscreen) override;
  void HandleWakeNotification() override;
  void HandleHoverStateChange(bool hovering) override;
  void HandleTabletModeChange(TabletMode mode) override;
  void HandlePolicyChange(const PowerManagementPolicy& policy) override;
  void HandleDisplayServiceStart() override;
  void SetDimmedForInactivity(bool dimmed) override;
  void SetOffForInactivity(bool off) override;
  void SetSuspended(bool suspended) override;
  void SetShuttingDown(bool shutting_down) override;
  void SetForcedOff(bool forced_off) override;
  bool GetForcedOff() override;
  bool GetBrightnessPercent(double* percent) override;
  int GetNumAmbientLightSensorAdjustments() const override;
  int GetNumUserAdjustments() const override;
  double LevelToPercent(int64_t level) const override;
  int64_t PercentToLevel(double percent) const override;

  // system::DisplayWatcherObserver implementation:
  void OnDisplaysChanged(
      const std::vector<system::DisplayInfo>& displays) override;

 private:
  // Handlers for requests sent via D-Bus.
  void HandleIncreaseBrightnessRequest();
  void HandleDecreaseBrightnessRequest(bool allow_off);
  void HandleSetBrightnessRequest(double percent,
                                  Transition transition,
                                  SetBacklightBrightnessRequest_Cause cause);
  void HandleGetBrightnessRequest(double* percent_out, bool* success_out);

  // Turns displays on or off via |monitor_reconfigure_| as needed for
  // |off_for_inactivity_|, |suspended_|, and |shutting_down_|.
  void UpdateScreenPowerState(BacklightBrightnessChange_Cause cause);

  // Sends notifications to |observers_| about the current brightness level.
  void NotifyObservers(BacklightBrightnessChange_Cause cause);

  // Updates |external_displays_| for |displays|.
  void UpdateDisplays(const std::vector<system::DisplayInfo>& displays);

  // Adjusts |external_displays_| by |percent_offset|, a linearly-calculated
  // percent in the range [-100.0, 100.0].
  void AdjustBrightnessByPercent(double percent_offset);

  // These pointers aren't owned by this class.
  system::DisplayWatcherInterface* display_watcher_ = nullptr;
  system::DisplayPowerSetterInterface* display_power_setter_ = nullptr;
  system::DBusWrapperInterface* dbus_wrapper_ = nullptr;

  base::ObserverList<BacklightControllerObserver> observers_;

  bool dimmed_for_inactivity_ = false;
  bool off_for_inactivity_ = false;
  bool suspended_ = false;
  bool shutting_down_ = false;
  bool forced_off_ = false;

  // Are the external displays currently turned off?
  bool currently_off_ = false;

  // Map from DRM device directories to ExternalDisplay objects for controlling
  // the corresponding displays.
  typedef std::map<base::FilePath, std::unique_ptr<system::ExternalDisplay>>
      ExternalDisplayMap;
  ExternalDisplayMap external_displays_;

  // Number of times the user has requested that the brightness be changed in
  // the current session.
  int num_brightness_adjustments_in_session_ = 0;

  base::WeakPtrFactory<ExternalBacklightController> weak_ptr_factory_;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_EXTERNAL_BACKLIGHT_CONTROLLER_H_
