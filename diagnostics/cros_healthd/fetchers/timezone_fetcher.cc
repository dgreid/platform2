// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/timezone_fetcher.h"

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <brillo/timezone/tzif_parser.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr char kLocaltimeFile[] = "var/lib/timezone/localtime";
constexpr char kZoneInfoPath[] = "usr/share/zoneinfo";

}  // namespace

mojo_ipc::TimezoneResultPtr FetchTimezoneInfo(const base::FilePath& root) {
  base::FilePath timezone_path;
  base::FilePath localtime_path = root.AppendASCII(kLocaltimeFile);
  if (!base::NormalizeFilePath(localtime_path, &timezone_path)) {
    return mojo_ipc::TimezoneResult::NewError(CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read symlink of localtime file: " + localtime_path.value()));
  }

  base::FilePath timezone_region_path;
  base::FilePath zone_info_path = root.AppendASCII(kZoneInfoPath);
  if (!zone_info_path.AppendRelativePath(timezone_path,
                                         &timezone_region_path)) {
    return mojo_ipc::TimezoneResult::NewError(CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to get timezone region from zone info path: " +
            timezone_path.value()));
  }
  auto posix_result = brillo::timezone::GetPosixTimezone(timezone_path);
  if (!posix_result) {
    return mojo_ipc::TimezoneResult::NewError(CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to get posix timezone from timezone path: " +
            timezone_path.value()));
  }

  return mojo_ipc::TimezoneResult::NewTimezoneInfo(mojo_ipc::TimezoneInfo::New(
      posix_result.value(), timezone_region_path.value()));
}

}  // namespace diagnostics
