// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SYSTEM_PROXY_SERVER_PROXY_H_
#define SYSTEM_PROXY_SERVER_PROXY_H_

#include <memory>
#include <string>

#include <arc/network/socket.h>
#include <base/callback_forward.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <brillo/asynchronous_signal_handler.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

namespace system_proxy {

// ServerProxy listens for connections from the host (system services, ARC++
// apps) and sets-up connections to the remote server.
// Note: System-proxy only supports proxying over IPv4 networks.
class ServerProxy {
 public:
  explicit ServerProxy(base::OnceClosure quit_closure);
  ServerProxy(const ServerProxy&) = delete;
  ServerProxy& operator=(const ServerProxy&) = delete;
  virtual ~ServerProxy();

  void Init();

 protected:
  virtual int GetStdinPipe();

 private:
  friend class ServerProxyTest;
  FRIEND_TEST(ServerProxyTest, FetchCredentials);
  FRIEND_TEST(ServerProxyTest, FetchListeningAddress);

  void HandleStdinReadable();
  bool HandleSignal(const struct signalfd_siginfo& siginfo);

  void CreateListeningSocket();
  void OnConnectionRequest();

  // The proxy listening address in network-byte order.
  uint32_t listening_addr_ = 0;
  int listening_port_;

  std::string username_;
  std::string password_;

  std::unique_ptr<arc_networkd::Socket> listening_fd_;

  base::OnceClosure quit_closure_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> stdin_watcher_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> fd_watcher_;

  brillo::AsynchronousSignalHandler signal_handler_;
};
}  // namespace system_proxy

#endif  // SYSTEM_PROXY_SERVER_PROXY_H_
