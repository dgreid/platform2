// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/macros.h>
#include <base/optional.h>
#include <mojo/public/cpp/bindings/binding_set.h>

#include "diagnostics/cros_healthd/cros_healthd_routine_service.h"
#include "diagnostics/cros_healthd/utils/backlight_utils.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "diagnostics/cros_healthd/utils/vpd_utils.h"
#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Implements the "CrosHealthdService" Mojo interface exposed by the
// cros_healthd daemon (see the API definition at mojo/cros_healthd.mojom)
class CrosHealthdMojoService final
    : public chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService,
      public chromeos::cros_healthd::mojom::CrosHealthdProbeService {
 public:
  using DiagnosticRoutineStatusEnum =
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum;
  using ProbeCategoryEnum = chromeos::cros_healthd::mojom::ProbeCategoryEnum;
  using RunRoutineResponse = chromeos::cros_healthd::mojom::RunRoutineResponse;

  // |backlight_fetcher| - BacklightFetcher implementation.
  // |battery_fetcher| - BatteryFetcher implementation.
  // |cached_vpd_fetcher| - CachedVpdFetcher implementation.
  // |routine_service| - CrosHealthdRoutineService implementation.
  CrosHealthdMojoService(BacklightFetcher* backlight_fetcher,
                         BatteryFetcher* battery_fetcher,
                         CachedVpdFetcher* cached_vpd_fetcher,
                         CrosHealthdRoutineService* routine_service);
  ~CrosHealthdMojoService() override;

  // chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService overrides:
  void GetAvailableRoutines(
      const GetAvailableRoutinesCallback& callback) override;
  void GetRoutineUpdate(
      int32_t id,
      chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
      bool include_output,
      const GetRoutineUpdateCallback& callback) override;
  void RunUrandomRoutine(uint32_t length_seconds,
                         const RunUrandomRoutineCallback& callback) override;
  void RunBatteryCapacityRoutine(
      uint32_t low_mah,
      uint32_t high_mah,
      const RunBatteryCapacityRoutineCallback& callback) override;
  void RunBatteryHealthRoutine(
      uint32_t maximum_cycle_count,
      uint32_t percent_battery_wear_allowed,
      const RunBatteryHealthRoutineCallback& callback) override;
  void RunSmartctlCheckRoutine(
      const RunSmartctlCheckRoutineCallback& callback) override;
  void RunAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type,
      const RunAcPowerRoutineCallback& callback) override;

  // chromeos::cros_healthd::mojom::CrosHealthdProbeService overrides:
  void ProbeTelemetryInfo(const std::vector<ProbeCategoryEnum>& categories,
                          const ProbeTelemetryInfoCallback& callback) override;

  // Adds a new binding to the internal binding sets.
  void AddProbeBinding(
      chromeos::cros_healthd::mojom::CrosHealthdProbeServiceRequest request);
  void AddDiagnosticsBinding(
      chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsServiceRequest
          request);

 private:
  // Mojo binding sets that connect |this| with message pipes, allowing the
  // remote ends to call our methods.
  mojo::BindingSet<chromeos::cros_healthd::mojom::CrosHealthdProbeService>
      probe_binding_set_;
  mojo::BindingSet<chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService>
      diagnostics_binding_set_;

  // Unowned. The backlight fetcher should outlive this instance.
  BacklightFetcher* backlight_fetcher_;
  // Unowned. The battery fetcher should outlive this instance.
  BatteryFetcher* battery_fetcher_;
  // Unowned. The cached VPD fetcher should outlive this instance.
  CachedVpdFetcher* cached_vpd_fetcher_;
  // Unowned. The routine service should outlive this instance.
  CrosHealthdRoutineService* const routine_service_;

  DISALLOW_COPY_AND_ASSIGN(CrosHealthdMojoService);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_
