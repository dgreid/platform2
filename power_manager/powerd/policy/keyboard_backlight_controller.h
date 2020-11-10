// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_KEYBOARD_BACKLIGHT_CONTROLLER_H_
#define POWER_MANAGER_POWERD_POLICY_KEYBOARD_BACKLIGHT_CONTROLLER_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include <base/compiler_specific.h>
#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

#include "power_manager/powerd/policy/ambient_light_handler.h"
#include "power_manager/powerd/policy/backlight_controller.h"
#include "power_manager/powerd/policy/backlight_controller_observer.h"
#include "power_manager/powerd/system/backlight_observer.h"

namespace power_manager {

class Clock;
class PrefsInterface;

namespace system {
class AmbientLightSensorInterface;
class BacklightInterface;
class DBusWrapperInterface;
}  // namespace system

namespace policy {

class KeyboardBacklightControllerTest;

// Controls the keyboard backlight for devices with such a backlight.
class KeyboardBacklightController : public BacklightController,
                                    public AmbientLightHandler::Delegate,
                                    public BacklightControllerObserver,
                                    public system::BacklightObserver {
 public:
  // Helper class for tests that need to access internal state.
  class TestApi {
   public:
    explicit TestApi(KeyboardBacklightController* controller);
    TestApi(const TestApi&) = delete;
    TestApi& operator=(const TestApi&) = delete;

    ~TestApi();

    Clock* clock() { return controller_->clock_.get(); }

    // Triggers |turn_off_timer_| or |video_timer_| and returns true. Returns
    // false if the timer wasn't running.
    bool TriggerTurnOffTimeout() WARN_UNUSED_RESULT;
    bool TriggerVideoTimeout() WARN_UNUSED_RESULT;

   private:
    KeyboardBacklightController* controller_;  // weak

  };

  // Backlight brightness percent to use when the screen is dimmed.
  static const double kDimPercent;

  KeyboardBacklightController();
  KeyboardBacklightController(const KeyboardBacklightController&) = delete;
  KeyboardBacklightController& operator=(const KeyboardBacklightController&) =
      delete;

  ~KeyboardBacklightController() override;

  // Initializes the object. Ownership of passed-in pointers remains with the
  // caller. |sensor| and |display_backlight_controller| may be NULL.
  void Init(system::BacklightInterface* backlight,
            PrefsInterface* prefs,
            system::AmbientLightSensorInterface* sensor,
            system::DBusWrapperInterface* dbus_wrapper,
            BacklightController* display_backlight_controller,
            LidState initial_lid_state,
            TabletMode initial_tablet_mode);

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

  // AmbientLightHandler::Delegate implementation:
  void SetBrightnessPercentForAmbientLight(
      double brightness_percent,
      AmbientLightHandler::BrightnessChangeCause cause) override;
  void OnColorTemperatureChanged(int color_temperature) override;

  // BacklightControllerObserver implementation:
  void OnBrightnessChange(double brightness_percent,
                          BacklightBrightnessChange_Cause cause,
                          BacklightController* source) override;

  // system::BacklightObserver implementation:
  void OnBacklightDeviceChanged(system::BacklightInterface* backlight) override;

 private:
  // Handles |video_timer_| firing, indicating that video activity has stopped.
  void HandleVideoTimeout();

  // Returns true if hovering is active or if user activity or hovering was
  // observed recently enough that the backlight should be kept on.
  bool RecentlyHoveringOrUserActive() const;

  // Initializes |user_step_index_| when transitioning from ALS to user control.
  void InitUserStepIndex();

  // Stops or starts |turn_off_timer_| as needed based on the current values of
  // |hovering_|, |last_hover_time_|, and |last_user_activity_time_|.
  void UpdateTurnOffTimer();

  // Handlers for requests sent via D-Bus.
  void HandleIncreaseBrightnessRequest();
  void HandleDecreaseBrightnessRequest(bool allow_off);
  void HandleGetBrightnessRequest(double* percent_out, bool* success_out);

  // Updates the current brightness after assessing the current state (based on
  // |dimmed_for_inactivity_|, |off_for_inactivity_|, etc.). Should be called
  // whenever the state changes. |transition| and |cause| are passed to
  // ApplyBrightnessPercent(). Returns true if the brightness was changed and
  // false otherwise.
  bool UpdateState(Transition transition,
                   BacklightBrightnessChange_Cause cause);

  // Sets the backlight's brightness to |percent| over |transition|.
  // Returns true and notifies observers if the brightness was changed.
  bool ApplyBrightnessPercent(double percent,
                              Transition transition,
                              BacklightBrightnessChange_Cause cause);

  // Returns true if the |user_steps_| is valid; otherwise returns false.
  bool ValidateUserSteps(std::string* err_msg);

  // Calculates scaled percentages in |user_steps_| from raw percentages.
  void ScaleUserSteps();

  // Calculates raw percentages to scaled percentages in |user_steps_|.
  double RawPercentToPercent(double raw_percent) const;

  // Calculates scaled percentages in |user_steps_| to raw percentages.
  double PercentToRawPercent(double percent) const;

  mutable std::unique_ptr<Clock> clock_;

  // Not owned by this class.
  system::BacklightInterface* backlight_ = nullptr;
  PrefsInterface* prefs_ = nullptr;
  system::DBusWrapperInterface* dbus_wrapper_ = nullptr;

  // Controller responsible for the display's brightness. Weak pointer.
  BacklightController* display_backlight_controller_ = nullptr;

  // May be NULL if no ambient light sensor is present.
  std::unique_ptr<AmbientLightHandler> ambient_light_handler_;

  // Observers to notify about changes.
  base::ObserverList<BacklightControllerObserver> observers_;

  // True if the system is capable of detecting whether the user's hands are
  // hovering over the touchpad.
  bool supports_hover_ = false;

  // True if the keyboard should be turned on for user activity but kept off
  // otherwise. This has no effect if |supports_hover_| is set.
  bool turn_on_for_user_activity_ = false;

  SessionState session_state_ = SessionState::STOPPED;
  LidState lid_state_ = LidState::NOT_PRESENT;
  TabletMode tablet_mode_ = TabletMode::UNSUPPORTED;

  bool dimmed_for_inactivity_ = false;
  bool off_for_inactivity_ = false;
  bool suspended_ = false;
  bool shutting_down_ = false;
  bool forced_off_ = false;
  bool hovering_ = false;

  // Is a fullscreen video currently being played?
  bool fullscreen_video_playing_ = false;

  // Current percentage that |backlight_| is set to (or possibly in the process
  // of transitioning to), in the range [0.0, 100.0].
  double current_percent_ = 0.0;

  // Current brightness step within |user_steps_| set by user, or -1 if
  // |automated_percent_| should be used.
  ssize_t user_step_index_ = -1;

  // Set of percentages that the user can select from for setting the
  // brightness. This is populated from a preference.
  std::vector<double> user_steps_;

  // Min, min visible and max percentages used to calculate scaled percentages
  // in |user_steps_| from raw percentages. This is populated from a preference.
  double min_raw_percent_ = -1;
  double min_visible_raw_percent = -1;
  double max_raw_percent_ = -1;

  // Backlight brightness in the range [0.0, 100.0] to use when the ambient
  // light sensor is controlling the brightness. This is set by
  // |ambient_light_handler_|. If no ambient light sensor is present, it is
  // initialized from kKeyboardBacklightNoAlsBrightnessPref.
  double automated_percent_ = 100.0;

  // Time at which the user's hands stopped hovering over the touchpad. Unset if
  // |hovering_| is true or |supports_hover_| is false.
  base::TimeTicks last_hover_time_;

  // Time at which user activity was last observed.
  base::TimeTicks last_user_activity_time_;

  // Duration the backlight should remain on after hovering stops (on systems
  // that support hover detection) or after user activity (if
  // |turn_on_for_user_activity_| is set).
  base::TimeDelta keep_on_delay_;

  // Like |keep_on_delay_|, but used while fullscreen video is playing.
  base::TimeDelta keep_on_during_video_delay_;

  // Runs UpdateState() |keep_on_delay_| or |keep_on_during_video_delay_| after
  // the user's hands stop hovering over the touchpad (or after user activity is
  // last observed, if |turn_on_for_user_activity_| is true).
  base::OneShotTimer turn_off_timer_;

  // Runs HandleVideoTimeout().
  base::OneShotTimer video_timer_;

  // Counters for stat tracking.
  int num_als_adjustments_ = 0;
  int num_user_adjustments_ = 0;

  // Did |display_backlight_controller_| indicate that the display
  // backlight brightness is currently zero?
  bool display_brightness_is_zero_ = false;

  base::WeakPtrFactory<KeyboardBacklightController> weak_ptr_factory_;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_KEYBOARD_BACKLIGHT_CONTROLLER_H_
