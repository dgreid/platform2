// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/format_macros.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>

#include "power_manager/common/battery_percentage_converter.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/policy/backlight_controller.h"
#include "power_manager/powerd/policy/internal_backlight_controller.h"
#include "power_manager/powerd/policy/keyboard_backlight_controller.h"
#include "power_manager/powerd/system/ambient_light_sensor_manager.h"
#include "power_manager/powerd/system/ambient_light_sensor_stub.h"
#include "power_manager/powerd/system/backlight_stub.h"
#include "power_manager/powerd/system/dbus_wrapper_stub.h"
#include "power_manager/powerd/system/display/display_power_setter_stub.h"
#include "power_manager/powerd/system/internal_backlight.h"
#include "power_manager/powerd/system/power_supply.h"
#include "power_manager/powerd/system/udev_stub.h"

using power_manager::BatteryPercentageConverter;
using power_manager::LidState;
using power_manager::PowerSource;
using power_manager::Prefs;
using power_manager::TabletMode;
using power_manager::policy::BacklightController;
using power_manager::policy::InternalBacklightController;
using power_manager::policy::KeyboardBacklightController;
using power_manager::system::AmbientLightSensorInterface;
using power_manager::system::AmbientLightSensorManager;
using power_manager::system::AmbientLightSensorStub;
using power_manager::system::BacklightStub;
using power_manager::system::DBusWrapperStub;
using power_manager::system::DisplayPowerSetterStub;
using power_manager::system::InternalBacklight;
using power_manager::system::PowerSupply;
using power_manager::system::UdevStub;
using power_manager::util::ClampPercent;

namespace {

// Abort if an ambient light sample hasn't been updated after this many
// milliseconds.
constexpr base::TimeDelta kUpdateTimeout =
    base::TimeDelta::FromMilliseconds(5000);

// Prints |message| to stderr with a trailing newline and exits.
void Abort(const std::string& message) {
  fprintf(stderr, "%s\n", message.c_str());
  exit(1);
}

// Converter instantiates several internal powerd classes to perform conversions
// between hardware backlight levels, nonlinear percents that powerd uses (which
// are dependent on the powerd prefs that have been set for the device), and
// linear percents. It also supports getting the initial level that powerd would
// choose.
class Converter {
 public:
  Converter(int64_t current_level,
            int64_t max_level,
            power_manager::system::BacklightInterface::BrightnessScale scale,
            int64_t lux,
            bool keyboard,
            bool force_battery)
      : backlight_(max_level, current_level, scale) {
    CHECK(prefs_.Init(Prefs::GetDefaultStore(), Prefs::GetDefaultSources()));

    bool has_als = false;
    if (prefs_.GetBool(power_manager::kHasAmbientLightSensorPref, &has_als) &&
        has_als) {
      light_sensor_ = std::make_unique<AmbientLightSensorStub>(lux);
    }

    if (keyboard) {
      auto controller = std::make_unique<KeyboardBacklightController>();
      controller->Init(&backlight_, &prefs_, light_sensor_.get(),
                       &dbus_wrapper_,
                       nullptr /* display_backlight_controller */,
                       LidState::NOT_PRESENT, TabletMode::UNSUPPORTED);
      controller->HandleHoverStateChange(true /* hovering */);
      controller_ = std::move(controller);
    } else {
      auto controller = std::make_unique<InternalBacklightController>();
      controller->Init(&backlight_, &prefs_, light_sensor_.get(),
                       &display_power_setter_, &dbus_wrapper_,
                       LidState::NOT_PRESENT);
      controller_ = std::move(controller);
    }

    if (light_sensor_.get())
      light_sensor_->NotifyObservers();

    PowerSource power_source = PowerSource::BATTERY;
    if (!force_battery) {
      UdevStub udev;

      auto battery_percentage_converter =
          BatteryPercentageConverter::CreateFromPrefs(&prefs_);

      PowerSupply power_supply;
      power_supply.Init(base::FilePath(power_manager::kPowerStatusPath),
                        &prefs_, &udev, &dbus_wrapper_,
                        battery_percentage_converter.get());
      if (!power_supply.RefreshImmediately()) {
        LOG(ERROR) << "Failed to read power supply information; using battery";
      } else {
        power_source = power_supply.GetPowerStatus().line_power_on
                           ? PowerSource::AC
                           : PowerSource::BATTERY;
      }
    }
    controller_->HandlePowerSourceChange(power_source);
  }
  Converter(const Converter&) = delete;
  Converter& operator=(const Converter&) = delete;

