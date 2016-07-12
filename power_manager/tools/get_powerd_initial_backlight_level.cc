// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This program prints the default backlight level that would be used by powerd,
// taking the current prefs and power source and the actual backlight range into
// account.

#include <cstdio>
#include <vector>

#include <base/at_exit.h>
#include <base/files/file_path.h>
#include <base/format_macros.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <brillo/flag_helper.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/policy/internal_backlight_controller.h"
#include "power_manager/powerd/policy/keyboard_backlight_controller.h"
#include "power_manager/powerd/system/ambient_light_sensor_stub.h"
#include "power_manager/powerd/system/backlight_stub.h"
#include "power_manager/powerd/system/display/display_power_setter_stub.h"
#include "power_manager/powerd/system/internal_backlight.h"
#include "power_manager/powerd/system/power_supply.h"
#include "power_manager/powerd/system/udev_stub.h"

int main(int argc, char* argv[]) {
  DEFINE_bool(keyboard, false, "Display initial keyboard (rather than panel) "
              "backlight brightness. The level corresponds to that used when "
              "hovering is detected and ambient light is at its lowest level "
              "(if applicable).");
  brillo::FlagHelper::Init(argc, argv, "Print initial backlight levels.");

  base::AtExitManager at_exit_manager;
  base::MessageLoopForIO message_loop;

  // Get the max and current brightness from the real backlight and use them to
  // initialize a stub backlight (so that the controller can do its thing
  // without changing the actual brightness level).
  power_manager::system::InternalBacklight real_backlight;
  CHECK(real_backlight.Init(
      base::FilePath(FLAGS_keyboard ? power_manager::kKeyboardBacklightPath
                                    : power_manager::kInternalBacklightPath),
      FLAGS_keyboard ? power_manager::kKeyboardBacklightPattern
                     : power_manager::kInternalBacklightPattern));
  power_manager::system::BacklightStub stub_backlight(
      real_backlight.GetMaxBrightnessLevel(),
      real_backlight.GetCurrentBrightnessLevel());

  power_manager::Prefs prefs;
  CHECK(prefs.Init(power_manager::util::GetPrefPaths(
      base::FilePath(power_manager::kReadWritePrefsDir),
      base::FilePath(power_manager::kReadOnlyPrefsDir))));

  std::unique_ptr<power_manager::system::AmbientLightSensorStub> light_sensor;
  bool has_als = false;
  if (prefs.GetBool(power_manager::kHasAmbientLightSensorPref, &has_als) &&
      has_als)
    light_sensor.reset(new power_manager::system::AmbientLightSensorStub(0));

  std::unique_ptr<power_manager::policy::BacklightController>
      backlight_controller;
  std::unique_ptr<power_manager::system::DisplayPowerSetterStub>
      display_power_setter;

  if (FLAGS_keyboard) {
    auto controller = new power_manager::policy::KeyboardBacklightController;
    controller->Init(&stub_backlight, &prefs, light_sensor.get(), nullptr,
                     power_manager::TABLET_MODE_OFF);
    controller->HandleHoverStateChanged(true /* hovering */);
    backlight_controller.reset(controller);
  } else {
    display_power_setter.reset(
        new power_manager::system::DisplayPowerSetterStub);
    auto controller = new power_manager::policy::InternalBacklightController;
    controller->Init(&stub_backlight, &prefs, light_sensor.get(),
                     display_power_setter.get());
    backlight_controller.reset(controller);
  }

  // Get the power source.
  power_manager::system::UdevStub udev;
  power_manager::system::PowerSupply power_supply;
  power_supply.Init(
      base::FilePath(power_manager::kPowerStatusPath), &prefs, &udev,
      false /* log_shutdown_thresholds */);
  CHECK(power_supply.RefreshImmediately());
  const power_manager::PowerSource power_source =
      power_supply.GetPowerStatus().line_power_on ?
      power_manager::POWER_AC : power_manager::POWER_BATTERY;

  // Mimic powerd startup and grab the brightness level that's used.
  if (light_sensor.get())
    light_sensor->NotifyObservers();
  backlight_controller->HandlePowerSourceChange(power_source);
  printf("%" PRId64 "\n", stub_backlight.current_level());
  return 0;
}
