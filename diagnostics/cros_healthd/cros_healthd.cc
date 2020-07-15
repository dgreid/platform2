// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd.h"

#include <cstdlib>
#include <memory>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/unguessable_token.h>
#include <dbus/cros_healthd/dbus-constants.h>
#include <dbus/object_path.h>
#include <mojo/public/cpp/bindings/interface_request.h>
#include <mojo/public/cpp/platform/platform_channel_endpoint.h>
#include <mojo/public/cpp/system/invitation.h>
#include "diagnostics/cros_healthd/cros_healthd_routine_service_impl.h"
#include "diagnostics/cros_healthd/events/bluetooth_events_impl.h"
#include "diagnostics/cros_healthd/events/lid_events_impl.h"
#include "diagnostics/cros_healthd/events/power_events_impl.h"

namespace diagnostics {

CrosHealthd::CrosHealthd(Context* context)
    : DBusServiceDaemon(kCrosHealthdServiceName /* service_name */),
      context_(context) {
  DCHECK(context_);

  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get() /* io_thread_task_runner */,
      mojo::core::ScopedIPCSupport::ShutdownPolicy::
          CLEAN /* blocking shutdown */);

  CHECK(context_->Initialize()) << "Failed to initialize context.";

  backlight_fetcher_ = std::make_unique<BacklightFetcher>(context_);

  battery_fetcher_ = std::make_unique<BatteryFetcher>(context_);

  bluetooth_fetcher_ = std::make_unique<BluetoothFetcher>(context_);

  cpu_fetcher_ = std::make_unique<CpuFetcher>(context_);

  disk_fetcher_ = std::make_unique<DiskFetcher>();

  fan_fetcher_ = std::make_unique<FanFetcher>(context_);

  system_fetcher_ = std::make_unique<SystemFetcher>(context_);

  bluetooth_events_ = std::make_unique<BluetoothEventsImpl>(context_);

  lid_events_ = std::make_unique<LidEventsImpl>(context_);

  power_events_ = std::make_unique<PowerEventsImpl>(context_);

  routine_service_ = std::make_unique<CrosHealthdRoutineServiceImpl>(
      context_, &routine_factory_impl_);

  mojo_service_ = std::make_unique<CrosHealthdMojoService>(
      backlight_fetcher_.get(), battery_fetcher_.get(),
      bluetooth_fetcher_.get(), cpu_fetcher_.get(), disk_fetcher_.get(),
      fan_fetcher_.get(), system_fetcher_.get(), bluetooth_events_.get(),
      lid_events_.get(), power_events_.get(), routine_service_.get());

  binding_set_.set_connection_error_handler(
      base::Bind(&CrosHealthd::OnDisconnect, base::Unretained(this)));
}

CrosHealthd::~CrosHealthd() = default;

int CrosHealthd::OnInit() {
  VLOG(0) << "Starting";
  return DBusServiceDaemon::OnInit();
}

void CrosHealthd::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  DCHECK(!dbus_object_);
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      nullptr /* object_manager */, bus_,
      dbus::ObjectPath(kCrosHealthdServicePath));
  brillo::dbus_utils::DBusInterface* dbus_interface =
      dbus_object_->AddOrGetInterface(kCrosHealthdServiceInterface);
  DCHECK(dbus_interface);
  dbus_interface->AddSimpleMethodHandler(
      kCrosHealthdBootstrapMojoConnectionMethod, base::Unretained(this),
      &CrosHealthd::BootstrapMojoConnection);
  dbus_object_->RegisterAsync(sequencer->GetHandler(
      "Failed to register D-Bus object" /* descriptive_message */,
      true /* failure_is_fatal */));
}

