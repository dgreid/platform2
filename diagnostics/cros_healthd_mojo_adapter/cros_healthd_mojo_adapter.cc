// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/optional.h>
#include <base/run_loop.h>
#include <mojo/public/cpp/bindings/interface_request.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter_delegate.h"
#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter_delegate_impl.h"
#include "mojo/cros_healthd.mojom.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/cros_healthd_events.mojom.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

// Provides a mojo connection to cros_healthd. See mojo/cros_healthd.mojom for
// details on cros_healthd's mojo interface. This should only be used by
// processes whose only mojo connection is to cros_healthd.
class CrosHealthdMojoAdapterImpl final : public CrosHealthdMojoAdapter {
 public:
  // Override |delegate| for testing only.
  explicit CrosHealthdMojoAdapterImpl(
      CrosHealthdMojoAdapterDelegate* delegate = nullptr);
  CrosHealthdMojoAdapterImpl(const CrosHealthdMojoAdapterImpl&) = delete;
  CrosHealthdMojoAdapterImpl& operator=(const CrosHealthdMojoAdapterImpl&) =
      delete;
  ~CrosHealthdMojoAdapterImpl() override;

  // Gets telemetry information from cros_healthd.
  chromeos::cros_healthd::mojom::TelemetryInfoPtr GetTelemetryInfo(
      const std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>&
          categories_to_probe) override;

  // Runs the urandom routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunUrandomRoutine(
      uint32_t length_seconds) override;

  // Runs the battery capacity routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryCapacityRoutine(uint32_t low_mah, uint32_t high_mah) override;

  // Runs the battery health routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunBatteryHealthRoutine(
      uint32_t maximum_cycle_count,
      uint32_t percent_battery_wear_allowed) override;

  // Runs the smartctl-check routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunSmartctlCheckRoutine()
      override;

  // Runs the AC power routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type) override;

  // Runs the CPU cache routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunCpuCacheRoutine(
      base::TimeDelta exec_duration) override;

  // Runs the CPU stress routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunCpuStressRoutine(
      base::TimeDelta exec_duration) override;

  // Runs the NvmeWearLevel routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunNvmeWearLevelRoutine(
      uint32_t wear_level_threshold) override;

  // Runs the NvmeSelfTest routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunNvmeSelfTestRoutine(
      chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type)
      override;

  // Runs the disk read routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunDiskReadRoutine(
      chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      base::TimeDelta exec_duration,
      uint32_t file_size_mb) override;

  // Runs the prime search routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunPrimeSearchRoutine(
      base::TimeDelta exec_duration, uint64_t max_num) override;

  // Runs the battery discharge routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryDischargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t maximum_discharge_percent_allowed) override;

  // Returns which routines are available on the platform.
  std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>
  GetAvailableRoutines() override;

  // Gets an update for the specified routine.
  chromeos::cros_healthd::mojom::RoutineUpdatePtr GetRoutineUpdate(
      int32_t id,
      chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
      bool include_output) override;

  // Runs the floating-point-accuracy routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunFloatingPointAccuracyRoutine(base::TimeDelta exec_duration) override;

  // Subscribes the client to Bluetooth events.
  void AddBluetoothObserver(
      chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserverPtr observer)
      override;

  // Subscribes the client to lid events.
  void AddLidObserver(chromeos::cros_healthd::mojom::CrosHealthdLidObserverPtr
                          observer) override;

  // Subscribes the client to power events.
  void AddPowerObserver(
      chromeos::cros_healthd::mojom::CrosHealthdPowerObserverPtr observer)
      override;

 private:
  // Establishes a mojo connection with cros_healthd.
  void Connect();

  // Default delegate implementation.
  std::unique_ptr<CrosHealthdMojoAdapterDelegateImpl> delegate_impl_;
  // Unowned. Must outlive this instance.
  CrosHealthdMojoAdapterDelegate* delegate_;

  // Binds to an implementation of CrosHealthdServiceFactory. The implementation
  // is provided by cros_healthd. Allows calling cros_healthd's mojo factory
  // methods.
  chromeos::cros_healthd::mojom::CrosHealthdServiceFactoryPtr
      cros_healthd_service_factory_;
  // Binds to an implementation of CrosHealthdProbeService. The implementation
  // is provided by cros_healthd. Allows calling cros_healthd's probe-related
  // mojo methods.
  chromeos::cros_healthd::mojom::CrosHealthdProbeServicePtr
      cros_healthd_probe_service_;
  // Binds to an implementation of CrosHealthdDiagnosticsService. The
  // implementation is provided by cros_healthd. Allows calling cros_healthd's
  // diagnostics-related mojo methods.
  chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsServicePtr
      cros_healthd_diagnostics_service_;
  // Binds to an implementation of CrosHealthdEventService. The
  // implementation is provided by cros_healthd. Allows calling cros_healthd's
  // event-related mojo methods.
  chromeos::cros_healthd::mojom::CrosHealthdEventServicePtr
      cros_healthd_event_service_;
};

