// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/server_proxy.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback_helpers.h>
#include <base/posix/eintr_wrapper.h>
#include <base/files/file_util.h>
#include <base/threading/thread.h>
#include <base/threading/thread_task_runner_handle.h>

#include "bindings/worker_common.pb.h"
#include "system-proxy/protobuf_util.h"

namespace system_proxy {

namespace {
const int kMaxConn = 1000;
}  // namespace

ServerProxy::ServerProxy(base::OnceClosure quit_closure)
    : quit_closure_(std::move(quit_closure)) {}

void ServerProxy::Init() {
  // Start listening for input.
  stdin_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      GetStdinPipe(),
      base::Bind(&ServerProxy::HandleStdinReadable, base::Unretained(this)));

  // Handle termination signals.
  signal_handler_.Init();
  for (int signal : {SIGINT, SIGTERM, SIGHUP, SIGQUIT}) {
    signal_handler_.RegisterHandler(
        signal, base::BindRepeating(&ServerProxy::HandleSignal,
                                    base::Unretained(this)));
  }
}

ServerProxy::~ServerProxy() = default;

void ServerProxy::HandleStdinReadable() {
  WorkerConfigs config;
  if (!ReadProtobuf(GetStdinPipe(), &config)) {
    LOG(ERROR) << "Error decoding protobuf configurations." << std::endl;
    return;
  }

  if (config.has_credentials()) {
    username_ = config.credentials().username();
    password_ = config.credentials().password();
  }

  if (config.has_listening_address()) {
    if (listening_addr_ != 0) {
      LOG(ERROR)
          << "Failure to set configurations: listening port was already set."
          << std::endl;
      return;
    }
    listening_addr_ = config.listening_address().addr();
    listening_port_ = config.listening_address().port();
    CreateListeningSocket();
  }
}

bool ServerProxy::HandleSignal(const struct signalfd_siginfo& siginfo) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                std::move(quit_closure_));
  return true;
}

int ServerProxy::GetStdinPipe() {
  return STDIN_FILENO;
}

void ServerProxy::CreateListeningSocket() {
  listening_fd_ = std::make_unique<arc_networkd::Socket>(
      AF_INET, SOCK_STREAM | SOCK_NONBLOCK);

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(listening_port_);
  addr.sin_addr.s_addr = listening_addr_;
  if (!listening_fd_->Bind((const struct sockaddr*)&addr, sizeof(addr))) {
    LOG(ERROR) << "Cannot bind source socket" << std::endl;
    return;
  }

  if (!listening_fd_->Listen(kMaxConn)) {
    LOG(ERROR) << "Cannot listen on source socket." << std::endl;
    return;
  }

  fd_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      listening_fd_->fd(),
      base::BindRepeating(&ServerProxy::OnConnectionRequest,
                          base::Unretained(this)));
}

void ServerProxy::OnConnectionRequest() {
  struct sockaddr_storage client_src = {};
  socklen_t sockaddr_len = sizeof(client_src);
  if (auto client_conn =
          listening_fd_->Accept((struct sockaddr*)&client_src, &sockaddr_len)) {
    // TODO(acostinas,chromium:1042626): Do curl authentication.
  }
}

}  // namespace system_proxy
