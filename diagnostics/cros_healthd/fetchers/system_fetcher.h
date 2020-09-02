// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_H_

#include <base/files/file_path.h>
#include <base/optional.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Relative path to DMI information.
extern const char kRelativeDmiInfoPath[];

// Files related to DMI information.
extern const char kBiosVersionFileName[];
extern const char kBoardNameFileName[];
extern const char kBoardVersionFileName[];
extern const char kChassisTypeFileName[];
extern const char kProductNameFileName[];

// Relative paths to cached VPD information.
extern const char kRelativeVpdRoPath[];
extern const char kRelativeVpdRwPath[];

// Files related to cached VPD information.
extern const char kFirstPowerDateFileName[];
extern const char kManufactureDateFileName[];
extern const char kSkuNumberFileName[];

class SystemFetcher final {
 public:
  explicit SystemFetcher(Context* context);
  SystemFetcher(const SystemFetcher&) = delete;
  SystemFetcher& operator=(const SystemFetcher&) = delete;
  ~SystemFetcher();

  // Returns either a structure with the system information or the error that
  // occurred fetching the information.
  chromeos::cros_healthd::mojom::SystemResultPtr FetchSystemInfo(
      const base::FilePath& root_dir);

 private:
  // Fetches information from cached VPD. On success, populates |output_info|
  // with the fetched information and returns base::nullopt. When an error
  // occurs, a ProbeError is returned and |output_info| does not contain valid
  // information.
  base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr>
  FetchCachedVpdInfo(const base::FilePath& root_dir,
                     chromeos::cros_healthd::mojom::SystemInfo* output_info);

  // Fetches information from the master configuration using CrosConfig. Since
  // this function does not read from a file, it does not check for errors.
  void FetchMasterConfigInfo(
      chromeos::cros_healthd::mojom::SystemInfo* output_info);

  // Fetches the operating system version and populates the |output_info|
  // structure.
  base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr> FetchOsVersion(
      chromeos::cros_healthd::mojom::OsVersion* os_version);

  // Unowned pointer that outlives this SystemFetcher instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_H_
