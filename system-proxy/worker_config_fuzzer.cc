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
#include <libprotobuf-mutator/src/libfuzzer/libfuzzer_macro.h>

#include "bindings/worker_common.pb.h"
#include "system-proxy/protobuf_util.h"
#include "system-proxy/server_proxy.h"

namespace {
void NullClosure() {}
}  // namespace

struct Environment {
  Environment() {
    logging::SetMinLogLevel(logging::LOG_FATAL);  // Disable logging.
  }
};

// ServerProxy implementation that receives input from a given file descriptor,
// instead of the default standard input file descriptor (STDIN_FILENO).
class FakeServerProxy : public system_proxy::ServerProxy {
 public:
  explicit FakeServerProxy(base::ScopedFD stdin_fd)
      : system_proxy::ServerProxy(base::BindOnce(&NullClosure)),
        stdin_fd_(std::move(stdin_fd)) {}
  FakeServerProxy(const FakeServerProxy&) = delete;
  FakeServerProxy& operator=(const FakeServerProxy&) = delete;
  ~FakeServerProxy() override = default;

  int GetStdinPipe() override { return stdin_fd_.get(); }

 private:
  base::ScopedFD stdin_fd_;
};

DEFINE_PROTO_FUZZER(const system_proxy::WorkerConfigs& configs) {
  static Environment env;

  // Mock main task runner
  base::MessageLoopForIO message_loop;
  brillo::BaseMessageLoop brillo_loop(&message_loop);
  brillo_loop.SetAsCurrent();

  int fds[2];
  CHECK(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD stdin_read_fd(fds[0]);
  base::ScopedFD stdin_write_fd(fds[1]);

  auto server = std::make_unique<FakeServerProxy>(std::move(stdin_read_fd));

  // Send the config to the worker's stdin input.
  WriteProtobuf(stdin_write_fd.get(), configs);
  base::RunLoop().RunUntilIdle();
}
