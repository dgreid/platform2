// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_DAEMON_H_
#define RUNTIME_PROBE_DAEMON_H_

#include <brillo/daemons/dbus_daemon.h>
#include <dbus/exported_object.h>

#include "runtime_probe/proto_bindings/runtime_probe.pb.h"

namespace dbus {
class MethodCall;
}  // namespace dbus

namespace runtime_probe {

class Daemon : public brillo::DBusDaemon {
 public:
  Daemon();
  ~Daemon() override;

 protected:
  // brillo::DBusDaemon:
  int OnInit() override;

 private:
  // This function initializes the D-Bus service. Since we expect requests
  // from client occurs as soon as the D-Bus channel is up, this
  // initialization should be the last thing that happens in Daemon::OnInit().
  void InitDBus();

  // Sugar wrapper for packing protocol buffer with error handling.
  void SendProbeResult(const ProbeResult& reply,
                       dbus::MethodCall* method_call,
                       dbus::ExportedObject::ResponseSender response_sender);

  // Handler of org.chromium.ProbeCategories method calls.
  void ProbeCategories(dbus::MethodCall* method_call,
                       dbus::ExportedObject::ResponseSender response_sender);

  // Schedule an asynchronous D-Bus shutdown and exit the daemon.
  void PostQuitTask();

  // Underlying function that performs D-Bus shutdown. This QuitDaemonInternal()
  // is *not* expected to be invoked while we're still processing the method
  // call, because it will create racing on variable |bus_| (i.e.: freed before
  // other usage depends on it.)
  void QuitDaemonInternal();

  // Because members are destructed in reverse-order that they appear in the
  // class definition. Member variables should appear before the WeakPtrFactory,
  // to ensure hat any WeakPtrs are invalidated before its members variable's
  // destructors are executed, making them invalid.
  base::WeakPtrFactory<Daemon> weak_ptr_factory_{this};
};

}  // namespace runtime_probe
#endif  // RUNTIME_PROBE_DAEMON_H_
