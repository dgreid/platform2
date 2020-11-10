// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_VM_VSOCK_PROXY_SERVER_PROXY_H_
#define ARC_VM_VSOCK_PROXY_SERVER_PROXY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/memory/ref_counted.h>

#include "arc/vm/vsock_proxy/message_stream.h"
#include "arc/vm/vsock_proxy/proxy_file_system.h"
#include "arc/vm/vsock_proxy/vsock_proxy.h"

namespace arc {

class ProxyFileSystem;

// ServerProxy sets up the VSockProxy and handles initial socket negotiation.
class ServerProxy : public VSockProxy::Delegate,
                    public ProxyFileSystem::Delegate {
 public:
  ServerProxy(scoped_refptr<base::TaskRunner> proxy_file_system_task_runner,
              const base::FilePath& proxy_file_system_mount_path,
              base::OnceClosure quit_closure);
  ServerProxy(const ServerProxy&) = delete;
  ServerProxy& operator=(const ServerProxy&) = delete;

  ~ServerProxy() override;

  // Sets up the ServerProxy. Specifically, start listening VSOCK.
  // Then, connect to /run/chrome/arc_bridge.sock, when an initial connection
  // comes to the vsock.
  bool Initialize();

  // VSockProxy::Delegate overrides:
  VSockProxy::Type GetType() const override { return VSockProxy::Type::SERVER; }
  int GetPollFd() override { return message_stream_->Get(); }
  base::ScopedFD CreateProxiedRegularFile(int64_t handle,
                                          int32_t flags) override;
  bool SendMessage(const arc_proxy::VSockMessage& message,
                   const std::vector<base::ScopedFD>& fds) override;
  bool ReceiveMessage(arc_proxy::VSockMessage* message,
                      std::vector<base::ScopedFD>* fds) override;
  void OnStopped() override;

  // ProxyFileSystem::Delegate overrides:
  void Pread(int64_t handle,
             uint64_t count,
             uint64_t offset,
             PreadCallback callback) override;
  void Pwrite(int64_t handle,
              std::string blob,
              uint64_t offset,
              PwriteCallback callback) override;
  void Close(int64_t handle) override;
  void Fstat(int64_t handle, FstatCallback callback) override;

 private:
  scoped_refptr<base::TaskRunner> proxy_file_system_task_runner_;
  ProxyFileSystem proxy_file_system_;
  base::OnceClosure quit_closure_;
  base::ScopedFD virtwl_socket_;
  base::ScopedFD virtwl_context_;
  bool guest_is_using_vsock_ = false;
  std::unique_ptr<MessageStream> message_stream_;
  std::unique_ptr<VSockProxy> vsock_proxy_;
};

}  // namespace arc

#endif  // ARC_VM_VSOCK_PROXY_SERVER_PROXY_H_
