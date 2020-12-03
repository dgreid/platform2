// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_

#include <cstdint>
#include <vector>

#include <mojo/public/cpp/bindings/binding_set.h>

#include "diagnostics/cros_healthd/events/bluetooth_events.h"
#include "diagnostics/cros_healthd/events/lid_events.h"
#include "diagnostics/cros_healthd/events/power_events.h"
#include "diagnostics/cros_healthd/fetch_aggregator.h"
#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Implements the "CrosHealthdService" Mojo interface exposed by the
// cros_healthd daemon (see the API definition at mojo/cros_healthd.mojom)
class CrosHealthdMojoService final
    : public chromeos::cros_healthd::mojom::CrosHealthdEventService,
      public chromeos::cros_healthd::mojom::CrosHealthdProbeService {
 public:
  using ProbeCategoryEnum = chromeos::cros_healthd::mojom::ProbeCategoryEnum;

  // |fetch_aggregator| - responsible for fulfilling probe requests.
  // |bluetooth_events| - BluetoothEvents implementation.
  // |lid_events| - LidEvents implementation.
  // |power_events| - PowerEvents implementation.
  CrosHealthdMojoService(Context* context,
                         FetchAggregator* fetch_aggregator,
                         BluetoothEvents* bluetooth_events,
                         LidEvents* lid_events,
                         PowerEvents* power_events);
  CrosHealthdMojoService(const CrosHealthdMojoService&) = delete;
  CrosHealthdMojoService& operator=(const CrosHealthdMojoService&) = delete;
  ~CrosHealthdMojoService() override;

  // chromeos::cros_healthd::mojom::CrosHealthdEventService overrides:
  void AddBluetoothObserver(
      chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserverPtr observer)
      override;
  void AddLidObserver(chromeos::cros_healthd::mojom::CrosHealthdLidObserverPtr
                          observer) override;
  void AddPowerObserver(
      chromeos::cros_healthd::mojom::CrosHealthdPowerObserverPtr observer)
      override;

  // chromeos::cros_healthd::mojom::CrosHealthdProbeService overrides:
  void ProbeProcessInfo(uint32_t process_id,
                        ProbeProcessInfoCallback callback) override;
  void ProbeTelemetryInfo(const std::vector<ProbeCategoryEnum>& categories,
                          ProbeTelemetryInfoCallback callback) override;

  // Adds a new binding to the internal binding sets.
  void AddProbeBinding(
      chromeos::cros_healthd::mojom::CrosHealthdProbeServiceRequest request);
  void AddEventBinding(
      chromeos::cros_healthd::mojom::CrosHealthdEventServiceRequest request);

 private:
  // Mojo binding sets that connect |this| with message pipes, allowing the
  // remote ends to call our methods.
  mojo::BindingSet<chromeos::cros_healthd::mojom::CrosHealthdProbeService>
      probe_binding_set_;
  mojo::BindingSet<chromeos::cros_healthd::mojom::CrosHealthdEventService>
      event_binding_set_;

  // Unowned. The Context instance should outlive this instance.
  Context* const context_ = nullptr;
  // Unowned. The FetchAggregator instance should outlive this instance.
  FetchAggregator* fetch_aggregator_;
  // Unowned. The BluetoothEvents instance should outlive this instance.
  BluetoothEvents* const bluetooth_events_ = nullptr;
  // Unowned. The lid events should outlive this instance.
  LidEvents* const lid_events_ = nullptr;
  // Unowned. The power events should outlive this instance.
  PowerEvents* const power_events_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_
