// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/timezone_utils.h"

#include <string>

#include <base/files/file_util.h>
#include <brillo/timezone/tzif_parser.h>

#include "diagnostics/common/file_utils.h"

namespace diagnostics {

namespace {

constexpr char kLocaltimeFile[] = "var/lib/timezone/localtime";
constexpr char kZoneInfoPath[] = "usr/share/zoneinfo";

using ::chromeos::cros_healthd::mojom::TimezoneInfo;
using ::chromeos::cros_healthd::mojom::TimezoneInfoPtr;

bool GetTimezone(const base::FilePath& root,
                 std::string* posix,
                 std::string* region) {
  DCHECK(posix);
  DCHECK(region);

  base::FilePath timezone_path;
  base::FilePath localtime_path = root.AppendASCII(kLocaltimeFile);
  if (!base::NormalizeFilePath(localtime_path, &timezone_path)) {
    LOG(ERROR) << "Unable to read symlink of localtime file: "
               << localtime_path.value();
    return false;
  }

  base::FilePath timezone_region_path;
  base::FilePath zone_info_path = root.AppendASCII(kZoneInfoPath);
  if (!zone_info_path.AppendRelativePath(timezone_path,
                                         &timezone_region_path)) {
    LOG(ERROR) << "Unable to get timezone region from zone info path: "
               << timezone_path.value();
    return false;
  }
  *region = timezone_region_path.value();

  if (!brillo::timezone::GetPosixTimezone(timezone_path, posix)) {
    LOG(ERROR) << "Unable to get posix timezone from timezone path: "
               << timezone_path.value();
    return false;
  }

  return true;
}

}  // namespace

TimezoneInfoPtr FetchTimezoneInfo(const base::FilePath& root) {
  TimezoneInfo info;
  GetTimezone(root, &info.posix, &info.region);

  return info.Clone();
}

}  // namespace diagnostics
