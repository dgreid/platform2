// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_ADB_PROXY_H_
#define PATCHPANEL_ADB_PROXY_H_

#include <deque>
#include <memory>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/memory/weak_ptr.h>
#include <brillo/daemons/daemon.h>

#include "patchpanel/message_dispatcher.h"
#include "patchpanel/socket.h"
#include "patchpanel/socket_forwarder.h"

namespace patchpanel {

// Running the proxy on port 5555 will cause ADBD to see it as an Android
// emulator rather than an attached device. This means, whenever host ADBD
// server runs a device named "emulator-5554" will show up.
// Connections to ARC via ADB (including by Tast) should now be done by
// starting ADB server (e.g. 'adb devices') instead of
// 'adb connect 127.0.0.1:5555' to avoid seeing multiple devices.
constexpr uint16_t kAdbProxyTcpListenPort = 5555;

// Subprocess for proxying ADB traffic.
class AdbProxy : public brillo::Daemon {
 public:
  explicit AdbProxy(base::ScopedFD control_fd);
  AdbProxy(const AdbProxy&) = delete;
  AdbProxy& operator=(const AdbProxy&) = delete;

  virtual ~AdbProxy();

 protected:
  int OnInit() override;

  void OnParentProcessExit();
  void OnGuestMessage(const GuestMessage& msg);

 private:
  void Reset();
  void OnFileCanReadWithoutBlocking();

  // Attempts to establish a connection to ADB at well-known destinations.
  std::unique_ptr<Socket> Connect() const;

  MessageDispatcher msg_dispatcher_;
  std::unique_ptr<Socket> src_;
  std::deque<std::unique_ptr<SocketForwarder>> fwd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> src_watcher_;

  GuestMessage::GuestType arc_type_;
  uint32_t arcvm_vsock_cid_;

  base::WeakPtrFactory<AdbProxy> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_ADB_PROXY_H_
