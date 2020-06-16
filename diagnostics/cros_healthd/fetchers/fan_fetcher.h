// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_FAN_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_FAN_FETCHER_H_

#include <vector>

#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Relative filepath used to determine whether a device has a Google EC.
constexpr char kRelativeCrosEcPath[] = "sys/class/chromeos/cros_ec";

// The maximum amount of time to wait for a debugd response.
constexpr base::TimeDelta kDebugdDBusTimeout = base::TimeDelta::FromSeconds(10);

// The FanFetcher class is responsible for gathering fan info reported by
// cros_healthd.
class FanFetcher {
 public:
  explicit FanFetcher(Context* context);
  FanFetcher(const FanFetcher&) = delete;
  FanFetcher& operator=(const FanFetcher&) = delete;
  ~FanFetcher();

  // Returns either a list of data about each of the device's fans or the error
  // that occurred retrieving the information.
  chromeos::cros_healthd::mojom::FanResultPtr FetchFanInfo(
      const base::FilePath& root_dir);

 private:
  // Unowned pointer that outlives this FanFetcher instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_FAN_FETCHER_H_
