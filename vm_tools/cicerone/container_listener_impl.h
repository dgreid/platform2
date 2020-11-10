// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CICERONE_CONTAINER_LISTENER_IMPL_H_
#define VM_TOOLS_CICERONE_CONTAINER_LISTENER_IMPL_H_

#include <stdint.h>

#include <string>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/sequenced_task_runner.h>
#include <base/time/time.h>
#include <grpcpp/grpcpp.h>
#include <vm_protos/proto_bindings/container_host.grpc.pb.h>

namespace vm_tools {
namespace cicerone {

class Service;

// gRPC server implementation for receiving messages from a container in a VM.
class ContainerListenerImpl final
    : public vm_tools::container::ContainerListener::Service {
 public:
  explicit ContainerListenerImpl(
      base::WeakPtr<vm_tools::cicerone::Service> service);
  ContainerListenerImpl(const ContainerListenerImpl&) = delete;
  ContainerListenerImpl& operator=(const ContainerListenerImpl&) = delete;

  ~ContainerListenerImpl() override = default;

  // Pretend that every service call comes from |testing_peer_address| instead
  // of ctx->peer().
  void OverridePeerAddressForTesting(const std::string& testing_peer_address);

  // ContainerListener overrides.
  grpc::Status ContainerReady(
      grpc::ServerContext* ctx,
      const vm_tools::container::ContainerStartupInfo* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status ContainerShutdown(
      grpc::ServerContext* ctx,
      const vm_tools::container::ContainerShutdownInfo* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status PendingUpdateApplicationListCalls(
      grpc::ServerContext* ctx,
      const vm_tools::container::PendingAppListUpdateCount* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status UpdateApplicationList(
      grpc::ServerContext* ctx,
      const vm_tools::container::UpdateApplicationListRequest* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status OpenUrl(grpc::ServerContext* ctx,
                       const vm_tools::container::OpenUrlRequest* request,
                       vm_tools::EmptyMessage* response) override;
  grpc::Status InstallLinuxPackageProgress(
      grpc::ServerContext* ctx,
      const vm_tools::container::InstallLinuxPackageProgressInfo* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status UninstallPackageProgress(
      grpc::ServerContext* ctx,
      const vm_tools::container::UninstallPackageProgressInfo* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status ApplyAnsiblePlaybookProgress(
      grpc::ServerContext* ctx,
      const vm_tools::container::ApplyAnsiblePlaybookProgressInfo* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status OpenTerminal(
      grpc::ServerContext* ctx,
      const vm_tools::container::OpenTerminalRequest* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status UpdateMimeTypes(
      grpc::ServerContext* ctx,
      const vm_tools::container::UpdateMimeTypesRequest* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status FileWatchTriggered(
      grpc::ServerContext* ctx,
      const vm_tools::container::FileWatchTriggeredInfo* request,
      vm_tools::EmptyMessage* response) override;

 private:
  // Returns 0 on failure, otherwise the parsed vsock cid from a
  // vsock:cid:port string from ctx->peer()
  uint32_t ExtractCidFromPeerAddress(grpc::ServerContext* ctx);

  // Returns true if the performing an open window/tab operation will be within
  // the rules for rate limiting, false if it should be blocked. This will also
  // increment the rate limit counter as a side effect.
  bool CheckOpenRateLimit();

  base::WeakPtr<vm_tools::cicerone::Service> service_;  // not owned
  // Task runner for the D-Bus thread; requests to perform D-Bus operations
  // on |service_| generally need to be posted to this thread.
  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  // Protects testing_peer_address_ so that OverridePeerAddressForTesting can
  // be called on any thread.
  base::Lock testing_peer_address_lock_;
  // Overrides ServerContext::peer if set.
  std::string testing_peer_address_;

  // We rate limit the requests to open a window/tab in Chrome to prevent an
  // accidental DOS of Chrome from a bad script in Linux. We use a fixed window
  // rate control algorithm to do this.
  uint32_t open_count_;
  base::TimeTicks open_rate_window_start_;
};

}  // namespace cicerone
}  // namespace vm_tools

#endif  // VM_TOOLS_CICERONE_CONTAINER_LISTENER_IMPL_H_
