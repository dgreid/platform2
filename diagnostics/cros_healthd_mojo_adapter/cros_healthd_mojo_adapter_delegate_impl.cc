// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter_delegate_impl.h"

#include <string>

#include <base/logging.h>
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

CrosHealthdMojoAdapterDelegateImpl::CrosHealthdMojoAdapterDelegateImpl() {
  CHECK(mojo_thread_.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0)))
      << "Failed starting the mojo thread.";

  CHECK(dbus_thread_.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0)))
      << "Failed starting the D-Bus thread.";

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      mojo_thread_.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
}

CrosHealthdMojoAdapterDelegateImpl::~CrosHealthdMojoAdapterDelegateImpl() =
    default;

chromeos::cros_healthd::mojom::CrosHealthdServiceFactoryPtr
CrosHealthdMojoAdapterDelegateImpl::GetCrosHealthdServiceFactory() {
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
  chromeos::cros_healthd::mojom::CrosHealthdServiceFactoryPtr service_ptr;
  service_ptr.Bind(
      chromeos::cros_healthd::mojom::CrosHealthdServiceFactoryPtrInfo(
          invitation.ExtractMessagePipe(token), 0u /* version */));

  return service_ptr;
}

}  // namespace diagnostics