  // Converts a brightness level to a nonlinear percent in [0.0, 100.0].
  double LevelToNonlinearPercent(int64_t level) {
    return controller_->LevelToPercent(level);
  }

  // Converts a nonlinear percent in [0.0, 100.0] to a brightness level.
  int64_t NonlinearPercentToLevel(double percent) {
    return controller_->PercentToLevel(percent);
  }

  // Converts a brightness level to a linear percent in [0.0, 100.0].
  double LevelToLinearPercent(int64_t level) {
    return level * 100.0 / backlight_.GetMaxBrightnessLevel();
  }

  // Converts a linear percent in [0.0, 100.0] to a brightness level.
  int64_t LinearPercentToLevel(double percent) {
    return static_cast<int64_t>(roundl(
        ClampPercent(percent) * backlight_.GetMaxBrightnessLevel() / 100.0));
  }

  // Returns the initial brightness level requested by |controller_|.
  int64_t GetInitialLevel() { return backlight_.GetCurrentBrightnessLevel(); }

 private:
  // A stub is used so |controller_| won't change the actual brightness.
  BacklightStub backlight_;
  Prefs prefs_;
  std::unique_ptr<AmbientLightSensorStub> light_sensor_;
  DisplayPowerSetterStub display_power_setter_;
  DBusWrapperStub dbus_wrapper_;
  std::unique_ptr<BacklightController> controller_;
};

class ObserverImpl : public power_manager::system::AmbientLightObserver {
 public:
  ObserverImpl(const ObserverImpl&) = delete;
  ObserverImpl& operator=(const ObserverImpl&) = delete;

  ObserverImpl() { ResetRunner(); }
  ~ObserverImpl() override {}

  bool RunUntilAmbientLightUpdated() {
    CHECK(runner_.get());

    runner_->Run();

    bool timed_out = timed_out_;
    ResetRunner();
    return !timed_out;
  }

  // AmbientLightObserver implementation:
  void OnAmbientLightUpdated(AmbientLightSensorInterface* sensor) override {
    CHECK(runner_.get());

    timeout_timer_.Stop();
    closure_.Run();
  }

 private:
  void ResetRunner() {
    runner_.reset(new base::RunLoop());
    closure_ = runner_->QuitClosure();
    timed_out_ = false;

    timeout_timer_.Start(FROM_HERE, kUpdateTimeout, this,
                         &ObserverImpl::OnTimeout);
  }

  void OnTimeout() {
    CHECK(runner_.get());
    timed_out_ = true;
    closure_.Run();
  }

  std::unique_ptr<base::RunLoop> runner_;
  base::RepeatingClosure closure_;
  bool timed_out_ = false;