std::string CrosHealthd::BootstrapMojoConnection(const base::ScopedFD& mojo_fd,
                                                 bool is_chrome) {
  VLOG(1) << "Received BootstrapMojoConnection D-Bus request";

  if (!mojo_fd.is_valid()) {
    constexpr char kInvalidFileDescriptorError[] =
        "Invalid Mojo file descriptor";
    LOG(ERROR) << kInvalidFileDescriptorError;
    return kInvalidFileDescriptorError;
  }

  // We need a file descriptor that stays alive after the current method
  // finishes, but libbrillo's D-Bus wrappers currently don't support passing
  // base::ScopedFD by value.
  base::ScopedFD mojo_fd_copy(HANDLE_EINTR(dup(mojo_fd.get())));
  if (!mojo_fd_copy.is_valid()) {
    constexpr char kFailedDuplicationError[] =
        "Failed to duplicate the Mojo file descriptor";
    PLOG(ERROR) << kFailedDuplicationError;
    return kFailedDuplicationError;
  }

  if (!base::SetCloseOnExec(mojo_fd_copy.get())) {
    constexpr char kFailedSettingFdCloexec[] =
        "Failed to set FD_CLOEXEC on Mojo file descriptor";
    PLOG(ERROR) << kFailedSettingFdCloexec;
    return kFailedSettingFdCloexec;
  }

  std::string token;

  chromeos::cros_healthd::mojom::CrosHealthdServiceFactoryRequest request;
  if (is_chrome) {
    if (mojo_service_bind_attempted_) {
      // This should not normally be triggered, since the other endpoint - the
      // browser process - should bootstrap the Mojo connection only once, and
      // when that process is killed the Mojo shutdown notification should have
      // been received earlier. But handle this case to be on the safe side.
      // After we restart, the browser process is expected to invoke the
      // bootstrapping again.
      ShutDownDueToMojoError(
          "Repeated Mojo bootstrap request received" /* debug_reason */);
      // It doesn't matter what we return here, this is just to satisfy the
      // compiler. ShutDownDueToMojoError will kill cros_healthd.
      return "";
    }

    // Connect to mojo in the requesting process.
    mojo::IncomingInvitation invitation =
        mojo::IncomingInvitation::Accept(mojo::PlatformChannelEndpoint(
            mojo::PlatformHandle(std::move(mojo_fd_copy))));
    request = mojo::InterfaceRequest<
        chromeos::cros_healthd::mojom::CrosHealthdServiceFactory>(
        invitation.ExtractMessagePipe(kCrosHealthdMojoConnectionChannelToken));
    mojo_service_bind_attempted_ = true;
  } else {
    // Create a unique token which will allow the requesting process to connect
    // to us via mojo.
    mojo::OutgoingInvitation invitation;
    token = base::UnguessableToken::Create().ToString();
    mojo::ScopedMessagePipeHandle pipe = invitation.AttachMessagePipe(token);

    mojo::OutgoingInvitation::Send(
        std::move(invitation), base::kNullProcessHandle,
        mojo::PlatformChannelEndpoint(
            mojo::PlatformHandle(std::move(mojo_fd_copy))));
    request = mojo::InterfaceRequest<
        chromeos::cros_healthd::mojom::CrosHealthdServiceFactory>(
        std::move(pipe));
  }
  binding_set_.AddBinding(this /* impl */, std::move(request), is_chrome);

  VLOG(1) << "Successfully bootstrapped Mojo connection";
  return token;
}

void CrosHealthd::GetProbeService(
    chromeos::cros_healthd::mojom::CrosHealthdProbeServiceRequest service) {
  mojo_service_->AddProbeBinding(std::move(service));
}

void CrosHealthd::GetDiagnosticsService(
    chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsServiceRequest
        service) {
  mojo_service_->AddDiagnosticsBinding(std::move(service));
}

void CrosHealthd::GetEventService(
    chromeos::cros_healthd::mojom::CrosHealthdEventServiceRequest service) {
  mojo_service_->AddEventBinding(std::move(service));
}

void CrosHealthd::ShutDownDueToMojoError(const std::string& debug_reason) {
  // Our daemon has to be restarted to be prepared for future Mojo connection
  // bootstraps. We can't do this without a restart since Mojo EDK gives no
  // guarantees it will support repeated bootstraps. Therefore, tear down and
  // exit from our process and let upstart restart us again.
  LOG(ERROR) << "Shutting down due to: " << debug_reason;
  mojo_service_.reset();
  Quit();
}

void CrosHealthd::OnDisconnect() {
  // Only respond to disconnects caused by the browser. All others are
  // recoverable.
  if (binding_set_.dispatch_context())
    ShutDownDueToMojoError("Lost mojo connection to browser.");
}

}  // namespace diagnostics
