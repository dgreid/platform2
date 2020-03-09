// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SYSTEM_PROXY_SERVER_PROXY_H_
#define SYSTEM_PROXY_SERVER_PROXY_H_

#include <memory>

#include <base/callback_forward.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <brillo/asynchronous_signal_handler.h>

namespace system_proxy {

// ServerProxy listens for connections from the host (system services, ARC++
// apps) and sets-up connections to the remote server.
class ServerProxy {
 public:
  explicit ServerProxy(base::OnceClosure quit_closure);
  ServerProxy(const ServerProxy&) = delete;
  ServerProxy& operator=(const ServerProxy&) = delete;
  ~ServerProxy();

  void Init();

 private:
  void HandleStdinReadable();
  bool HandleSignal(const struct signalfd_siginfo& siginfo);

  base::OnceClosure quit_closure_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> stdin_watcher_;
  brillo::AsynchronousSignalHandler signal_handler_;
};
}  // namespace system_proxy

#endif  // SYSTEM_PROXY_SERVER_PROXY_H_
