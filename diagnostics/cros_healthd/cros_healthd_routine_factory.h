// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_FACTORY_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_FACTORY_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/optional.h>
#include <base/time/time.h>

#include "diagnostics/common/system/debugd_adapter_impl.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Interface for constructing DiagnosticRoutines.
class CrosHealthdRoutineFactory {
 public:
  virtual ~CrosHealthdRoutineFactory() = default;

  // Constructs a new instance of the urandom routine. See
  // diagnostics/routines/urandom for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeUrandomRoutine(
      uint32_t length_seconds) = 0;
  // Constructs a new instance of the battery capacity routine. See
  // diagnostics/routines/battery for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBatteryCapacityRoutine() = 0;
  // Constructs a new instance of the battery health routine. See
  // diagnostics/routines/battery_sysfs for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBatteryHealthRoutine() = 0;
  // Constructs a new instance of the smartctl check routine. See
  // diagnostics/routines/smartctl_check for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeSmartctlCheckRoutine() = 0;
  // Constructs a new instance of the AC power routine. See
  // diagnostics/routines/battery_sysfs for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type) = 0;
  // Constructs a new instance of the CPU cache routine. See
  // diagnostics/routines/cpu_cache for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeCpuCacheRoutine(
      base::TimeDelta exec_duration) = 0;
  // Constructs a new instance of the CPU stress routine. See
  // diagnostics/routines/cpu_stress for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeCpuStressRoutine(
      base::TimeDelta exec_duration) = 0;
  // Constructs a new instance of the floating point accuracy routine. See
  // diagnostics/routines/floating_point for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeFloatingPointAccuracyRoutine(
      base::TimeDelta exec_duration) = 0;
  // Constructs a new instance of the nvme_wear_level routine. See
  // diagnostics/routines/nvme_wear_level for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeNvmeWearLevelRoutine(
      DebugdAdapter* debugd_adapter, uint32_t wear_level_threshold) = 0;
  // Constructs a new instance of the NvmeSelfTest routine. See
  // diagnostics/routines/nvme_self_test for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeNvmeSelfTestRoutine(
      DebugdAdapter* debugd_adapter,
      chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum
          nvme_self_test_type) = 0;
  // Constructs a new instance of the disk read routine. See
  // diagnostics/routines/disk_read for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeDiskReadRoutine(
      chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      base::TimeDelta exec_duration,
      uint32_t file_size_mb) = 0;
  // Constructs a new instance of the prime search routine. See
  // diagnostics/routines/prime_search for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakePrimeSearchRoutine(
      base::TimeDelta exec_duration, uint64_t max_num) = 0;
  // Constructs a new instance of the battery discharge routine. See
  // diagnostics/routines/battery_discharge for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBatteryDischargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t maximum_discharge_percent_allowed) = 0;
  // Constructs a new instance of the battery charge routine. See
  // diagnostics/routines/battery_charge for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBatteryChargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t minimum_charge_percent_required) = 0;
  // Constructs a new instance of the memory routine. See
  // diagnostics/routines/memory for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeMemoryRoutine() = 0;
  // Constructs a new instance of the LAN connectivity routine. See
  // diagnostics/routines/lan_connectivity for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeLanConnectivityRoutine() = 0;
  // Constructs a new instance of the signal strength routine. See
  // diagnostics/routines/signal_strength for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeSignalStrengthRoutine() = 0;
  // Constructs a new instance of the gateway can be pinged routine. See
  // diagnostics/routines/gateway_can_be_pinged for details on the routine
  // itself.
  virtual std::unique_ptr<DiagnosticRoutine>
  MakeGatewayCanBePingedRoutine() = 0;
  // Constructs a new instance of the has secure wifi connection routine. See
  // diagnostics/routines/has_secure_wifi_connection for details on the routine
  // itself.
  virtual std::unique_ptr<DiagnosticRoutine>
  MakeHasSecureWiFiConnectionRoutine() = 0;
  // Constructs a new instance of the DNS resolver present routine. See
  // diagnostics/routines/dns_resolver_present for details on the routine
  // itself.
  virtual std::unique_ptr<DiagnosticRoutine>
  MakeDnsResolverPresentRoutine() = 0;
  // Constructs a new instance of the DNS latency routine. See
  // diagnostics/routines/dns_latency for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeDnsLatencyRoutine() = 0;
  // Constructs a new instance of the DNS resolution routine. See
  // diagnostics/routines/dns_resolution for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeDnsResolutionRoutine() = 0;
  // Constructs a new instance of the captive portal routine. See
  // diagnostics/routines/captive_portal for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeCaptivePortalRoutine() = 0;
  // Constructs a new instance of the HTTP firewall routine. See
  // diagnostics/routines/http_firewall for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeHttpFirewallRoutine() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_FACTORY_H_
