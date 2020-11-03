// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor.h"

#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

#include <base/bind.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "power_manager/common/util.h"

namespace power_manager {
namespace system {

namespace {

// Default path examined for backlight device directories.
const char kDefaultDeviceListPath[] = "/sys/bus/iio/devices";

// Default interval for polling the ambient light sensor.
const int kDefaultPollIntervalMs = 1000;

enum class ChannelType {
  X,
  Y,
  Z,
};

SensorLocation StringToSensorLocation(const std::string& location) {
  if (location == "base")
    return SensorLocation::BASE;
  if (location == "lid")
    return SensorLocation::LID;
  return SensorLocation::UNKNOWN;
}

std::string SensorLocationToString(SensorLocation location) {
  switch (location) {
    case SensorLocation::UNKNOWN:
      return "unknown";
    case SensorLocation::BASE:
      return "base";
    case SensorLocation::LID:
      return "lid";
  }
}

bool ParseLuxData(const std::string& data, int* value) {
  DCHECK(value);
  std::string trimmed_data;
  base::TrimWhitespaceASCII(data, base::TRIM_ALL, &trimmed_data);
  if (!base::StringToInt(trimmed_data, value)) {
    LOG(ERROR) << "Could not read lux value from ALS file contents: ["
               << trimmed_data << "]";
    return false;
  }
  VLOG(1) << "Read lux value " << value;
  return true;
}

}  // namespace

const struct ColorChannelInfo {
  ChannelType type;
  const char* rgb_name;
  const char* xyz_name;
  bool is_lux_channel;
} kColorChannelConfig[] = {
    {ChannelType::X, "red", "x", false},
    {ChannelType::Y, "green", "y", true},
    {ChannelType::Z, "blue", "z", false},
};

const int AmbientLightSensor::kNumInitAttemptsBeforeLogging = 5;
const int AmbientLightSensor::kNumInitAttemptsBeforeGivingUp = 20;

AmbientLightSensor::AmbientLightSensor()
    : AmbientLightSensor(SensorLocation::UNKNOWN, false) {}

AmbientLightSensor::AmbientLightSensor(SensorLocation expected_sensor_location)
    : AmbientLightSensor(expected_sensor_location, false) {}

AmbientLightSensor::AmbientLightSensor(bool enable_color_support)
    : AmbientLightSensor(SensorLocation::UNKNOWN, enable_color_support) {}

AmbientLightSensor::AmbientLightSensor(SensorLocation expected_sensor_location,
                                       bool enable_color_support)
    : device_list_path_(kDefaultDeviceListPath),
      poll_interval_ms_(kDefaultPollIntervalMs),
      enable_color_support_(enable_color_support),
      lux_value_(-1),
      color_temperature_(-1),
      num_init_attempts_(0),
      expected_sensor_location_(expected_sensor_location) {}

AmbientLightSensor::~AmbientLightSensor() {}

void AmbientLightSensor::Init(bool read_immediately) {
  if (read_immediately)
    ReadAls();
  StartTimer();
}

bool AmbientLightSensor::TriggerPollTimerForTesting() {
  if (!poll_timer_.IsRunning())
    return false;

  ReadAls();
  return true;
}

void AmbientLightSensor::AddObserver(AmbientLightObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void AmbientLightSensor::RemoveObserver(AmbientLightObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

int AmbientLightSensor::GetAmbientLightLux() {
  return lux_value_;
}

bool AmbientLightSensor::IsColorSensor() const {
  return !color_als_files_.empty();
}

int AmbientLightSensor::GetColorTemperature() {
  return color_temperature_;
}

void AmbientLightSensor::StartTimer() {
  poll_timer_.Start(FROM_HERE,
                    base::TimeDelta::FromMilliseconds(poll_interval_ms_), this,
                    &AmbientLightSensor::ReadAls);
}

void AmbientLightSensor::ReadAls() {
  // We really want to read the ambient light level.
  // Complete the deferred lux file open if necessary.
  if (!als_file_.HasOpenedFile() && !InitAlsFile()) {
    if (num_init_attempts_ >= kNumInitAttemptsBeforeGivingUp) {
      LOG(ERROR) << "Giving up on reading from sensor";
      poll_timer_.Stop();
    }
    return;
  }

  // The timer will be restarted after the read finishes.
  poll_timer_.Stop();
  if (!IsColorSensor()) {
    als_file_.StartRead(
        base::Bind(&AmbientLightSensor::ReadCallback, base::Unretained(this)),
        base::Bind(&AmbientLightSensor::ErrorCallback, base::Unretained(this)));
    return;
  }

  color_readings_.clear();
  for (const ColorChannelInfo& channel : kColorChannelConfig) {
    color_als_files_[&channel].StartRead(
        base::Bind(&AmbientLightSensor::ReadColorChannelCallback,
                   base::Unretained(this), &channel),
        base::Bind(&AmbientLightSensor::ErrorColorChannelCallback,
                   base::Unretained(this), &channel));
  }
}

void AmbientLightSensor::ReadCallback(const std::string& data) {
  int value = 0;
  if (ParseLuxData(data, &value)) {
    lux_value_ = value;
    for (AmbientLightObserver& observer : observers_)
      observer.OnAmbientLightUpdated(this);
  }
  StartTimer();
}

void AmbientLightSensor::ErrorCallback() {
  LOG(ERROR) << "Error reading ALS file";
  StartTimer();
}

void AmbientLightSensor::ReadColorChannelCallback(
    const ColorChannelInfo* channel, const std::string& data) {
  int value = -1;
  ParseLuxData(data, &value);
  color_readings_[channel] = value;
  CollectChannelReadings();
}

void AmbientLightSensor::ErrorColorChannelCallback(
    const ColorChannelInfo* channel) {
  LOG(ERROR) << "Error reading ALS file for " << channel->xyz_name << "channel";
  color_readings_[channel] = -1;
  CollectChannelReadings();
}

void AmbientLightSensor::CollectChannelReadings() {
  if (color_readings_.size() != base::size(kColorChannelConfig))
    return;

  // We should notify observers if there is either a change in lux or a change
  // in color temperature. This means that we can always notify when we have the
  // Y value but otherwise we need all three.
  std::map<ChannelType, int> valid_readings;
  double scale_factor = 0;
  for (const auto& reading : color_readings_) {
    // -1 marks an invalid reading.
    if (reading.second == -1)
      continue;
    if (reading.first->is_lux_channel)
      lux_value_ = reading.second;
    valid_readings[reading.first->type] = reading.second;
    scale_factor += reading.second;
  }

  if (valid_readings.count(ChannelType::Y) == 0) {
    StartTimer();
    return;
  }

  if (valid_readings.size() != color_readings_.size() || scale_factor == 0) {
    // We either don't have all of the channels or there is no light in the
    // sensor and therefore no color temperature, but we can still notify
    // for lux.
    color_temperature_ = -1;
  } else {
    double scaled_x = valid_readings[ChannelType::X] / scale_factor;
    double scaled_y = valid_readings[ChannelType::Y] / scale_factor;
    // Avoid weird behavior around the function's pole.
    if (scaled_y < 0.186) {
      color_temperature_ = -1;
    } else {
      double n = (scaled_x - 0.3320) / (0.1858 - scaled_y);
      color_temperature_ = static_cast<int>(449 * n * n * n + 3525 * n * n +
                                            6823.3 * n + 5520.33);
    }
  }

  for (AmbientLightObserver& observer : observers_)
    observer.OnAmbientLightUpdated(this);
  StartTimer();
}

void AmbientLightSensor::InitColorAlsFiles(const base::FilePath& device_dir) {
  color_als_files_.clear();
  std::map<const ColorChannelInfo*, AsyncFileReader> channel_map;

  for (const ColorChannelInfo& channel : kColorChannelConfig) {
    base::FilePath channel_path(device_dir.Append(
        base::StringPrintf("in_illuminance_%s_raw", channel.rgb_name)));
    if (!base::PathExists(channel_path))
      return;
    if (!channel_map[&channel].Init(channel_path))
      return;
    VLOG(2) << "Found " << channel.xyz_name << " light intensity file at "
            << channel_path.value();
  }

  color_als_files_ = std::move(channel_map);
  LOG(INFO) << "ALS at path " << device_dir.value() << " has color support";
}

bool AmbientLightSensor::InitAlsFile() {
  CHECK(!als_file_.HasOpenedFile());

  // Search the iio/devices directory for a subdirectory (eg "device0" or
  // "iio:device0") that contains the "[in_]illuminance[0]_{input|raw}" file.
  base::FileEnumerator dir_enumerator(device_list_path_, false,
                                      base::FileEnumerator::DIRECTORIES);
  const char* input_names[] = {
      "in_illuminance0_input", "in_illuminance_input", "in_illuminance0_raw",
      "in_illuminance_raw",    "illuminance0_input",
  };

  num_init_attempts_++;
  for (base::FilePath check_path = dir_enumerator.Next(); !check_path.empty();
       check_path = dir_enumerator.Next()) {
    if (expected_sensor_location_ != SensorLocation::UNKNOWN) {
      base::FilePath loc_path = check_path.Append("location");
      std::string location;
      if (!base::ReadFileToString(loc_path, &location)) {
        continue;
      }
      base::TrimWhitespaceASCII(location, base::TRIM_ALL, &location);
      SensorLocation als_loc = StringToSensorLocation(location);
      if (als_loc != expected_sensor_location_) {
        continue;
      }
    }
    for (unsigned int i = 0; i < base::size(input_names); i++) {
      base::FilePath als_path = check_path.Append(input_names[i]);
      if (!base::PathExists(als_path))
        continue;
      if (!als_file_.Init(als_path))
        continue;
      if (enable_color_support_)
        InitColorAlsFiles(check_path);
      LOG(INFO) << "Using lux file " << als_path.value() << " for "
                << SensorLocationToString(expected_sensor_location_) << " ALS";
      return true;
    }
  }

  // If the illuminance file is not immediately found, issue a deferral
  // message and try again later.
  if (num_init_attempts_ > kNumInitAttemptsBeforeLogging)
    PLOG(ERROR) << "lux file initialization failed";
  return false;
}

}  // namespace system
}  // namespace power_manager
