// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thd/source/ectool_temps_source.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

namespace thd {

namespace {

// Command line tool to interact with the EC. The 1st argument.
const char kEctoolCmd[] = "ectool";

// Option to read temperature values using ectool. The 2nd argument.
const char kTempsArg[] = "temps";

// Ectool temps x outputs this prefix followed by the actual temperature.
// This consant is used to parse out the number properly.
const char kOutputPrefix[] = "Reading temperature...";

// Output temperature values are in Kelvin, so they should always be
// 3 characters long.
const int kOutputTempLength = 3;

int64_t KelvinToCelcius(int64_t ktemp) {
  return ktemp - 273;
}

}  // namespace

EctoolTempsSource::EctoolTempsSource(int sensor_id)
    : cmd_args_({kEctoolCmd, kTempsArg, std::to_string(sensor_id)}) {}

EctoolTempsSource::~EctoolTempsSource() {}

bool EctoolTempsSource::ReadValue(int64_t* value) {
  std::string cmd_output;
  if (!base::GetAppOutput(cmd_args_, &cmd_output)) {
    std::string cmd_line = base::JoinString(cmd_args_, " ");
    PLOG(ERROR) << "Running the command \"" << cmd_line << "\" failed.";
    return false;
  }
  if (!base::StartsWith(cmd_output, kOutputPrefix,
                        base::CompareCase::SENSITIVE)) {
    LOG(ERROR)
        << "Ectool temps source was unable to parse the command line output."
        << "Output: " << cmd_output << ".";
    return false;
  }
  std::string value_str =
      cmd_output.substr(strlen(kOutputPrefix), kOutputTempLength);
  int64_t temperature_in_kelvin = -1;
  if (!base::StringToInt64(value_str, &temperature_in_kelvin)) {
    LOG(ERROR) << "Unable to parse an integer from the output \"" << value_str
               << "\".";
    return false;
  }
  *value = KelvinToCelcius(temperature_in_kelvin);
  return true;
}

}  // namespace thd
