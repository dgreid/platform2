// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/core_delegate_impl.h"

#include <utility>

#include <base/logging.h>
#include <dbus/wilco_dtc_supportd/dbus-constants.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/interface_request.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/platform_handle.h>

#include "debugd/dbus-proxies.h"
#include "diagnostics/common/system/debugd_adapter_impl.h"
#include "diagnostics/common/system/powerd_adapter_impl.h"
#include "diagnostics/wilco_dtc_supportd/system/bluetooth_client_impl.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/bluetooth_event_service_impl.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/ec_event_service.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/powerd_event_service_impl.h"
#include "mojo/wilco_dtc_supportd.mojom.h"

namespace diagnostics {

using MojomWilcoDtcSupportdServiceFactory =
    chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceFactory;

CoreDelegateImpl::CoreDelegateImpl(brillo::Daemon* daemon) : daemon_(daemon) {
  DCHECK(daemon_);
}

CoreDelegateImpl::~CoreDelegateImpl() = default;

std::unique_ptr<mojo::Binding<MojomWilcoDtcSupportdServiceFactory>>
CoreDelegateImpl::BindMojoServiceFactory(
    MojomWilcoDtcSupportdServiceFactory* mojo_service_factory,
    base::ScopedFD mojo_pipe_fd) {
  DCHECK(mojo_pipe_fd.is_valid());

  mojo::IncomingInvitation invitation =
      mojo::IncomingInvitation::Accept(mojo::PlatformChannelEndpoint(
          mojo::PlatformHandle(std::move(mojo_pipe_fd))));

  mojo::ScopedMessagePipeHandle mojo_pipe_handle =
      invitation.ExtractMessagePipe(
          kWilcoDtcSupportdMojoConnectionChannelToken);
  if (!mojo_pipe_handle.is_valid()) {
    LOG(ERROR) << "Failed to create Mojo child message pipe";
    return nullptr;
  }

  return std::make_unique<mojo::Binding<MojomWilcoDtcSupportdServiceFactory>>(
      mojo_service_factory,
      mojo::InterfaceRequest<MojomWilcoDtcSupportdServiceFactory>(
          std::move(mojo_pipe_handle)));
}

void CoreDelegateImpl::BeginDaemonShutdown() {
  daemon_->Quit();
}

std::unique_ptr<BluetoothClient> CoreDelegateImpl::CreateBluetoothClient(
    const scoped_refptr<dbus::Bus>& bus) {
  DCHECK(bus);
  return std::make_unique<BluetoothClientImpl>(bus);
}

std::unique_ptr<DebugdAdapter> CoreDelegateImpl::CreateDebugdAdapter(
    const scoped_refptr<dbus::Bus>& bus) {
  DCHECK(bus);
  return std::make_unique<DebugdAdapterImpl>(
      std::make_unique<org::chromium::debugdProxy>(bus));
}

std::unique_ptr<PowerdAdapter> CoreDelegateImpl::CreatePowerdAdapter(
    const scoped_refptr<dbus::Bus>& bus) {
  DCHECK(bus);
  return std::make_unique<PowerdAdapterImpl>(bus);
}

std::unique_ptr<BluetoothEventService>
CoreDelegateImpl::CreateBluetoothEventService(
    BluetoothClient* bluetooth_client) {
  DCHECK(bluetooth_client);
  return std::make_unique<BluetoothEventServiceImpl>(bluetooth_client);
}

std::unique_ptr<EcEventService> CoreDelegateImpl::CreateEcEventService() {
  return std::make_unique<EcEventService>();
}

std::unique_ptr<PowerdEventService> CoreDelegateImpl::CreatePowerdEventService(
    PowerdAdapter* powerd_adapter) {
  DCHECK(powerd_adapter);
  return std::make_unique<PowerdEventServiceImpl>(powerd_adapter);
}

}  // namespace diagnostics
