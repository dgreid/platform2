// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetch_aggregator.h"

#include <iterator>
#include <map>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/logging.h>

#include "diagnostics/cros_healthd/fetchers/memory_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/stateful_partition_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/timezone_fetcher.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = chromeos::cros_healthd::mojom;

}  // namespace

FetchAggregator::FetchAggregator(Context* context)
    : backlight_fetcher_(std::make_unique<BacklightFetcher>(context)),
      battery_fetcher_(std::make_unique<BatteryFetcher>(context)),
      bluetooth_fetcher_(std::make_unique<BluetoothFetcher>(context)),
      cpu_fetcher_(std::make_unique<CpuFetcher>(context)),
      disk_fetcher_(std::make_unique<DiskFetcher>()),
      fan_fetcher_(std::make_unique<FanFetcher>(context)),
      system_fetcher_(std::make_unique<SystemFetcher>(context)),
      network_fetcher_(std::make_unique<NetworkFetcher>(context)) {
  DCHECK(backlight_fetcher_);
  DCHECK(battery_fetcher_);
  DCHECK(bluetooth_fetcher_);
  DCHECK(cpu_fetcher_);
  DCHECK(disk_fetcher_);
  DCHECK(fan_fetcher_);
  DCHECK(system_fetcher_);
  DCHECK(network_fetcher_);
}

FetchAggregator::~FetchAggregator() = default;

void FetchAggregator::Run(
    const std::vector<mojo_ipc::ProbeCategoryEnum>& categories_to_probe,
    mojo_ipc::CrosHealthdProbeService::ProbeTelemetryInfoCallback callback) {
  std::unique_ptr<ProbeState> state = std::make_unique<ProbeState>();
  state->remaining_categories = std::set<mojo_ipc::ProbeCategoryEnum>(
      categories_to_probe.begin(), categories_to_probe.end());
  state->callback = std::move(callback);

  auto itr_bool_pair =
      pending_calls_.emplace(GetNextAvailableKey(), std::move(state));
  DCHECK(itr_bool_pair.second);
  auto itr = itr_bool_pair.first;

  mojo_ipc::TelemetryInfo* info = &itr->second->fetched_data;

  for (const auto category : categories_to_probe) {
    switch (category) {
      case mojo_ipc::ProbeCategoryEnum::kBattery: {
        WrapFetchProbeData(category, itr, &info->battery_result,
                           battery_fetcher_->FetchBatteryInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kCpu: {
        WrapFetchProbeData(category, itr, &info->cpu_result,
                           cpu_fetcher_->FetchCpuInfo(base::FilePath("/")));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kNonRemovableBlockDevices: {
        WrapFetchProbeData(category, itr, &info->block_device_result,
                           disk_fetcher_->FetchNonRemovableBlockDevicesInfo(
                               base::FilePath("/")));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kTimezone: {
        WrapFetchProbeData(category, itr, &info->timezone_result,
                           FetchTimezoneInfo(base::FilePath("/")));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kMemory: {
        WrapFetchProbeData(category, itr, &info->memory_result,
                           FetchMemoryInfo(base::FilePath("/")));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kBacklight: {
        WrapFetchProbeData(
            category, itr, &info->backlight_result,
            backlight_fetcher_->FetchBacklightInfo(base::FilePath("/")));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kFan: {
        fan_fetcher_->FetchFanInfo(
            base::FilePath("/"),
            base::BindOnce(
                &FetchAggregator::WrapFetchProbeData<mojo_ipc::FanResultPtr>,
                weak_factory_.GetWeakPtr(), category, itr, &info->fan_result));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kStatefulPartition: {
        WrapFetchProbeData(category, itr, &info->stateful_partition_result,
                           FetchStatefulPartitionInfo(base::FilePath("/")));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kBluetooth: {
        WrapFetchProbeData(category, itr, &info->bluetooth_result,
                           bluetooth_fetcher_->FetchBluetoothInfo());
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kSystem: {
        WrapFetchProbeData(
            category, itr, &info->system_result,
            system_fetcher_->FetchSystemInfo(base::FilePath("/")));
        break;
      }
      case mojo_ipc::ProbeCategoryEnum::kNetwork: {
        network_fetcher_->FetchNetworkInfo(base::BindOnce(
            &FetchAggregator::WrapFetchProbeData<mojo_ipc::NetworkResultPtr>,
            weak_factory_.GetWeakPtr(), category, itr, &info->network_result));
        break;
      }
    }
  }
}

template <class T>
void FetchAggregator::WrapFetchProbeData(
    mojo_ipc::ProbeCategoryEnum category,
    std::map<uint32_t, std::unique_ptr<ProbeState>>::iterator itr,
    T* response_data,
    T fetched_data) {
  DCHECK(response_data);

  *response_data = std::move(fetched_data);

  base::AutoLock auto_lock(lock_);

  ProbeState* state = itr->second.get();

  auto* remaining_categories = &state->remaining_categories;
  // Remove the current category, since it's been fetched.
  remaining_categories->erase(category);

  // Check for any unfetched categories - if one exists, we can't run the
  // callback yet.
  if (!remaining_categories->empty())
    return;

  std::move(state->callback).Run(state->fetched_data.Clone());
  pending_calls_.erase(itr);
}

uint32_t FetchAggregator::GetNextAvailableKey() {
  uint32_t free_index = 0;
  for (auto it = pending_calls_.cbegin(); it != pending_calls_.cend(); it++) {
    // Since we allocate keys sequentially, if the iterator key ever skips a
    // value, then that value is free to take.
    if (free_index != it->first)
      break;
    free_index++;
  }

  return free_index;
}

}  // namespace diagnostics
