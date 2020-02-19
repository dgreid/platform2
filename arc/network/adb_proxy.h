// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_ADB_PROXY_H_
#define ARC_NETWORK_ADB_PROXY_H_

#include <deque>
#include <memory>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/memory/weak_ptr.h>
#include <brillo/daemons/daemon.h>

#include "arc/network/message_dispatcher.h"
#include "arc/network/socket.h"
#include "arc/network/socket_forwarder.h"

namespace arc_networkd {

// Subprocess for proxying ADB traffic.
class AdbProxy : public brillo::Daemon {
 public:
  explicit AdbProxy(base::ScopedFD control_fd);
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
  DISALLOW_COPY_AND_ASSIGN(AdbProxy);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_ADB_PROXY_H_
