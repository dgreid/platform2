// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_H_

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <base/optional.h>
#include <base/time/time.h>

#include "mojo/cros_healthd.mojom.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/cros_healthd_events.mojom.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Provides a mojo connection to cros_healthd. See mojo/cros_healthd.mojom for
// details on cros_healthd's mojo interface. This should only be used by
// processes whose only mojo connection is to cros_healthd. This is a public
// interface of the class providing the functionality.
class CrosHealthdMojoAdapter {
 public:
  virtual ~CrosHealthdMojoAdapter() {}

  // Creates an instance of CrosHealthdMojoAdapter.
  static std::unique_ptr<CrosHealthdMojoAdapter> Create();

  // Gets telemetry information from cros_healthd.
  virtual chromeos::cros_healthd::mojom::TelemetryInfoPtr GetTelemetryInfo(
      const std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>&
          categories_to_probe) = 0;

  // Gets information about a specific process from cros_healthd.
  virtual chromeos::cros_healthd::mojom::ProcessResultPtr GetProcessInfo(
      pid_t pid) = 0;

  // Runs the urandom routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunUrandomRoutine(const base::Optional<base::TimeDelta>& length_seconds) = 0;

  // Runs the battery capacity routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryCapacityRoutine() = 0;

  // Runs the battery health routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryHealthRoutine() = 0;

  // Runs the smartctl-check routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunSmartctlCheckRoutine() = 0;

  // Runs the AC power routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type) = 0;

  // Runs the CPU cache routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunCpuCacheRoutine(const base::Optional<base::TimeDelta>& exec_duration) = 0;

  // Runs the CPU stress routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunCpuStressRoutine(const base::Optional<base::TimeDelta>& exec_duration) = 0;

  // Runs the floating-point-accuracy routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunFloatingPointAccuracyRoutine(
      const base::Optional<base::TimeDelta>& exec_duration) = 0;

  // Runs the NvmeWearLevel routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunNvmeWearLevelRoutine(uint32_t wear_level_threshold) = 0;

  // Runs the NvmeSelfTest routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunNvmeSelfTestRoutine(chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum
                             nvme_self_test_type) = 0;

  // Runs the disk read routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunDiskReadRoutine(
      chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      base::TimeDelta exec_duration,
      uint32_t file_size_mb) = 0;

  // Runs the prime search routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunPrimeSearchRoutine(
      const base::Optional<base::TimeDelta>& exec_duration) = 0;

  // Runs the battery discharge routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryDischargeRoutine(base::TimeDelta exec_duration,
                             uint32_t maximum_discharge_percent_allowed) = 0;

  // Runs the battery charge routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryChargeRoutine(base::TimeDelta exec_duration,
                          uint32_t minimum_charge_percent_required) = 0;

  // Runs the LAN connectivity routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunLanConnectivityRoutine() = 0;

  // Runs the signal strength routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunSignalStrengthRoutine() = 0;

  // Runs the memory routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunMemoryRoutine() = 0;

  // Runs the gateway can be pinged routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunGatewayCanBePingedRoutine() = 0;

  // Runs the has secure WiFi connection routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunHasSecureWiFiConnectionRoutine() = 0;

  // Runs the DNS resolver present routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunDnsResolverPresentRoutine() = 0;

  // Runs the DNS latency routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunDnsLatencyRoutine() = 0;

  // Runs the DNS resolution routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunDnsResolutionRoutine() = 0;

  // Runs the captive portal routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunCaptivePortalRoutine() = 0;

  // Runs the HTTP firewall routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunHttpFirewallRoutine() = 0;

  // Runs the HTTPS firewall routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunHttpsFirewallRoutine() = 0;

  // Runs the HTTPS latency routine.
  virtual chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunHttpsLatencyRoutine() = 0;

  // Returns which routines are available on the platform.
  virtual std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>
  GetAvailableRoutines() = 0;

  // Gets an update for the specified routine.
  virtual chromeos::cros_healthd::mojom::RoutineUpdatePtr GetRoutineUpdate(
      int32_t id,
      chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
      bool include_output) = 0;

  // Subscribes the client to Bluetooth events.
  virtual void AddBluetoothObserver(
      chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserverPtr
          observer) = 0;

  // Subscribes the client to lid events.
  virtual void AddLidObserver(
      chromeos::cros_healthd::mojom::CrosHealthdLidObserverPtr observer) = 0;

  // Subscribes the client to power events.
  virtual void AddPowerObserver(
      chromeos::cros_healthd::mojom::CrosHealthdPowerObserverPtr observer) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_H_
