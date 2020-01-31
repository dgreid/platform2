// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CICERONE_CRASH_LISTENER_IMPL_H_
#define VM_TOOLS_CICERONE_CRASH_LISTENER_IMPL_H_

#include <grpcpp/grpcpp.h>
#include <vm_protos/proto_bindings/vm_crash.grpc.pb.h>

#include "metrics/metrics_library.h"

namespace vm_tools {
namespace cicerone {

class CrashListenerImpl final : public CrashListener::Service {
 public:
  CrashListenerImpl() = default;
  CrashListenerImpl(const CrashListenerImpl&) = delete;
  CrashListenerImpl& operator=(const CrashListenerImpl&) = delete;
  ~CrashListenerImpl() override = default;

  grpc::Status CheckMetricsConsent(grpc::ServerContext* ctx,
                                   const EmptyMessage* request,
                                   MetricsConsentResponse* response) override;

  grpc::Status SendCrashReport(grpc::ServerContext* ctx,
                               const CrashReport* crash_report,
                               EmptyMessage* response) override;

 private:
  MetricsLibrary metrics_{};
};

}  // namespace cicerone
}  // namespace vm_tools

#endif  // VM_TOOLS_CICERONE_CRASH_LISTENER_IMPL_H_
