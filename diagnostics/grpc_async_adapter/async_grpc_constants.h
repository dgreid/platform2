// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_GRPC_ASYNC_ADAPTER_ASYNC_GRPC_CONSTANTS_H_
#define DIAGNOSTICS_GRPC_ASYNC_ADAPTER_ASYNC_GRPC_CONSTANTS_H_

#include <base/time/time.h>

namespace diagnostics {

// Use this constant to explicitly set gRPC max send/receive message lengths,
// because GRPC_DEFAULT_MAX_SEND_MESSAGE_LENGTH const is -1.
// GRPC_DEFAULT_MAX_SEND_MESSAGE_LENGTH will be used as a default value if max
// send message length is not configured for client and server.
extern const int kMaxGrpcMessageSize;

// Use the following constants to control the backoff timer for reconnecting
// used by the GRPC client.

// Sets GRPC_ARG_MIN_RECONNECT_BACKOFF_MS
extern const base::TimeDelta kMinGrpcReconnectBackoffTime;
// Sets GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS
extern const base::TimeDelta kInitialGrpcReconnectBackoffTime;
// Sets GRPC_ARG_MAX_RECONNECT_BACKOFF_MS
extern const base::TimeDelta kMaxGrpcReconnectBackoffTime;

// Use this constant to set the deadline for RPC requests performed by the
// GRPC client.
extern const base::TimeDelta kRpcDeadline;

}  // namespace diagnostics

#endif  // DIAGNOSTICS_GRPC_ASYNC_ADAPTER_ASYNC_GRPC_CONSTANTS_H_
