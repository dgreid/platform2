// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Fallback CrosConfig when running on non-unibuild platforms that
// gets info by calling out to external commands (e.g., mosys)

#include "chromeos-config/libcros_config/cros_config_fallback.h"

#include <iostream>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/process/launch.h>
#include <base/strings/string_split.h>
#include <base/system/sys_info.h>
#include <brillo/file_utils.h>
#include "chromeos-config/libcros_config/cros_config_interface.h"

namespace brillo {

namespace {

struct FunctionMapEntry {
  // The path and property to match on
  const char* path;
  const char* property;

  // The function to run to generate the contents for the property.
  base::RepeatingCallback<base::Optional<std::string>()> function;
};

// Helper function to determine if the device has a backlight.
base::Optional<std::string> GetHasBacklight() {
  // Assume the device has a backlight unless it is a CHROMEBOX or CHROMEBIT.
  std::string device_type;
  if (!base::SysInfo::GetLsbReleaseValue("DEVICETYPE", &device_type)) {
    CROS_CONFIG_LOG(ERROR) << "Unable to get DEVICETYPE from /etc/lsb-release";
    return base::nullopt;
  }

  if (device_type == "CHROMEBOX" || device_type == "CHROMEBIT") {
    return "false";
  }

  return "true";
}

// Helper function to run a provided command and return the result on success.
// |command| is just a space-separated argv (not parsed by shell)
base::Optional<std::string> GetOutputForCommand(const std::string& command) {
  std::string result;
  std::vector<std::string> argv = base::SplitString(
      command, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

  if (!base::GetAppOutput(argv, &result)) {
    CROS_CONFIG_LOG(ERROR) << "\"" << command << "\" has non-zero exit code";
    return base::nullopt;
  }

  // Trim off (one) trailing newline from command response.
  if (result.back() == '\n')
    result.pop_back();
  return result;
}

const FunctionMapEntry kFunctionMap[] = {
    {"/firmware", "image-name",
     base::BindRepeating(&GetOutputForCommand, "mosys platform model")},
    {"/", "name",
     base::BindRepeating(&GetOutputForCommand, "mosys platform model")},
    {"/", "brand-code",
     base::BindRepeating(&GetOutputForCommand, "mosys platform brand")},
    {"/identity", "sku-id",
     base::BindRepeating(&GetOutputForCommand, "mosys platform sku")},
    {"/identity", "platform-name",
     base::BindRepeating(&GetOutputForCommand, "mosys platform name")},
    {"/hardware-properties", "psu-type",
     base::BindRepeating(&GetOutputForCommand, "mosys psu type")},
    {"/cros-healthd", "has-backlight", base::BindRepeating(&GetHasBacklight)}};

// Helper function to write a single value to ConfigFS at the given path.
// Returns true if successful and false otherwise.
bool WriteConfigValue(const base::FilePath& output_dir,
                      const std::string& path,
                      const std::string& property,
                      const std::string& value) {
  auto path_dir = output_dir;
  for (const auto& part : base::SplitStringPiece(
           path, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    path_dir = path_dir.Append(part);
  }
  if (!MkdirRecursively(path_dir, 0755).is_valid()) {
    CROS_CONFIG_LOG(ERROR) << "Unable to create directory " << path_dir.value()
                           << ": "
                           << logging::SystemErrorCodeToString(
                                  logging::GetLastSystemErrorCode());
    return false;
  }

  const auto property_file = path_dir.Append(property);
  if (base::WriteFile(property_file, value.data(), value.length()) < 0) {
    CROS_CONFIG_LOG(ERROR) << "Unable to create file " << property_file.value();
    return false;
  }

  return true;
}

}  // namespace

CrosConfigFallback::CrosConfigFallback() {}
CrosConfigFallback::~CrosConfigFallback() {}

bool CrosConfigFallback::WriteConfigFS(const base::FilePath& output_dir) {
  for (auto entry : kFunctionMap) {
    auto value = entry.function.Run();
    // Not all commands may be supported on every board. Don't
    // write the property if the board does not support it.
    if (!value)
      continue;

    if (!WriteConfigValue(output_dir, entry.path, entry.property,
                          value.value()))
      return false;
  }
  return true;
}

}  // namespace brillo