// Saves |response| to |response_destination|.
template <class T>
void OnMojoResponseReceived(T* response_destination,
                            base::Closure quit_closure,
                            T response) {
  *response_destination = std::move(response);
  quit_closure.Run();
}

CrosHealthdMojoAdapterImpl::CrosHealthdMojoAdapterImpl(
    CrosHealthdMojoAdapterDelegate* delegate) {
  if (delegate) {
    delegate_ = delegate;
  } else {
    delegate_impl_ = std::make_unique<CrosHealthdMojoAdapterDelegateImpl>();
    delegate_ = delegate_impl_.get();
  }
  DCHECK(delegate_);
}

CrosHealthdMojoAdapterImpl::~CrosHealthdMojoAdapterImpl() = default;

chromeos::cros_healthd::mojom::TelemetryInfoPtr
CrosHealthdMojoAdapterImpl::GetTelemetryInfo(
    const std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>&
        categories_to_probe) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::TelemetryInfoPtr response;
  base::RunLoop run_loop;
  cros_healthd_probe_service_->ProbeTelemetryInfo(
      categories_to_probe,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::TelemetryInfoPtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunUrandomRoutine(uint32_t length_seconds) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunUrandomRoutine(
      length_seconds,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunBatteryCapacityRoutine(uint32_t low_mah,
                                                      uint32_t high_mah) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryCapacityRoutine(
      low_mah, high_mah,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunBatteryHealthRoutine(
    uint32_t maximum_cycle_count, uint32_t percent_battery_wear_allowed) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryHealthRoutine(
      maximum_cycle_count, percent_battery_wear_allowed,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunSmartctlCheckRoutine() {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunSmartctlCheckRoutine(
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunAcPowerRoutine(
    chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunAcPowerRoutine(
      expected_status, expected_power_type,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunCpuCacheRoutine(base::TimeDelta exec_duration) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunCpuCacheRoutine(
      exec_duration.InSeconds(),
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunCpuStressRoutine(base::TimeDelta exec_duration) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunCpuStressRoutine(
      exec_duration.InSeconds(),
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunFloatingPointAccuracyRoutine(
    base::TimeDelta exec_duration) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunFloatingPointAccuracyRoutine(
      exec_duration.InSeconds(),
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunNvmeWearLevelRoutine(
    uint32_t wear_level_threshold) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunNvmeWearLevelRoutine(
      wear_level_threshold,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunNvmeSelfTestRoutine(
    chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunNvmeSelfTestRoutine(
      nvme_self_test_type,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunDiskReadRoutine(
    chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
    base::TimeDelta exec_duration,
    uint32_t file_size_mb) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunDiskReadRoutine(
      type, exec_duration.InSeconds(), file_size_mb,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunPrimeSearchRoutine(base::TimeDelta exec_duration,
                                                  uint64_t max_num) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunPrimeSearchRoutine(
      exec_duration.InSeconds(), max_num,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapterImpl::RunBatteryDischargeRoutine(
    base::TimeDelta exec_duration, uint32_t maximum_discharge_percent_allowed) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryDischargeRoutine(
      exec_duration.InSeconds(), maximum_discharge_percent_allowed,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>
CrosHealthdMojoAdapterImpl::GetAvailableRoutines() {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum> response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->GetAvailableRoutines(base::Bind(
      [](std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>* out,
         base::Closure quit_closure,
         const std::vector<
             chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>& routines) {
        *out = routines;
        quit_closure.Run();
      },
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RoutineUpdatePtr
CrosHealthdMojoAdapterImpl::GetRoutineUpdate(
    int32_t id,
    chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
    bool include_output) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RoutineUpdatePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->GetRoutineUpdate(
      id, command, include_output,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RoutineUpdatePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

void CrosHealthdMojoAdapterImpl::AddBluetoothObserver(
    chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserverPtr observer) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  cros_healthd_event_service_->AddBluetoothObserver(std::move(observer));
}

void CrosHealthdMojoAdapterImpl::AddLidObserver(
    chromeos::cros_healthd::mojom::CrosHealthdLidObserverPtr observer) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  cros_healthd_event_service_->AddLidObserver(std::move(observer));
}

void CrosHealthdMojoAdapterImpl::AddPowerObserver(
    chromeos::cros_healthd::mojom::CrosHealthdPowerObserverPtr observer) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  cros_healthd_event_service_->AddPowerObserver(std::move(observer));
}

void CrosHealthdMojoAdapterImpl::Connect() {
  cros_healthd_service_factory_ = delegate_->GetCrosHealthdServiceFactory();

  // Bind the probe, diagnostics and event services.
  cros_healthd_service_factory_->GetProbeService(
      mojo::MakeRequest(&cros_healthd_probe_service_));
  cros_healthd_service_factory_->GetDiagnosticsService(
      mojo::MakeRequest(&cros_healthd_diagnostics_service_));
  cros_healthd_service_factory_->GetEventService(
      mojo::MakeRequest(&cros_healthd_event_service_));
}

}  // namespace

std::unique_ptr<CrosHealthdMojoAdapter> CrosHealthdMojoAdapter::Create() {
  return std::make_unique<CrosHealthdMojoAdapterImpl>();
}

}  // namespace diagnostics
