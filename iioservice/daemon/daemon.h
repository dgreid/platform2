// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_DAEMON_DAEMON_H_
#define IIOSERVICE_DAEMON_DAEMON_H_

#include <memory>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>
#include <dbus/exported_object.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "iioservice/daemon/sensor_hal_server_impl.h"

namespace iioservice {

class Daemon : public brillo::DBusDaemon {
 public:
  Daemon();
  ~Daemon() override;

 protected:
  // brillo::DBusDaemon:
  int OnInit() override;

 private:
  void ServerBootstrapMojoConnection();
  void ReconnectWithDelay();

  void OnBootstrapResponse(dbus::Response* response);

  void OnMojoDisconnection();

  // IPC Support
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  SensorHalServerImpl::ScopedSensorHalServerImpl sensor_hal_server_ = {
      nullptr, SensorHalServerImpl::SensorHalServerImplDeleter};

  // TODO(chenghaoyang): Add metrics
  // For periodic and on-demand UMA metrics logging.
  // Metrics metrics_;

  // Must be last class member.
  base::WeakPtrFactory<Daemon> weak_ptr_factory_;
};

}  // namespace iioservice

#endif  // IIOSERVICE_DAEMON_DAEMON_H_
