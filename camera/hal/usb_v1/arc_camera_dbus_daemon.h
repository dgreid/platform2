/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_V1_ARC_CAMERA_DBUS_DAEMON_H_
#define CAMERA_HAL_USB_V1_ARC_CAMERA_DBUS_DAEMON_H_

#include <memory>

#include <base/macros.h>
#include <brillo/daemons/dbus_daemon.h>

namespace arc {

// ArcCameraDBusDaemon is a D-Bus daemon which accepts a new connection and
// forks a child process to do mojo connection to connect to container. The
// child process is run as mojo child.
// The child process exits when mojo connection is gone. When upstart stops
// arc-camera, SIGTERM is sent to the process group of the main process and
// all child processes will be killed as well.
class ArcCameraDBusDaemon : public brillo::DBusServiceDaemon {
 public:
  // File descriptor used to pass the mojo channel to child processes.
  static constexpr int kMojoChannelFD = 3;

  ArcCameraDBusDaemon();
  ArcCameraDBusDaemon(const ArcCameraDBusDaemon&) = delete;
  ArcCameraDBusDaemon& operator=(const ArcCameraDBusDaemon&) = delete;

  ~ArcCameraDBusDaemon() override;

 protected:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

 private:
  class DBusAdaptor;

  std::unique_ptr<DBusAdaptor> adaptor_;
};

}  // namespace arc

#endif  // CAMERA_HAL_USB_V1_ARC_CAMERA_DBUS_DAEMON_H_
