// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_FAN_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_FAN_FETCHER_H_

#include <vector>

#include <base/callback_forward.h>
#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_executor.mojom.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Relative filepath used to determine whether a device has a Google EC.
constexpr char kRelativeCrosEcPath[] = "sys/class/chromeos/cros_ec";

// The FanFetcher class is responsible for gathering fan info reported by
// cros_healthd.
class FanFetcher {
 public:
  using FetchFanInfoCallback =
      base::OnceCallback<void(chromeos::cros_healthd::mojom::FanResultPtr)>;

  explicit FanFetcher(Context* context);
  FanFetcher(const FanFetcher&) = delete;
  FanFetcher& operator=(const FanFetcher&) = delete;
  ~FanFetcher();

  // Returns either a list of data about each of the device's fans or the error
  // that occurred retrieving the information.
  void FetchFanInfo(const base::FilePath& root_dir,
                    FetchFanInfoCallback callback);

 private:
  // Handles the executor's response to a GetFanSpeed IPC.
  void HandleFanSpeedResponse(
      FetchFanInfoCallback callback,
      chromeos::cros_healthd_executor::mojom::ProcessResultPtr result);

  // Unowned pointer that outlives this FanFetcher instance.
  Context* const context_ = nullptr;

  // Must be the last member of the class, so that it's destroyed first when an
  // instance of the class is destroyed. This will prevent any outstanding
  // callbacks from being run and segfaulting.
  base::WeakPtrFactory<FanFetcher> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_FAN_FETCHER_H_
