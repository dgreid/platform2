// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_GRPC_FUTURE_UTIL_H_
#define VM_TOOLS_CONCIERGE_GRPC_FUTURE_UTIL_H_

#include <memory>
#include <tuple>
#include <utility>

#include <brillo/grpc/async_grpc_client.h>
#include "vm_tools/concierge/future.h"

namespace vm_tools {

// Mirrors the definition in brillo/grpc/async_grpc_client.h
//
// Since the original definition lives in a template class, compiler will not be
// able to deduce the template parameters for the function below if the original
// definition is used.
template <typename AsyncServiceStub,
          typename RequestType,
          typename ResponseType>
using AsyncRequestFnPtr =
    std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>> (
        AsyncServiceStub::*)(grpc::ClientContext* context,
                             const RequestType& request,
                             grpc::CompletionQueue* cq);

template <typename ServiceType,
          typename AsyncServiceStub,
          typename RequestType,
          typename ResponseType>
static Future<std::tuple<grpc::Status, std::unique_ptr<ResponseType>>>
CallRpcFuture(brillo::AsyncGrpcClient<ServiceType>* client,
              AsyncRequestFnPtr<AsyncServiceStub, RequestType, ResponseType>
                  async_rpc_start,
              base::TimeDelta rpc_deadline,
              const RequestType& request) {
  Promise<std::tuple<grpc::Status, std::unique_ptr<ResponseType>>> promise;
  auto fut = promise.GetFuture(base::SequencedTaskRunnerHandle::Get());
  client->CallRpc(
      async_rpc_start, std::move(rpc_deadline), request,
      base::Bind(
          [](Promise<std::tuple<grpc::Status, std::unique_ptr<ResponseType>>>
                 promise,
             grpc::Status status, std::unique_ptr<ResponseType> response) {
            promise.SetValue(
                std::make_tuple(std::move(status), std::move(response)));
          },
          base::Passed(std::move(promise))));
  return fut;
}

}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_GRPC_FUTURE_UTIL_H_
