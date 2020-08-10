// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Fallback CrosConfig when running on non-unibuild platforms that
// gets info by calling out to external commands (e.g., mosys)

#include "chromeos-config/libcros_config/cros_config_fallback.h"

#include <iostream>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/process/launch.h>
#include <base/strings/string_split.h>
#include <brillo/file_utils.h>
#include "chromeos-config/libcros_config/cros_config_interface.h"

namespace {

struct CommandMapEntry {
  // The path and property to match on
  const char* path;
  const char* property;

  // The corresponding command to run, which is just a space-separated
  // argv (not parsed by shell)
  const char* command;
};

constexpr CommandMapEntry kCommandMap[] = {
  {"/firmware", "image-name", "mosys platform model"},
  {"/", "name", "mosys platform model"},
  {"/", "brand-code", "mosys platform brand"},
  {"/identity", "sku-id", "mosys platform sku"},
  {"/identity", "platform-name", "mosys platform name"},
  {"/hardware-properties", "psu-type", "mosys psu type"}};

}  // namespace

namespace brillo {

CrosConfigFallback::CrosConfigFallback() {}
CrosConfigFallback::~CrosConfigFallback() {}

static bool GetStringForEntry(const struct CommandMapEntry& entry,
                              std::string* val_out) {
  std::vector<std::string> argv = base::SplitString(
      entry.command, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

  if (!base::GetAppOutput(argv, val_out)) {
    CROS_CONFIG_LOG(ERROR) << "\"" << entry.command
                           << "\" has non-zero exit code";
    return false;
  }

  // Trim off (one) trailing newline from mosys
  if (val_out->back() == '\n')
    val_out->pop_back();
  return true;
}

bool CrosConfigFallback::WriteConfigFS(const base::FilePath& output_dir) {
  for (auto entry : kCommandMap) {
    std::string value;
    if (!GetStringForEntry(entry, &value)) {
      // Not all mosys commands may be supported on every board. Don't
      // write the property if the board does not support it.
      continue;
    }

    auto path_dir = output_dir;
    for (const auto& part :
         base::SplitStringPiece(entry.path, "/", base::KEEP_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY)) {
      path_dir = path_dir.Append(part);
    }

    if (!MkdirRecursively(path_dir, 0755).is_valid()) {
      CROS_CONFIG_LOG(ERROR)
          << "Unable to create directory " << path_dir.value() << ": "
          << logging::SystemErrorCodeToString(
                 logging::GetLastSystemErrorCode());
      return false;
    }

    const auto property_file = path_dir.Append(entry.property);
    if (base::WriteFile(property_file, value.data(), value.length()) < 0) {
      CROS_CONFIG_LOG(ERROR)
          << "Unable to create file " << property_file.value();
      return false;
    }
  }
  return true;
}

}  // namespace brillo
