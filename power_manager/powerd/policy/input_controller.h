// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_INPUT_CONTROLLER_H_
#define POWER_MANAGER_POWERD_POLICY_INPUT_CONTROLLER_H_

#include <memory>

#include <base/compiler_specific.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/input_observer.h"

namespace power_manager {

class Clock;
class DBusSenderInterface;
class PrefsInterface;

namespace system {
class DisplayWatcherInterface;
class InputWatcherInterface;
}  // namespace system

namespace policy {

// InputController responds to input events (e.g. lid open/close, power button,
// etc.).
class InputController : public system::InputObserver {
 public:
  // Interface for delegates responsible for performing actions on behalf of
  // InputController.
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Handles the lid being closed.
    virtual void HandleLidClosed() = 0;

    // Handles the lid being opened.
    virtual void HandleLidOpened() = 0;

    // Handles the power button being pressed or released.
    virtual void HandlePowerButtonEvent(ButtonState state) = 0;

    // Handles hovering/proximity changes.
    virtual void HandleHoverStateChanged(bool hovering) = 0;

    // Handles the device entering or leaving tablet mode.
    virtual void HandleTabletModeChanged(TabletMode mode) = 0;

    // Defers the inactivity timeout in response to VT2 being active (since
    // Chrome can't detect user activity).
    virtual void DeferInactivityTimeoutForVT2() = 0;

    // Shuts the system down in reponse to the power button being pressed while
    // no display is connected.
    virtual void ShutDownForPowerButtonWithNoDisplay() = 0;

    // Handles Chrome failing to acknowledge a power button press quickly
    // enough.
    virtual void HandleMissingPowerButtonAcknowledgment() = 0;

    // Sends a metric reporting how long Chrome took to acknowledge a power
    // button press.
    virtual void ReportPowerButtonAcknowledgmentDelay(base::TimeDelta delay)
        = 0;
  };

  // Amount of time to wait for Chrome to acknowledge power button presses, in
  // milliseconds.
  static const int kPowerButtonAcknowledgmentTimeoutMs = 2000;

  InputController();
  virtual ~InputController();

  Clock* clock_for_testing() { return clock_.get(); }

  // Ownership of passed-in pointers remains with the caller.
  void Init(system::InputWatcherInterface* input_watcher,
            Delegate* delegate,
            system::DisplayWatcherInterface* display_watcher,
            DBusSenderInterface* dbus_sender,
            PrefsInterface* prefs);

  // Calls HandlePowerButtonAcknowledgmentTimeout(). Returns false if
  // |power_button_acknowledgment_timer_| isn't running.
  bool TriggerPowerButtonAcknowledgmentTimeoutForTesting();

  // Calls CheckActiveVT(). Returns false if |check_active_vt_timer| isn't
  // running.
  bool TriggerCheckActiveVTTimeoutForTesting();

  // Handles acknowledgment from that a power button press was handled.
  // |timestamp| is the timestamp from the original event.
  void HandlePowerButtonAcknowledgment(const base::TimeTicks& timestamp);

  // system::InputObserver implementation:
  void OnLidEvent(LidState state) override;
  void OnTabletModeEvent(TabletMode mode) override;
  void OnPowerButtonEvent(ButtonState state) override;
  void OnHoverStateChanged(bool hovering) override;

 private:
  // Asks |delegate_| to defer the inactivity timeout if the second virtual
  // terminal is currently active (which typically means that the user is doing
  // something on the console in dev mode, so Chrome won't be reporting user
  // activity to keep power management from kicking in).
  void CheckActiveVT();

  // Tells |delegate_| when Chrome hasn't acknowledged a power button press
  // quickly enough.
  void HandlePowerButtonAcknowledgmentTimeout();

  system::InputWatcherInterface* input_watcher_;  // weak
  Delegate* delegate_;  // weak
  system::DisplayWatcherInterface* display_watcher_;  // weak
  DBusSenderInterface* dbus_sender_;  // weak

  std::unique_ptr<Clock> clock_;

  // True if the device doesn't have an internal display.
  bool only_has_external_display_;

  LidState lid_state_;
  TabletMode tablet_mode_;

  // Timestamp from the most recent power-button-down event that Chrome is
  // expected to acknowledge. Unset when the power button isn't pressed or if
  // Chrome has already acknowledged the event.
  base::TimeTicks expected_power_button_acknowledgment_timestamp_;

  // Calls HandlePowerButtonAcknowledgmentTimeout().
  base::OneShotTimer power_button_acknowledgment_timer_;

  // Calls CheckActiveVT() periodically.
  base::RepeatingTimer check_active_vt_timer_;

  DISALLOW_COPY_AND_ASSIGN(InputController);
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_INPUT_CONTROLLER_H_
