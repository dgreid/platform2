// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_KEYBOARD_BACKLIGHT_CONTROLLER_H_
#define POWER_MANAGER_POWERD_POLICY_KEYBOARD_BACKLIGHT_CONTROLLER_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include <base/compiler_specific.h>
#include <base/observer_list.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

#include "power_manager/powerd/policy/ambient_light_handler.h"
#include "power_manager/powerd/policy/backlight_controller.h"
#include "power_manager/powerd/policy/backlight_controller_observer.h"

namespace power_manager {

class Clock;
class PrefsInterface;

namespace system {
class AmbientLightSensorInterface;
class BacklightInterface;
}  // namespace system

namespace policy {

class KeyboardBacklightControllerTest;

// Controls the keyboard backlight for devices with such a backlight.
class KeyboardBacklightController
    : public BacklightController,
      public AmbientLightHandler::Delegate,
      public BacklightControllerObserver {
 public:
  // Helper class for tests that need to access internal state.
  class TestApi {
   public:
    explicit TestApi(KeyboardBacklightController* controller);
    ~TestApi();

    Clock* clock() { return controller_->clock_.get(); }

    // Triggers |turn_off_timer_| or |video_timer_| and returns true. Returns
    // false if the timer wasn't running.
    bool TriggerTurnOffTimeout() WARN_UNUSED_RESULT;
    bool TriggerVideoTimeout() WARN_UNUSED_RESULT;

   private:
    KeyboardBacklightController* controller_;  // weak

    DISALLOW_COPY_AND_ASSIGN(TestApi);
  };

  // Backlight brightness percent to use when the screen is dimmed.
  static const double kDimPercent;

  KeyboardBacklightController();
  virtual ~KeyboardBacklightController();

  // Initializes the object. Ownership of passed-in pointers remains with the
  // caller. |sensor| and |display_backlight_controller| may be NULL.
  void Init(system::BacklightInterface* backlight,
            PrefsInterface* prefs,
            system::AmbientLightSensorInterface* sensor,
            BacklightController* display_backlight_controller);

  // Called when a notification about video activity has been received.
  void HandleVideoActivity(bool is_fullscreen);

  // Called when the user's hands start or stop hovering over the touchpad.
  void HandleHoverStateChanged(bool hovering);

  // BacklightController implementation:
  void AddObserver(BacklightControllerObserver* observer) override;
  void RemoveObserver(BacklightControllerObserver* observer) override;
  void HandlePowerSourceChange(PowerSource source) override;
  void HandleDisplayModeChange(DisplayMode mode) override;
  void HandleSessionStateChange(SessionState state) override;
  void HandlePowerButtonPress() override;
  void HandleUserActivity(UserActivityType type) override;
  void HandlePolicyChange(const PowerManagementPolicy& policy) override;
  void HandleChromeStart() override;
  void SetDimmedForInactivity(bool dimmed) override;
  void SetOffForInactivity(bool off) override;
  void SetSuspended(bool suspended) override;
  void SetShuttingDown(bool shutting_down) override;
  void SetDocked(bool docked) override;
  bool GetBrightnessPercent(double* percent) override;
  bool SetUserBrightnessPercent(double percent, TransitionStyle style) override;
  bool IncreaseUserBrightness() override;
  bool DecreaseUserBrightness(bool allow_off) override;
  int GetNumAmbientLightSensorAdjustments() const override;
  int GetNumUserAdjustments() const override;

  // AmbientLightHandler::Delegate implementation:
  void SetBrightnessPercentForAmbientLight(
      double brightness_percent,
      AmbientLightHandler::BrightnessChangeCause cause) override;

  // BacklightControllerObserver implementation:
  void OnBrightnessChanged(
      double brightness_percent,
      BacklightController::BrightnessChangeCause cause,
      BacklightController* source) override;

 private:
  // Handles |video_timer_| firing, indicating that video activity has stopped.
  void HandleVideoTimeout();

  int64_t PercentToLevel(double percent) const;
  double LevelToPercent(int64_t level) const;

  // Returns true if |hovering_| is true, or |last_hover_time_| or
  // |last_user_activity_time_| were less than |keep_on_delay_| ago.
  bool RecentlyHoveringOrUserActive() const;

  // Initializes |user_step_index_| when transitioning from ALS to user control.
  void InitUserStepIndex();

  // Updates the current brightness after assessing the current state (based on
  // |dimmed_for_inactivity_|, |off_for_inactivity_|, etc.). Should be called
  // whenever the state changes. |transition| and |cause| are passed to
  // ApplyBrightnessPercent(). Returns true if the brightness was changed and
  // false otherwise.
  bool UpdateState(TransitionStyle transition, BrightnessChangeCause cause);

  // Sets the backlight's brightness to |percent| over |transition|.
  // Returns true and notifies observers if the brightness was changed.
  bool ApplyBrightnessPercent(double percent,
                              TransitionStyle transition,
                              BrightnessChangeCause cause);

  // Logs that hovering stopped at |last_hover_time_|. Called by
  // |log_hover_off_timer_| after a delay to avoid spamming logs.
  void LogHoverOff();

  mutable std::unique_ptr<Clock> clock_;

  // Backlight used for dimming. Weak pointer.
  system::BacklightInterface* backlight_;

  // Interface for saving preferences. Weak pointer.
  PrefsInterface* prefs_;

  // Controller responsible for the display's brightness. Weak pointer.
  BacklightController* display_backlight_controller_;

  // May be NULL if no ambient light sensor is present.
  std::unique_ptr<AmbientLightHandler> ambient_light_handler_;

  // Observers to notify about changes.
  base::ObserverList<BacklightControllerObserver> observers_;

  // True if the system is capable of detecting whether the user's hands are
  // hovering over the touchpad.
  bool supports_hover_;

  // True if the keyboard should be turned on for user activity but kept off
  // otherwise. This has no effect if |supports_hover_| is set.
  bool turn_on_for_user_activity_;

  SessionState session_state_;

  bool dimmed_for_inactivity_;
  bool off_for_inactivity_;
  bool suspended_;
  bool shutting_down_;
  bool docked_;
  bool hovering_;

  // Is a fullscreen video currently being played?
  bool fullscreen_video_playing_;

  // Maximum brightness level exposed by the backlight driver.
  // 0 is always the minimum.
  int64_t max_level_;

  // Current level that |backlight_| is set to (or possibly in the process
  // of transitioning to).
  int64_t current_level_;

  // Current brightness step within |user_steps_| set by user, or -1 if
  // |automated_percent_| should be used.
  ssize_t user_step_index_;

  // Set of percentages that the user can select from for setting the
  // brightness. This is populated from a preference.
  std::vector<double> user_steps_;

  // Backlight brightness in the range [0.0, 100.0] to use when the ambient
  // light sensor is controlling the brightness. This is set by
  // |ambient_light_handler_|. If no ambient light sensor is present, it is
  // initialized from kKeyboardBacklightNoAlsBrightnessPref.
  double automated_percent_;

  // Time at which the user's hands stopped hovering over the touchpad. Unset if
  // |hovering_| is true or |supports_hover_| is false.
  base::TimeTicks last_hover_time_;

  // Time at which user activity was last observed.
  base::TimeTicks last_user_activity_time_;

  // Duration the backlight should remain on after hovering stops (on systems
  // that support hover detection) or after user activity (if
  // |turn_on_for_user_activity_| is set).
  base::TimeDelta keep_on_delay_;

  // Runs UpdateState() |keep_on_delay_| after the user's hands stop hovering
  // over the touchpad (or after user activity is last observed, if
  // |turn_on_for_user_activity_| is true).
  base::OneShotTimer turn_off_timer_;

  // Runs HandleVideoTimeout().
  base::OneShotTimer video_timer_;

  // Runs LogHoverOff().
  base::OneShotTimer log_hover_off_timer_;

  // Counters for stat tracking.
  int num_als_adjustments_;
  int num_user_adjustments_;

  // Did |display_backlight_controller_| indicate that the display
  // backlight brightness is currently zero?
  bool display_brightness_is_zero_;

  DISALLOW_COPY_AND_ASSIGN(KeyboardBacklightController);
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_KEYBOARD_BACKLIGHT_CONTROLLER_H_
