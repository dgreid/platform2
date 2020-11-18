// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_H_

#include <list>
#include <map>
#include <string>

#include <base/compiler_specific.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/observer_list.h>
#include <base/timer/timer.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/ambient_light_observer.h"
#include "power_manager/powerd/system/ambient_light_sensor_interface.h"
#include "power_manager/powerd/system/async_file_reader.h"

namespace power_manager {
namespace system {

struct ColorChannelInfo;

enum class SensorLocation {
  UNKNOWN,
  BASE,
  LID,
};

class AmbientLightSensor : public AmbientLightSensorInterface {
 public:
  // Number of failed init attempts before AmbientLightSensor will start logging
  // warnings or stop trying entirely.
  static const int kNumInitAttemptsBeforeLogging;
  static const int kNumInitAttemptsBeforeGivingUp;

  AmbientLightSensor();
  explicit AmbientLightSensor(SensorLocation expected_sensor_location);
  explicit AmbientLightSensor(bool allow_ambient_eq);
  AmbientLightSensor(SensorLocation expected_sensor_location,
                     bool allow_ambient_eq);
  AmbientLightSensor(const AmbientLightSensor&) = delete;
  AmbientLightSensor& operator=(const AmbientLightSensor&) = delete;

  ~AmbientLightSensor() override;

  void set_device_list_path_for_testing(const base::FilePath& path) {
    device_list_path_ = path;
  }
  void set_poll_interval_ms_for_testing(int interval_ms) {
    poll_interval_ms_ = interval_ms;
  }

  // Starts polling. If |read_immediately| is true, ReadAls() will also
  // immediately be called synchronously. This is separate from c'tor so that
  // tests can call set_*_for_testing() first.
  void Init(bool read_immediately);

  // If |poll_timer_| is running, calls ReadAls() and returns true. Otherwise,
  // returns false.
  bool TriggerPollTimerForTesting();

  // AmbientLightSensorInterface implementation:
  void AddObserver(AmbientLightObserver* observer) override;
  void RemoveObserver(AmbientLightObserver* observer) override;
  bool IsColorSensor() const override;
  int GetAmbientLightLux() override;
  int GetColorTemperature() override;
  base::FilePath GetIlluminancePath() const override;

 private:
  // Starts |poll_timer_|.
  void StartTimer();

  // Handler for a periodic event that reads the ambient light sensor.
  void ReadAls();

  // Asynchronous I/O success and error handlers, respectively.
  void ReadCallback(const std::string& data);
  void ErrorCallback();

  // Asynchronous I/O handlers for color ALS and other utility methods used to
  // put everything together.
  void ReadColorChannelCallback(const ColorChannelInfo* channel,
                                const std::string& data);
  void ErrorColorChannelCallback(const ColorChannelInfo* channel);
  void CollectChannelReadings();

  // Initializes |als_file_| and optionally color ALS support if it exists.
  // Returns true if at least lux information is available for use.
  bool InitAlsFile();

  // Initializes |color_als_files_|.
  void InitColorAlsFiles(const base::FilePath& device_dir);

  // Path containing backlight devices.  Typically under /sys, but can be
  // overridden by tests.
  base::FilePath device_list_path_;

  // Runs ReadAls().
  base::RepeatingTimer poll_timer_;

  // Time between polls of the sensor file, in milliseconds.
  int poll_interval_ms_;

  // List of backlight controllers that are currently interested in updates from
  // this sensor.
  base::ObserverList<AmbientLightObserver> observers_;

  // Boolean to indicate if color support should be enabled on this ambient
  // light sensor. Color support should only be enabled if sensor is properly
  // calibrated. Only search for color support if true.
  bool enable_color_support_;

  // Lux value read by the class. If this read did not succeed or no read has
  // occured yet this variable is set to -1.
  int lux_value_;

  // Color temperature read by the class. If this read did not succeed or no
  // read has occurred yet this variable is set to -1.
  int color_temperature_;

  // Number of attempts to find and open the lux file made so far.
  int num_init_attempts_;

  // This is the ambient light sensor asynchronous file I/O object.
  AsyncFileReader als_file_;

  // Async file I/O objects for color ALS channels if it is supported.
  // If this map is empty, then there is no color support.
  std::map<const ColorChannelInfo*, AsyncFileReader> color_als_files_;

  // Values read by the |color_als_files_| readers. We need to gather data
  // from each channel before computing a color temperature.
  std::map<const ColorChannelInfo*, int> color_readings_;

  // Location on the device (e.g. lid, base) where this sensor reports itself
  // to be. If set to unknown, powerd looks for a sensor at any location.
  SensorLocation expected_sensor_location_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_H_
