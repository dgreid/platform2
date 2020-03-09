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

ServerProxy::ServerProxy(base::OnceClosure quit_closure)
    : quit_closure_(std::move(quit_closure)) {}

void ServerProxy::Init() {
  // Start listening for input.
  stdin_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      STDIN_FILENO,
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

  if (!ReadProtobuf(STDIN_FILENO, &config)) {
    std::string error = "Error decoding protobuf configurations\n";
    base::WriteFileDescriptor(STDERR_FILENO, error.data(), error.size());
    return;
  }
}

bool ServerProxy::HandleSignal(const struct signalfd_siginfo& siginfo) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                std::move(quit_closure_));
  return true;
}

}  // namespace system_proxy
