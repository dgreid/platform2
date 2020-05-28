// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/grpc_client_manager.h"

#include <utility>

#include <base/barrier_closure.h>
#include <base/logging.h>
#include <base/threading/thread_task_runner_handle.h>

namespace diagnostics {

GrpcClientManager::GrpcClientManager() = default;
GrpcClientManager::~GrpcClientManager() = default;

void GrpcClientManager::Start(
    const std::string& ui_message_receiver_wilco_dtc_grpc_uri,
    const std::vector<std::string>& wilco_dtc_grpc_client_uris) {
  // Start the gRPC clients that talk to the wilco_dtc daemon.
  for (const auto& uri : wilco_dtc_grpc_client_uris) {
    wilco_dtc_grpc_clients_.push_back(
        std::make_unique<brillo::AsyncGrpcClient<grpc_api::WilcoDtc>>(
            base::ThreadTaskRunnerHandle::Get(), uri));
    VLOG(0) << "Created gRPC wilco_dtc client on " << uri;
  }

  // Start the gRPC client that is allowed to receive UI messages as a normal
  // gRPC client that talks to the wilco_dtc daemon.
  wilco_dtc_grpc_clients_.push_back(
      std::make_unique<brillo::AsyncGrpcClient<grpc_api::WilcoDtc>>(
          base::ThreadTaskRunnerHandle::Get(),
          ui_message_receiver_wilco_dtc_grpc_uri));
  VLOG(0) << "Created gRPC wilco_dtc client on "
          << ui_message_receiver_wilco_dtc_grpc_uri;
  ui_message_receiver_wilco_dtc_grpc_client_ =
      wilco_dtc_grpc_clients_.back().get();
}

void GrpcClientManager::ShutDown(base::OnceClosure on_shutdown_callback) {
  const base::Closure barrier_closure = base::BarrierClosure(
      wilco_dtc_grpc_clients_.size(), std::move(on_shutdown_callback));
  for (const auto& client : wilco_dtc_grpc_clients_) {
    client->ShutDown(barrier_closure);
  }
  ui_message_receiver_wilco_dtc_grpc_client_ = nullptr;
}

brillo::AsyncGrpcClient<grpc_api::WilcoDtc>* GrpcClientManager::GetUiClient()
    const {
  return ui_message_receiver_wilco_dtc_grpc_client_;
}

const std::vector<std::unique_ptr<brillo::AsyncGrpcClient<grpc_api::WilcoDtc>>>&
GrpcClientManager::GetClients() const {
  return wilco_dtc_grpc_clients_;
}

}  // namespace diagnostics
