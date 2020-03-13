// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_SOCKET_FORWARDER_H_
#define ARC_NETWORK_SOCKET_FORWARDER_H_

#include <netinet/ip.h>
#include <sys/socket.h>

#include <atomic>
#include <memory>
#include <string>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/threading/simple_thread.h>
#include <brillo/brillo_export.h>

#include "arc/network/socket.h"

namespace arc_networkd {
// Forwards data between a pair of sockets.
// This is a simple implementation as a thread main function.
class BRILLO_EXPORT SocketForwarder : public base::SimpleThread {
 public:
  SocketForwarder(const std::string& name,
                  std::unique_ptr<Socket> sock0,
                  std::unique_ptr<Socket> sock1);
  virtual ~SocketForwarder();

  // Runs the forwarder. The sockets are closed and released on exit,
  // so this can only be run once.
  void Run() override;
  bool IsRunning() const;

 private:
  static constexpr int kBufSize = 4096;

  void Poll();
  bool ProcessEvents(uint32_t events, int efd, int cfd);

  std::unique_ptr<Socket> sock0_;
  std::unique_ptr<Socket> sock1_;
  char buf0_[kBufSize] = {0};
  char buf1_[kBufSize] = {0};
  ssize_t len0_;
  ssize_t len1_;

  std::atomic<bool> poll_;
  std::atomic<bool> done_;

  DISALLOW_COPY_AND_ASSIGN(SocketForwarder);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_SOCKET_FORWARDER_H_
