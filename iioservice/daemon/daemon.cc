// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/daemon.h"

#include <sysexits.h>

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/system/invitation.h>

#include "iioservice/daemon/sensor_hal_server_impl.h"
#include "iioservice/include/common.h"

namespace {

constexpr int kDelayBootstrapInMilliseconds = 1000;

}

namespace iioservice {

Daemon::Daemon() : weak_ptr_factory_(this) {}

Daemon::~Daemon() {}

int Daemon::OnInit() {
  int exit_code = DBusDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
  ServerBootstrapMojoConnection();

  return 0;
}

void Daemon::ServerBootstrapMojoConnection() {
  // Get or create the ExportedObject for the IIO service.
  dbus::ObjectProxy* proxy = bus_->GetObjectProxy(
      ::mojo_connection_service::kMojoConnectionServiceServiceName,
      dbus::ObjectPath(
          ::mojo_connection_service::kMojoConnectionServiceServicePath));

  dbus::MethodCall method_call(
      ::mojo_connection_service::kMojoConnectionServiceInterface,
      ::mojo_connection_service::kBootstrapMojoConnectionForIioServiceMethod);
  dbus::MessageWriter writer(&method_call);
  proxy->CallMethod(&method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                    base::BindOnce(&Daemon::OnBootstrapResponse,
                                   weak_ptr_factory_.GetWeakPtr()));
}

void Daemon::ReconnectWithDelay() {
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Daemon::ServerBootstrapMojoConnection,
                     weak_ptr_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(kDelayBootstrapInMilliseconds));
}

void Daemon::OnBootstrapResponse(dbus::Response* response) {
  DCHECK(!sensor_hal_server_.get());

  if (!response) {
    LOG(ERROR) << ::mojo_connection_service::kMojoConnectionServiceServiceName
               << " D-Bus call to "
               << ::mojo_connection_service::
                      kBootstrapMojoConnectionForIioServiceMethod
               << " failed";
    ReconnectWithDelay();
    return;
  }

  base::ScopedFD file_handle;
  dbus::MessageReader reader(response);

  if (!reader.PopFileDescriptor(&file_handle)) {
    LOG(ERROR) << "Couldn't extract file descriptor from D-Bus call";
    ReconnectWithDelay();
    return;
  }

  if (!file_handle.is_valid()) {
    LOG(ERROR) << "ScopedFD extracted from D-Bus call was invalid (i.e. empty)";
    ReconnectWithDelay();
    return;
  }

  if (!base::SetCloseOnExec(file_handle.get())) {
    PLOG(ERROR) << "Failed setting FD_CLOEXEC on file descriptor";
    ReconnectWithDelay();
    return;
  }

  // Connect to mojo in the requesting process.
  mojo::IncomingInvitation invitation =
      mojo::IncomingInvitation::Accept(mojo::PlatformChannelEndpoint(
          mojo::PlatformHandle(std::move(file_handle))));

  LOGF(INFO) << "Broker connected";

  // Bind primordial message pipe to a SensorHalServer implementation.
  sensor_hal_server_ = SensorHalServerImpl::Create(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::PendingReceiver<cros::mojom::SensorHalServer>(
          invitation.ExtractMessagePipe(
              ::mojo_connection_service::
                  kBootstrapMojoConnectionForIioServiceChannelToken)),
      base::Bind(&Daemon::OnMojoDisconnection, weak_ptr_factory_.GetWeakPtr()));
}

void Daemon::OnMojoDisconnection() {
  LOGF(WARNING) << "Chromium crashes. Try to establish a new DBus connection.";
  sensor_hal_server_.reset();
  ReconnectWithDelay();
}

}  // namespace iioservice
