// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/backlight_utils.h"

#include <string>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

using ::chromeos::cros_healthd::mojom::BacklightInfo;
using ::chromeos::cros_healthd::mojom::BacklightInfoPtr;

constexpr char kBacklightPropertiesPath[] = "/cros-healthd/backlight";
constexpr char kHasBacklightProperty[] = "has-backlight";
constexpr char kRelativeBacklightDirectoryPath[] = "sys/class/backlight";

// Fetches backlight information for a specific sysfs path. On success,
// populates |output_info| with the fetched information and returns true.
bool FetchBacklightInfoForPath(const base::FilePath& path,
                               BacklightInfoPtr* output_info) {
  DCHECK(output_info);

  BacklightInfo info;
  info.path = path.value();

  if (!ReadInteger(path, "max_brightness", &base::StringToUint,
                   &info.max_brightness)) {
    LOG(ERROR) << "Failed to read max_brightness for " << path.value();
    return false;
  }

  if (!ReadInteger(path, "brightness", &base::StringToUint, &info.brightness)) {
    LOG(ERROR) << "Failed to read brightness for " << path.value();
    return false;
  }

  *output_info = info.Clone();
  return true;
}

}  // namespace

BacklightFetcher::BacklightFetcher(brillo::CrosConfigInterface* cros_config)
    : cros_config_(cros_config) {
  DCHECK(cros_config_);
}
BacklightFetcher::~BacklightFetcher() = default;

std::vector<BacklightInfoPtr> BacklightFetcher::FetchBacklightInfo(
    const base::FilePath& root) {
  std::vector<BacklightInfoPtr> backlights;

  std::string has_backlight;
  cros_config_->GetString(kBacklightPropertiesPath, kHasBacklightProperty,
                          &has_backlight);
  if (has_backlight == "false")
    return backlights;

  base::FileEnumerator backlight_dirs(
      root.AppendASCII(kRelativeBacklightDirectoryPath),
      false /* is_recursive */,
      base::FileEnumerator::SHOW_SYM_LINKS | base::FileEnumerator::FILES |
          base::FileEnumerator::DIRECTORIES);

  for (base::FilePath path = backlight_dirs.Next(); !path.empty();
       path = backlight_dirs.Next()) {
    VLOG(1) << "Processing the node " << path.value();
    BacklightInfoPtr backlight;
    if (FetchBacklightInfoForPath(path, &backlight)) {
      DCHECK_NE(backlight->path, "");
      DCHECK_LE(backlight->brightness, backlight->max_brightness);
      backlights.push_back(std::move(backlight));
    }
  }

  return backlights;
}

}  // namespace diagnostics
