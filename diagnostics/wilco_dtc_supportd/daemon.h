// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_DAEMON_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_DAEMON_H_

#include <memory>

#include <base/macros.h>
#include <base/timer/timer.h>
#include <brillo/daemons/dbus_daemon.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "diagnostics/wilco_dtc_supportd/core.h"
#include "diagnostics/wilco_dtc_supportd/core_delegate_impl.h"
#include "diagnostics/wilco_dtc_supportd/grpc_client_manager.h"

namespace diagnostics {

// Daemon class for the wilco_dtc_supportd daemon.
class Daemon final : public brillo::DBusServiceDaemon {
 public:
  Daemon();
  ~Daemon() override;

 private:
  // brillo::DBusServiceDaemon overrides:
  int OnInit() override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;
  void OnShutdown(int* error_code) override;

  // Forces shutting down the whole process if the graceful shutdown wasn't
  // done within timeout.
  void ForceShutdown();

  GrpcClientManager grpc_client_manager_;
  CoreDelegateImpl wilco_dtc_supportd_core_delegate_impl_{this /* daemon */};
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  Core wilco_dtc_supportd_core_;

  base::OneShotTimer force_shutdown_timer_;

  DISALLOW_COPY_AND_ASSIGN(Daemon);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_DAEMON_H_
