// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/cicerone/crash_listener_impl.h"

namespace vm_tools {
namespace cicerone {

grpc::Status CrashListenerImpl::CheckMetricsConsent(
    grpc::ServerContext* ctx,
    const EmptyMessage* request,
    MetricsConsentResponse* response) {
  response->set_consent_granted(metrics_.AreMetricsEnabled());
  return grpc::Status::OK;
}

grpc::Status CrashListenerImpl::SendCrashReport(grpc::ServerContext* ctx,
                                                const CrashReport* crash_report,
                                                EmptyMessage* response) {
  LOG(INFO) << "A program crashed in the VM";

  return grpc::Status::OK;
}

}  // namespace cicerone
}  // namespace vm_tools
