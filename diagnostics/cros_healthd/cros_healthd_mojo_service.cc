// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"

#include <sys/types.h>

#include <utility>

#include <base/logging.h>

#include "diagnostics/cros_healthd/fetchers/process_fetcher.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

CrosHealthdMojoService::CrosHealthdMojoService(
    FetchAggregator* fetch_aggregator,
    BluetoothEvents* bluetooth_events,
    LidEvents* lid_events,
    PowerEvents* power_events)
    : fetch_aggregator_(fetch_aggregator),
      bluetooth_events_(bluetooth_events),
      lid_events_(lid_events),
      power_events_(power_events) {
  DCHECK(fetch_aggregator_);
  DCHECK(bluetooth_events_);
  DCHECK(lid_events_);
  DCHECK(power_events_);
}

CrosHealthdMojoService::~CrosHealthdMojoService() = default;

void CrosHealthdMojoService::AddBluetoothObserver(
    chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserverPtr observer) {
  bluetooth_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddLidObserver(
    chromeos::cros_healthd::mojom::CrosHealthdLidObserverPtr observer) {
  lid_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddPowerObserver(
    chromeos::cros_healthd::mojom::CrosHealthdPowerObserverPtr observer) {
  power_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::ProbeProcessInfo(
    uint32_t process_id, ProbeProcessInfoCallback callback) {
  ProcessFetcher(static_cast<pid_t>(process_id))
      .FetchProcessInfo(std::move(callback));
}

void CrosHealthdMojoService::ProbeTelemetryInfo(
    const std::vector<ProbeCategoryEnum>& categories,
    ProbeTelemetryInfoCallback callback) {
  return fetch_aggregator_->Run(categories, std::move(callback));
}

void CrosHealthdMojoService::AddProbeBinding(
    chromeos::cros_healthd::mojom::CrosHealthdProbeServiceRequest request) {
  probe_binding_set_.AddBinding(this /* impl */, std::move(request));
}

void CrosHealthdMojoService::AddEventBinding(
    chromeos::cros_healthd::mojom::CrosHealthdEventServiceRequest request) {
  event_binding_set_.AddBinding(this /* impl */, std::move(request));
}

}  // namespace diagnostics
