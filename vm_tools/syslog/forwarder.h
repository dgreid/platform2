// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SYSLOG_FORWARDER_H_
#define VM_TOOLS_SYSLOG_FORWARDER_H_

#include <memory>

#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <grpcpp/grpcpp.h>
#include <vm_protos/proto_bindings/vm_host.grpc.pb.h>

namespace vm_tools {
namespace syslog {

// Responsible for collecting log records from the VM, scrubbing them,
// and then forwarding them to the host syslog daemon.
class Forwarder final : public LogCollector::Service {
 public:
  explicit Forwarder(base::ScopedFD destination,
                     bool is_socket_destination = true);
  ~Forwarder() override = default;

  // vm_tools::LogCollector::Service overrides.
  grpc::Status CollectKernelLogs(grpc::ServerContext* ctx,
                                 const vm_tools::LogRequest* request,
                                 vm_tools::EmptyMessage* response) override;
  grpc::Status CollectUserLogs(grpc::ServerContext* ctx,
                               const vm_tools::LogRequest* request,
                               vm_tools::EmptyMessage* response) override;

 private:
  // Common implementation for actually forwarding logs to the syslog daemon.
  grpc::Status ForwardLogs(grpc::ServerContext* ctx,
                           const vm_tools::LogRequest* request,
                           bool is_kernel);

  base::ScopedFD destination_;
  const bool is_socket_destination_;

  DISALLOW_COPY_AND_ASSIGN(Forwarder);
};

}  // namespace syslog
}  // namespace vm_tools

#endif  //  VM_TOOLS_SYSLOG_FORWARDER_H_