  base::OneShotTimer timeout_timer_;
};

// Gets the lux of ambient light sensor illuminance that powerd would monitor
// and a trailing newline to stdout. Prints an error and aborts with status code
// 1 if the ALS has been disabled or no lux value was available before timed
// out.
void GetAmbientLightLux(bool keyboard) {
  Prefs prefs;
  CHECK(prefs.Init(Prefs::GetDefaultStore(), Prefs::GetDefaultSources()));
  int64_t num_als = 0;
  if (!prefs.GetInt64(power_manager::kHasAmbientLightSensorPref, &num_als) ||
      !num_als) {
    Abort("Ambient light sensor not enabled");
  }

  AmbientLightSensorManager als_manager;
  als_manager.Init(&prefs);
  als_manager.Run(true /* read_immediately */);

  AmbientLightSensorInterface* sensor;
  sensor = keyboard ? als_manager.GetSensorForKeyboardBacklight()
                    : als_manager.GetSensorForInternalBacklight();
  if (!sensor)
    Abort("Ambient light sensor not found");

  int lux = sensor->GetAmbientLightLux();
  if (lux < 0) {
    // Wait for a sample updated or timeout.

    ObserverImpl observer;
    sensor->AddObserver(&observer);
    if (!observer.RunUntilAmbientLightUpdated())
      Abort("Timed out before an ambient light sample updated");

    lux = sensor->GetAmbientLightLux();
  }

  CHECK_GE(lux, 0);
  printf("%i\n", lux);
}

// Prints the path to the ambient light sensor illuminance file that powerd
// would monitor and a trailing newline to stdout. Prints an error and aborts
// with status code 1 if the ALS has been disabled or no path was found.
void PrintAmbientLightPath(bool keyboard) {
  Prefs prefs;
  CHECK(prefs.Init(Prefs::GetDefaultStore(), Prefs::GetDefaultSources()));
  int64_t num_als = 0;
  if (!prefs.GetInt64(power_manager::kHasAmbientLightSensorPref, &num_als) ||
      !num_als) {
    Abort("Ambient light sensor not enabled");
  }

  AmbientLightSensorManager als_manager;
  als_manager.Init(&prefs);
  als_manager.Run(true /* read_immediately */);

  AmbientLightSensorInterface* sensor;
  sensor = keyboard ? als_manager.GetSensorForKeyboardBacklight()
                    : als_manager.GetSensorForInternalBacklight();
  if (!sensor)
    Abort("Ambient light sensor not found");

  base::FilePath path = sensor->GetIlluminancePath();
  if (path.empty())
    Abort("Ambient light sensor illuminance file not found");

  printf("%s\n", path.value().c_str());
}

}  // namespace

