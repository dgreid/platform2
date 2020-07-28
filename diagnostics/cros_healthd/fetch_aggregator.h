// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCH_AGGREGATOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCH_AGGREGATOR_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/synchronization/lock.h>

#include "diagnostics/cros_healthd/fetchers/backlight_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/battery_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/cpu_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/disk_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/fan_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/network_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd.mojom.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// This class is responsible for aggregating probe data from various fetchers,
// some of which may be asynchronous, and running the given callback when all
// probe data has been fetched.
class FetchAggregator final {
 public:
  explicit FetchAggregator(Context* context);
  FetchAggregator(const FetchAggregator&) = delete;
  FetchAggregator& operator=(const FetchAggregator&) = delete;
  ~FetchAggregator();

  // Runs the aggregator, which will collect all relevant data and then run the
  // callback.
  void Run(const std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>&
               categories_to_probe,
           chromeos::cros_healthd::mojom::CrosHealthdProbeService::
               ProbeTelemetryInfoCallback callback);

 private:
  // Holds all state related to a single call to Run().
  struct ProbeState {
    // Contains requested categories which have not been fetched yet.
    std::set<chromeos::cros_healthd::mojom::ProbeCategoryEnum>
        remaining_categories;
    // Callback which will be run once all requested categories have been
    // fetched.
    chromeos::cros_healthd::mojom::CrosHealthdProbeService::
        ProbeTelemetryInfoCallback callback;
    // Holds all fetched data.
    chromeos::cros_healthd::mojom::TelemetryInfo fetched_data;
  };

  // Wraps a fetch operation from either a synchronous or asynchronous fetcher.
  template <class T>
  void WrapFetchProbeData(
      chromeos::cros_healthd::mojom::ProbeCategoryEnum category,
      std::map<uint32_t, std::unique_ptr<ProbeState>>::iterator itr,
      T* response_data,
      T fetched_data);

  // Returns the next available key in |pending_calls_|.
  uint32_t GetNextAvailableKey();

  // Maps call state to individual calls to Run(). This allows a single
  // FetchAggregator instance to have multiple pending asychronous fetches
  // corresponding to distinct Run() calls.
  std::map<uint32_t, std::unique_ptr<ProbeState>> pending_calls_;

  // Protects against one fetcher setting the last bit of a |fetched_data| map
  // to true while another fetcher reads it. Without the lock, the reading
  // fetcher would then take ownership of the callback and run it, then the
  // setting fetcher would later attempt to run the invalid callback a second
  // time.
  base::Lock lock_;

  // Unowned. The backlight fetcher should outlive this instance.
  std::unique_ptr<BacklightFetcher> const backlight_fetcher_ = nullptr;
  // Unowned. The battery fetcher should outlive this instance.
  std::unique_ptr<BatteryFetcher> const battery_fetcher_ = nullptr;
  // Unowned. The Bluetooth fetcher should outlive this instance.
  std::unique_ptr<BluetoothFetcher> const bluetooth_fetcher_ = nullptr;
  // Unowned. The CPU fetcher should outlive this instance.
  std::unique_ptr<CpuFetcher> const cpu_fetcher_ = nullptr;
  // Unowned. The disk fetcher should outlive this instance.
  std::unique_ptr<DiskFetcher> const disk_fetcher_ = nullptr;
  // Unowned. The fan fetcher should outlive this instance.
  std::unique_ptr<FanFetcher> const fan_fetcher_ = nullptr;
  // Unowned. The system fetcher should outlive this instance.
  std::unique_ptr<SystemFetcher> const system_fetcher_ = nullptr;
  // Unowned. The network fetcher should outlive this instance.
  std::unique_ptr<NetworkFetcher> const network_fetcher_ = nullptr;

  // Must be the last member of the class.
  base::WeakPtrFactory<FetchAggregator> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCH_AGGREGATOR_H_
