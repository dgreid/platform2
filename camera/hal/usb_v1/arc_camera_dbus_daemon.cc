/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb_v1/arc_camera_dbus_daemon.h"

#include <signal.h>

#include <memory>
#include <string>
#include <vector>

#include <base/command_line.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "dbus_adaptors/org.chromium.ArcCamera.h"
#include "hal/usb_v1/arc_camera_service.h"

namespace arc {

// DBusAdaptor handles incoming D-Bus method calls.
class ArcCameraDBusDaemon::DBusAdaptor
    : public org::chromium::ArcCameraAdaptor,
      public org::chromium::ArcCameraInterface {
 public:
  explicit DBusAdaptor(scoped_refptr<dbus::Bus> bus)
      : org::chromium::ArcCameraAdaptor(this),
        dbus_object_(
            nullptr, bus, dbus::ObjectPath(arc_camera::kArcCameraServicePath)) {
  }
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  ~DBusAdaptor() override = default;

  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
    RegisterWithDBusObject(&dbus_object_);
    dbus_object_.RegisterAsync(cb);
  }

  // org::chromium::ArcCameraInterface overrides:
  bool StartService(brillo::ErrorPtr* error,
                    const base::ScopedFD& fd,
                    const std::string& token) override {
    VLOG(1) << "Accepted a client, fd: " << fd.get();

    // Fork and exec this executable in child mode.
    // Note: We don't use brillo::ProcessImpl here as we are ignoring SIGCHLD
    // and there is no need to track child processes.
    base::CommandLine new_cl(*base::CommandLine::ForCurrentProcess());
    new_cl.AppendSwitchASCII("child", token);
    std::vector<std::string> argv = new_cl.argv();
    std::vector<const char*> argv_cstr;
    for (auto& arg : argv)
      argv_cstr.push_back(arg.c_str());
    argv_cstr.push_back(nullptr);

    pid_t child_pid = fork();
    if (child_pid < 0) {
      PLOG(ERROR) << "Fork failed";
      return false;
    }
    if (child_pid == 0) {  // child
      // Pass the FD to the child.
      dup2(fd.get(), kMojoChannelFD);
      close(fd.get());
      execv(argv_cstr[0], const_cast<char**>(argv_cstr.data()));
      _exit(EXIT_FAILURE);
    }
    return true;
  }

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
};

ArcCameraDBusDaemon::ArcCameraDBusDaemon()
    : DBusServiceDaemon(arc_camera::kArcCameraServiceName) {
  // Reap zombie processes when child process exited.
  signal(SIGCHLD, SIG_IGN);
}

ArcCameraDBusDaemon::~ArcCameraDBusDaemon() = default;

void ArcCameraDBusDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  adaptor_ = std::make_unique<DBusAdaptor>(bus_);
  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed.", true));
}

}  // namespace arc
