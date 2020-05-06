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
#include "diagnostics/cros_healthd/events/bluetooth_events.h"
#include "diagnostics/cros_healthd/events/lid_events.h"
#include "diagnostics/cros_healthd/events/power_events.h"
#include "diagnostics/cros_healthd/utils/backlight_utils.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "diagnostics/cros_healthd/utils/fan_utils.h"
#include "diagnostics/cros_healthd/utils/vpd_utils.h"
#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Implements the "CrosHealthdService" Mojo interface exposed by the
// cros_healthd daemon (see the API definition at mojo/cros_healthd.mojom)
class CrosHealthdMojoService final
    : public chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService,
      public chromeos::cros_healthd::mojom::CrosHealthdEventService,
      public chromeos::cros_healthd::mojom::CrosHealthdProbeService {
 public:
  using DiagnosticRoutineStatusEnum =
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum;
  using ProbeCategoryEnum = chromeos::cros_healthd::mojom::ProbeCategoryEnum;
  using RunRoutineResponse = chromeos::cros_healthd::mojom::RunRoutineResponse;

  // |backlight_fetcher| - BacklightFetcher implementation.
  // |battery_fetcher| - BatteryFetcher implementation.
  // |cached_vpd_fetcher| - CachedVpdFetcher implementation.
  // |fan_fetcher| - FanFetcher implementation.
  // |bluetooth_events| - BluetoothEvents implementation.
  // |lid_events| - LidEvents implementation.
  // |power_events| - PowerEvents implementation.
  // |routine_service| - CrosHealthdRoutineService implementation.
  CrosHealthdMojoService(BacklightFetcher* backlight_fetcher,
                         BatteryFetcher* battery_fetcher,
                         CachedVpdFetcher* cached_vpd_fetcher,
                         FanFetcher* fan_fetcher,
                         BluetoothEvents* bluetooth_events,
                         LidEvents* lid_events,
                         PowerEvents* power_events,
                         CrosHealthdRoutineService* routine_service);
  ~CrosHealthdMojoService() override;

  // chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService overrides:
  void GetAvailableRoutines(GetAvailableRoutinesCallback callback) override;
  void GetRoutineUpdate(
      int32_t id,
      chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
      bool include_output,
      GetRoutineUpdateCallback callback) override;
  void RunUrandomRoutine(uint32_t length_seconds,
                         RunUrandomRoutineCallback callback) override;
  void RunBatteryCapacityRoutine(
      uint32_t low_mah,
      uint32_t high_mah,
      RunBatteryCapacityRoutineCallback callback) override;
  void RunBatteryHealthRoutine(
      uint32_t maximum_cycle_count,
      uint32_t percent_battery_wear_allowed,
      RunBatteryHealthRoutineCallback callback) override;
  void RunSmartctlCheckRoutine(
      RunSmartctlCheckRoutineCallback callback) override;
  void RunAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type,
      RunAcPowerRoutineCallback callback) override;
  void RunCpuCacheRoutine(uint32_t length_seconds,
                          RunCpuCacheRoutineCallback callback) override;
  void RunCpuStressRoutine(uint32_t length_seconds,
                           RunCpuStressRoutineCallback callback) override;
  void RunFloatingPointAccuracyRoutine(
      uint32_t length_seconds,
      RunFloatingPointAccuracyRoutineCallback callback) override;
  void RunNvmeWearLevelRoutine(
      uint32_t wear_level_threshold,
      RunNvmeWearLevelRoutineCallback callback) override;
  void RunNvmeSelfTestRoutine(
      chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type,
      RunNvmeSelfTestRoutineCallback callback) override;
  void RunDiskReadRoutine(
      chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      uint32_t length_seconds,
      uint32_t file_size_mb,
      RunDiskReadRoutineCallback callback) override;
  void RunPrimeSearchRoutine(uint32_t length_seconds,
                             uint64_t max_num,
                             RunPrimeSearchRoutineCallback callback) override;
  void RunBatteryDischargeRoutine(
      uint32_t length_seconds,
      uint32_t maximum_discharge_percent_allowed,
      RunBatteryDischargeRoutineCallback callback) override;

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
  void ProbeTelemetryInfo(const std::vector<ProbeCategoryEnum>& categories,
                          ProbeTelemetryInfoCallback callback) override;

  // Adds a new binding to the internal binding sets.
  void AddProbeBinding(
      chromeos::cros_healthd::mojom::CrosHealthdProbeServiceRequest request);
  void AddDiagnosticsBinding(
      chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsServiceRequest
          request);
  void AddEventBinding(
      chromeos::cros_healthd::mojom::CrosHealthdEventServiceRequest request);

 private:
  // Mojo binding sets that connect |this| with message pipes, allowing the
  // remote ends to call our methods.
  mojo::BindingSet<chromeos::cros_healthd::mojom::CrosHealthdProbeService>
      probe_binding_set_;
  mojo::BindingSet<chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService>
      diagnostics_binding_set_;
  mojo::BindingSet<chromeos::cros_healthd::mojom::CrosHealthdEventService>
      event_binding_set_;

  // Unowned. The backlight fetcher should outlive this instance.
  BacklightFetcher* backlight_fetcher_;
  // Unowned. The battery fetcher should outlive this instance.
  BatteryFetcher* battery_fetcher_;
  // Unowned. The cached VPD fetcher should outlive this instance.
  CachedVpdFetcher* cached_vpd_fetcher_;
  // Unowned. The fan fetcher should outlive this instance.
  FanFetcher* fan_fetcher_;
  // Unowned. The BluetoothEvents instance should outlive this instance.
  BluetoothEvents* const bluetooth_events_ = nullptr;
  // Unowned. The lid events should outlive this instance.
  LidEvents* const lid_events_ = nullptr;
  // Unowned. The power events should outlive this instance.
  PowerEvents* const power_events_ = nullptr;
  // Unowned. The routine service should outlive this instance.
  CrosHealthdRoutineService* const routine_service_;

  DISALLOW_COPY_AND_ASSIGN(CrosHealthdMojoService);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_MOJO_SERVICE_H_
