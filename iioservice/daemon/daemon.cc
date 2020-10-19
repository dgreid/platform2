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

namespace iioservice {

Daemon::~Daemon() = default;

int Daemon::OnInit() {
  int exit_code = DBusDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  SetBus(bus_.get());
  BootstrapMojoConnection();

  return 0;
}

void Daemon::OnServerReceived(
    mojo::PendingReceiver<cros::mojom::SensorHalServer> server) {
  sensor_hal_server_ = SensorHalServerImpl::Create(
      base::ThreadTaskRunnerHandle::Get(), std::move(server),
      base::Bind(&Daemon::OnMojoDisconnect, weak_factory_.GetWeakPtr()));
}

void Daemon::OnMojoDisconnect() {
  LOGF(WARNING) << "Chromium crashed. Try to establish a new Mojo connection.";
  sensor_hal_server_.reset();
  ReconnectMojoWithDelay();
}

}  // namespace iioservice