int main(int argc, char* argv[]) {
  // Flags that print the brightness.
  DEFINE_bool(get_brightness, false, "Print current brightness level");
  DEFINE_bool(get_brightness_percent, false,
              "Print current brightness as linear percent");
  DEFINE_bool(get_max_brightness, false, "Print max brightness level");
  DEFINE_bool(get_initial_brightness, false,
              "Print brightness level used "
              "by powerd at boot");

  // Flags that convert between units.
  DEFINE_double(nonlinear_to_level, -1.0,
                "Convert supplied nonlinear brightness percent to level");
  DEFINE_int64(level_to_nonlinear, -1,
               "Convert supplied brightness level to nonlinear percent");
  DEFINE_double(linear_to_level, -1.0,
                "Convert supplied linear brightness percent to level");
  DEFINE_int64(level_to_linear, -1,
               "Convert supplied brightness level to linear percent");
  DEFINE_double(linear_to_nonlinear, -1.0,
                "Convert supplied linear brightness percent to nonlinear");
  DEFINE_double(nonlinear_to_linear, -1.0,
                "Convert supplied nonlinear brightness percent to linear");

  // Flags that set the brightness.
  DEFINE_int64(set_brightness, -1, "Set brightness level");
  DEFINE_double(set_brightness_percent, -1.0,
                "Set brightness as "
                "linearly-calculated percent in [0.0, 100.0]");

  // Other flags.
  DEFINE_bool(get_ambient_light_lux, false, "Get ambient light sensor reading");
  DEFINE_bool(get_ambient_light_path, false,
              "Print path to ambient light sensor illuminance file");
  DEFINE_bool(force_battery, false,
              "Act as if on battery even if currently on AC (for "
              "-get_initial_brightness)");
  DEFINE_bool(keyboard, false, "Use keyboard (rather than panel) backlight");
  DEFINE_int32(lux, 0,
               "Ambient light sensor reading (for -get_initial_brightness)");

  brillo::FlagHelper::Init(
      argc, argv,
      "Print or set the internal panel or keyboard backlight's brightness.");

  base::AtExitManager at_exit_manager;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  logging::SetMinLogLevel(logging::LOG_WARNING);

  if (FLAGS_get_brightness + FLAGS_get_max_brightness +
          FLAGS_get_initial_brightness + FLAGS_get_brightness_percent +
          FLAGS_get_ambient_light_lux + FLAGS_get_ambient_light_path +
          (FLAGS_nonlinear_to_level >= 0.0) + (FLAGS_level_to_nonlinear >= 0) +
          (FLAGS_linear_to_level >= 0.0) + (FLAGS_level_to_linear >= 0) +
          (FLAGS_linear_to_nonlinear >= 0.0) +
          (FLAGS_nonlinear_to_linear >= 0.0) >
      1) {
    Abort("At most one flag that prints a level or percent may be passed.");
  }
  if (FLAGS_set_brightness >= 0 && FLAGS_set_brightness_percent >= 0.0)
    Abort("At most one of -set_brightness* may be passed.");

  if (FLAGS_get_ambient_light_lux) {
    GetAmbientLightLux(FLAGS_keyboard);
    return 0;
  }

  if (FLAGS_get_ambient_light_path) {
    PrintAmbientLightPath(FLAGS_keyboard);
    return 0;
  }

  InternalBacklight backlight;
  base::FilePath path(FLAGS_keyboard ? power_manager::kKeyboardBacklightPath
                                     : power_manager::kInternalBacklightPath);
  std::string pattern = FLAGS_keyboard
                            ? power_manager::kKeyboardBacklightPattern
                            : power_manager::kInternalBacklightPattern;
  if (!backlight.Init(path, pattern))
    Abort("No backlight in " + path.value() + " matched by " + pattern + ".");

  const int64_t current_level = backlight.GetCurrentBrightnessLevel();
  Converter converter(current_level, backlight.GetMaxBrightnessLevel(),
                      backlight.GetBrightnessScale(), FLAGS_lux, FLAGS_keyboard,
                      FLAGS_force_battery);

  // Print brightness.
  if (FLAGS_get_brightness)
    printf("%" PRIi64 "\n", current_level);
  if (FLAGS_get_max_brightness)
    printf("%" PRIi64 "\n", backlight.GetMaxBrightnessLevel());
  if (FLAGS_get_brightness_percent)
    printf("%f\n", converter.LevelToLinearPercent(current_level));
  if (FLAGS_get_initial_brightness)
    printf("%" PRIi64 "\n", converter.GetInitialLevel());

  // Convert between units.
  if (FLAGS_nonlinear_to_level >= 0.0) {
    printf("%" PRIi64 "\n",
           converter.NonlinearPercentToLevel(FLAGS_nonlinear_to_level));
  }
  if (FLAGS_level_to_nonlinear >= 0) {
    printf("%f\n", converter.LevelToNonlinearPercent(FLAGS_level_to_nonlinear));
  }
  if (FLAGS_linear_to_level >= 0.0) {
    printf("%" PRIi64 "\n",
           converter.LinearPercentToLevel(FLAGS_linear_to_level));
  }
  if (FLAGS_level_to_linear >= 0) {
    printf("%f\n", converter.LevelToLinearPercent(FLAGS_level_to_linear));
  }
  if (FLAGS_linear_to_nonlinear >= 0.0) {
    printf("%f\n",
           converter.LevelToNonlinearPercent(
               converter.LinearPercentToLevel(FLAGS_linear_to_nonlinear)));
  }
  if (FLAGS_nonlinear_to_linear >= 0.0) {
    printf("%f\n",
           converter.LevelToLinearPercent(
               converter.NonlinearPercentToLevel(FLAGS_nonlinear_to_linear)));
  }

  // Change the brightness.
  if (FLAGS_set_brightness >= 0) {
    CHECK(
        backlight.SetBrightnessLevel(FLAGS_set_brightness, base::TimeDelta()));
  }
  if (FLAGS_set_brightness_percent >= 0.0) {
    CHECK(backlight.SetBrightnessLevel(
        converter.LinearPercentToLevel(FLAGS_set_brightness_percent),
        base::TimeDelta()));
  }

  return 0;
}
