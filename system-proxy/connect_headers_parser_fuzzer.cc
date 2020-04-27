// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <brillo/message_loops/base_message_loop.h>
#include <chromeos/patchpanel/socket.h>
#include <chromeos/patchpanel/socket_forwarder.h>

#include "system-proxy/proxy_connect_job.h"

namespace {
void NullProxyResolver(
    const std::string&,
    base::OnceCallback<void(const std::list<std::string>&)>) {}

void OnConnectionSetupFinished(base::OnceClosure quit_task,
                               std::unique_ptr<patchpanel::SocketForwarder>,
                               system_proxy::ProxyConnectJob*) {
  std::move(quit_task).Run();
}
}  // namespace

struct Environment {
  Environment() {
    logging::SetMinLogLevel(logging::LOG_INFO);  // Disable logging.
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  // Mock main task runner
  base::MessageLoopForIO message_loop;
  brillo::BaseMessageLoop brillo_loop(&message_loop);
  brillo_loop.SetAsCurrent();

  base::RunLoop run_loop;

  int socket_pair[2];
  socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, socket_pair);
  base::ScopedFD reader_fd(socket_pair[0]);
  base::ScopedFD writer_fd(socket_pair[1]);
  int fds[2];

  socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
             0 /* protocol */, fds);
  patchpanel::Socket cros_client_socket((base::ScopedFD(fds[1])));

  auto connect_job = std::make_unique<system_proxy::ProxyConnectJob>(
      std::make_unique<patchpanel::Socket>(base::ScopedFD(fds[0])), "",
      base::BindOnce(&NullProxyResolver),
      base::BindOnce(&OnConnectionSetupFinished, run_loop.QuitClosure()));
  connect_job->Start();
  cros_client_socket.SendTo(data, size);

  run_loop.Run();
  return 0;
}
