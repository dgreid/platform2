// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/run_loop.h>
#include <base/synchronization/waitable_event.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/file_descriptor.h>
#include <dbus/bus.h>
#include <dbus/cros_healthd/dbus-constants.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/system/invitation.h>

namespace diagnostics {
namespace {
// Saves |response| to |response_destination|.
template <class T>
void OnMojoResponseReceived(T* response_destination,
                            base::Closure quit_closure,
                            T response) {
  *response_destination = std::move(response);
  quit_closure.Run();
}

// Sends |raw_fd| to cros_healthd via D-Bus. Sets |token_out| to a unique token
// which can be used to create a message pipe to cros_healthd.
void DoDBusBootstrap(int raw_fd,
                     base::WaitableEvent* event,
                     std::string* token_out) {
  dbus::Bus::Options bus_options;
  bus_options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(bus_options);

  CHECK(bus->Connect());

  dbus::ObjectProxy* cros_healthd_service_factory_proxy = bus->GetObjectProxy(
      diagnostics::kCrosHealthdServiceName,
      dbus::ObjectPath(diagnostics::kCrosHealthdServicePath));

  brillo::dbus_utils::FileDescriptor fd(raw_fd);
  brillo::ErrorPtr error;
  auto response = brillo::dbus_utils::CallMethodAndBlock(
      cros_healthd_service_factory_proxy, kCrosHealthdServiceInterface,
      kCrosHealthdBootstrapMojoConnectionMethod, &error, fd,
      false /* is_chrome */);

  if (!response) {
    LOG(ERROR) << "No response received.";
    return;
  }

  dbus::MessageReader reader(response.get());
  if (!reader.PopString(token_out)) {
    LOG(ERROR) << "Failed to pop string.";
    return;
  }

  event->Signal();
}
}  // namespace

CrosHealthdMojoAdapter::CrosHealthdMojoAdapter() {
  CHECK(mojo_thread_.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0)))
      << "Failed starting the mojo thread.";

  CHECK(dbus_thread_.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0)))
      << "Failed starting the D-Bus thread.";

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      mojo_thread_.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
}

CrosHealthdMojoAdapter::~CrosHealthdMojoAdapter() = default;

chromeos::cros_healthd::mojom::TelemetryInfoPtr
CrosHealthdMojoAdapter::GetTelemetryInfo(
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
CrosHealthdMojoAdapter::RunUrandomRoutine(uint32_t length_seconds) {
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
CrosHealthdMojoAdapter::RunBatteryCapacityRoutine(uint32_t low_mah,
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
CrosHealthdMojoAdapter::RunBatteryHealthRoutine(
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
CrosHealthdMojoAdapter::RunSmartctlCheckRoutine() {
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
CrosHealthdMojoAdapter::RunAcPowerRoutine(
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
CrosHealthdMojoAdapter::RunCpuCacheRoutine(base::TimeDelta exec_duration) {
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
CrosHealthdMojoAdapter::RunCpuStressRoutine(base::TimeDelta exec_duration) {
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
CrosHealthdMojoAdapter::RunFloatingPointAccuracyRoutine(
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
CrosHealthdMojoAdapter::RunNvmeWearLevelRoutine(uint32_t wear_level_threshold) {
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
CrosHealthdMojoAdapter::RunNvmeSelfTestRoutine(
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
CrosHealthdMojoAdapter::RunDiskReadRoutine(
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
CrosHealthdMojoAdapter::RunPrimeSearchRoutine(base::TimeDelta exec_duration,
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

std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>
CrosHealthdMojoAdapter::GetAvailableRoutines() {
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
CrosHealthdMojoAdapter::GetRoutineUpdate(
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

void CrosHealthdMojoAdapter::Connect() {
  mojo::PlatformChannel channel;
  std::string token;

  // Pass the other end of the pipe to cros_healthd. Wait for this task to run,
  // since we need the resulting token to continue.
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  dbus_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          &DoDBusBootstrap,
          channel.TakeRemoteEndpoint().TakePlatformHandle().TakeFD().release(),
          &event, &token));
  event.Wait();

  mojo::IncomingInvitation invitation =
      mojo::IncomingInvitation::Accept(channel.TakeLocalEndpoint());

  // Bind our end of |pipe| to our CrosHealthdServicePtr. The daemon
  // should bind its end to a CrosHealthdService implementation.
  cros_healthd_service_factory_.Bind(
      chromeos::cros_healthd::mojom::CrosHealthdServiceFactoryPtrInfo(
          invitation.ExtractMessagePipe(token), 0u /* version */));

  // Bind the probe and diagnostics services.
  cros_healthd_service_factory_->GetProbeService(
      mojo::MakeRequest(&cros_healthd_probe_service_));
  cros_healthd_service_factory_->GetDiagnosticsService(
      mojo::MakeRequest(&cros_healthd_diagnostics_service_));
}

}  // namespace diagnostics
