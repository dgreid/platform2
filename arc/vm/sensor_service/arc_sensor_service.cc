// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/command_line.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/threading/thread.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>

#include "arc/vm/sensor_service/dbus_adaptors/org.chromium.ArcSensorService.h"

namespace {

class DBusAdaptor : public org::chromium::ArcSensorServiceAdaptor,
                    public org::chromium::ArcSensorServiceInterface {
 public:
  explicit DBusAdaptor(scoped_refptr<dbus::Bus> bus)
      : org::chromium::ArcSensorServiceAdaptor(this),
        dbus_object_(nullptr, bus, GetObjectPath()) {}

  ~DBusAdaptor() override = default;

  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
    RegisterWithDBusObject(&dbus_object_);
    dbus_object_.RegisterAsync(cb);
  }

  // org::chromium::ArcSensorServiceInterface overrides:
  bool BootstrapMojoConnection(brillo::ErrorPtr* error,
                               const base::ScopedFD& in_handle,
                               const std::string& in_token) override {
    // TODO(hashimoto): Accept the mojo invitation and use the attached pipe.
    return true;
  }

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
};

class Daemon : public brillo::DBusServiceDaemon {
 public:
  Daemon() : DBusServiceDaemon(arc::sensor::kArcSensorServiceServiceName) {}
  ~Daemon() override = default;

  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

 protected:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    adaptor_ = std::make_unique<DBusAdaptor>(bus_);
    adaptor_->RegisterAsync(
        sequencer->GetHandler("RegisterAsync() failed.", true));
  }

 private:
  std::unique_ptr<DBusAdaptor> adaptor_;
};

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  base::Thread mojo_ipc_thread("mojo IPC thread");
  CHECK(mojo_ipc_thread.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0)));

  return Daemon().Run();
}
