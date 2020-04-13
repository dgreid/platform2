// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/backlight_utils.h"

#include <string>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>

#include "diagnostics/common/file_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr char kBacklightPropertiesPath[] = "/cros-healthd/backlight";
constexpr char kHasBacklightProperty[] = "has-backlight";
constexpr char kRelativeBacklightDirectoryPath[] = "sys/class/backlight";

// Fetches backlight information for a specific sysfs path. On success,
// populates |output_info| with the fetched information and returns a
// base::nullopt. When an error occurs, a ProbeError is returned and
// |output_info| does not contain valid information.
base::Optional<mojo_ipc::ProbeErrorPtr> FetchBacklightInfoForPath(
    const base::FilePath& path, mojo_ipc::BacklightInfoPtr* output_info) {
  DCHECK(output_info);

  mojo_ipc::BacklightInfo info;
  info.path = path.value();

  if (!ReadInteger(path, "max_brightness", &base::StringToUint,
                   &info.max_brightness)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Failed to read max_brightness for " + path.value());
  }

  if (!ReadInteger(path, "brightness", &base::StringToUint, &info.brightness)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Failed to read brightness for " + path.value());
  }

  *output_info = info.Clone();
  return base::nullopt;
}

}  // namespace

BacklightFetcher::BacklightFetcher(brillo::CrosConfigInterface* cros_config)
    : cros_config_(cros_config) {
  DCHECK(cros_config_);
}
BacklightFetcher::~BacklightFetcher() = default;

mojo_ipc::BacklightResultPtr BacklightFetcher::FetchBacklightInfo(
    const base::FilePath& root) {
  std::vector<mojo_ipc::BacklightInfoPtr> backlights;

  std::string has_backlight;
  cros_config_->GetString(kBacklightPropertiesPath, kHasBacklightProperty,
                          &has_backlight);
  if (has_backlight == "false")
    return mojo_ipc::BacklightResult::NewBacklightInfo(std::move(backlights));

  base::FileEnumerator backlight_dirs(
      root.AppendASCII(kRelativeBacklightDirectoryPath),
      false /* is_recursive */,
      base::FileEnumerator::SHOW_SYM_LINKS | base::FileEnumerator::FILES |
          base::FileEnumerator::DIRECTORIES);

  for (base::FilePath path = backlight_dirs.Next(); !path.empty();
       path = backlight_dirs.Next()) {
    VLOG(1) << "Processing the node " << path.value();
    mojo_ipc::BacklightInfoPtr backlight;
    auto error = FetchBacklightInfoForPath(path, &backlight);
    if (error.has_value()) {
      return mojo_ipc::BacklightResult::NewError(std::move(error.value()));
    }
    DCHECK_NE(backlight->path, "");
    DCHECK_LE(backlight->brightness, backlight->max_brightness);
    backlights.push_back(std::move(backlight));
  }

  return mojo_ipc::BacklightResult::NewBacklightInfo(std::move(backlights));
}

}  // namespace diagnostics
