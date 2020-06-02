// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is an integration test, testing AsyncGrpcClient and AsyncGrpcServer by
// sending messages between instances of the two classes.

#include <memory>
#include <queue>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/macros.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <gmock/gmock.h>
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include "brillo/grpc/async_grpc_client.h"
#include "brillo/grpc/async_grpc_constants.h"
#include "brillo/grpc/async_grpc_server.h"
#include "test_rpcs.grpc.pb.h"  // NOLINT(build/include)

namespace brillo {

namespace {

// A utility for testing incoming RPCs. It exposes a handler callback through
// |GetRpcHandlerCallback|. This can be passed to
// |AsyncGrpcServer::RegisterRpcHandler|.
template <typename RequestType, typename ResponseType>
class PendingIncomingRpcQueue {
 public:
  using HandlerDoneCallback =
      base::Callback<void(grpc::Status status,
                          std::unique_ptr<ResponseType> response)>;
  using RpcHandlerCallback =
      base::Callback<void(std::unique_ptr<RequestType> request,
                          const HandlerDoneCallback& response_callback)>;

  // Holds information about an RPC that should be handled.
  struct PendingIncomingRpc {
    // The request of this RPC.
    std::unique_ptr<RequestType> request;
    // The callback which must be called to answer this RPC.
    HandlerDoneCallback handler_done_callback;
  };

  PendingIncomingRpcQueue() : weak_ptr_factory_(this) {}
  ~PendingIncomingRpcQueue() = default;

  // Returns a callback that should be called when an incoming RPC is available.
  RpcHandlerCallback GetRpcHandlerCallback() {
    return base::Bind(&PendingIncomingRpcQueue::HandleRpc,
                      weak_ptr_factory_.GetWeakPtr());
  }

  // Wait until there are |count| pending incoming RPCs of this type.
  void WaitUntilPendingRpcCount(size_t count) {
    while (pending_rpcs_.size() < count) {
      waiting_loop_ = std::make_unique<base::RunLoop>();
      waiting_loop_->Run();
    }
  }

  // Get the IncomingRpcContext for the oldest pending incoming RPC. May only be
  // called if there is at least one pending incoming RPC.
  std::unique_ptr<PendingIncomingRpc> GetOldestPendingRpc() {
    CHECK(!pending_rpcs_.empty());
    auto oldest_pending_rpc = std::move(pending_rpcs_.front());
    pending_rpcs_.pop();
    return oldest_pending_rpc;
  }

 private:
  // This is the actual handler function invoked on incoming RPCs.
  void HandleRpc(std::unique_ptr<RequestType> request,
                 const HandlerDoneCallback& handler_done_callback) {
    auto incoming_rpc = std::make_unique<PendingIncomingRpc>();
    incoming_rpc->request = std::move(request);
    incoming_rpc->handler_done_callback = handler_done_callback;
    pending_rpcs_.push(std::move(incoming_rpc));
    if (waiting_loop_)
      waiting_loop_->Quit();
  }

  // Holds information about all RPCs that this |PendingIncomingRpcQueue| was
  // asked to handle, but which have not been retrieved using
  // |GetOldestPendingRpc| yet.
  std::queue<std::unique_ptr<PendingIncomingRpc>> pending_rpcs_;
  // This |RunLoop| is started when waiting for (an) incoming RPC(s) and exited
  // when an RPC comes in.
  std::unique_ptr<base::RunLoop> waiting_loop_;

  base::WeakPtrFactory<PendingIncomingRpcQueue> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(PendingIncomingRpcQueue);
};

// A utility for testing outgoing RPCs. It gets notified of a response to an
// outgoing RPC through the callback it returns from |MakeWriter|.
template <typename ResponseType>
class RpcReply {
 public:
  RpcReply() : weak_ptr_factory_(this) {}
  ~RpcReply() = default;

  // Returns a callback that should be called when a response to the outgoing
  // RPC is available.
  base::Callback<void(grpc::Status status,
                      std::unique_ptr<ResponseType>)> MakeWriter() {
    return base::Bind(&RpcReply::OnReply, weak_ptr_factory_.GetWeakPtr());
  }

  // Wait until this RPC has a reply.
  void Wait() {
    if (has_reply_)
      return;

    waiting_loop_ = std::make_unique<base::RunLoop>();
    waiting_loop_->Run();
  }

  // Returns true if the reply indicated an error. This may only be called after
  // |Wait| returned.
  bool IsError() const {
    CHECK(has_reply_);
    return !status_.ok();
  }

  // Returns this outgoing RPC's response. This may only be called after
  // |Wait| returned and when |IsError| is false.
  const ResponseType& response() const {
    CHECK(!IsError());
    return *response_;
  }

 private:
  void OnReply(grpc::Status status, std::unique_ptr<ResponseType> response) {
    CHECK(!has_reply_);

    has_reply_ = true;
    response_ = std::move(response);
    status_ = std::move(status);

    if (waiting_loop_)
      waiting_loop_->Quit();
  }

  std::unique_ptr<base::RunLoop> waiting_loop_;
  bool has_reply_ = false;
  grpc::Status status_;
  std::unique_ptr<ResponseType> response_;

  base::WeakPtrFactory<RpcReply> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(RpcReply);
};

}  // namespace

// Creates an |AsyncGrpcServer| and |AsyncGrpcClient| for the
// test_rpcs::ExampleService::AsyncService interface (defined in
// test_rpcs.proto), set up to talk to each other.
class AsyncGrpcClientServerTest : public ::testing::Test {
 public:
  AsyncGrpcClientServerTest() = default;

  void SetUp() override {
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());

    // Create a temporary filename that's guaranteed to not exist, but is
    // inside our scoped directory so it'll get deleted later.
    // |tmpfile_| will be used as a unix domain socket to communicate between
    // the server and client used here.
    tmpfile_ = tmpdir_.GetPath().AppendASCII("testsocket");

    StartServer();

    // Create the AsyncGrpcClient.
    client_ = std::make_unique<AsyncGrpcClient<test_rpcs::ExampleService>>(
        message_loop_.task_runner(), GetDomainSocketAddress());
  }

  void StartServer() {
    // Create and start the AsyncGrpcServer.
    server_ = std::make_unique<
        AsyncGrpcServer<test_rpcs::ExampleService::AsyncService>>(
        message_loop_.task_runner(),
        std::vector<std::string>{GetDomainSocketAddress()});
    server_->RegisterHandler(
        &test_rpcs::ExampleService::AsyncService::RequestEmptyRpc,
        pending_empty_rpcs_.GetRpcHandlerCallback());
    server_->RegisterHandler(
        &test_rpcs::ExampleService::AsyncService::RequestEchoIntRpc,
        pending_echo_int_rpcs_.GetRpcHandlerCallback());
    server_->RegisterHandler(
        &test_rpcs::ExampleService::AsyncService::RequestHeavyRpc,
        pending_heavy_rpcs_.GetRpcHandlerCallback());
    ASSERT_TRUE(server_->Start());
  }

  // Create the second AsyncGrpcClient using the same socket, which has to be
  // shutdown by ShutDownSecondClient.
  void CreateSecondClient() {
    client2_ = std::make_unique<AsyncGrpcClient<test_rpcs::ExampleService>>(
        message_loop_.task_runner(), GetDomainSocketAddress());
  }

  // Shutdown the second AsyncGrpcClient.
  void ShutDownSecondClient() {
    base::RunLoop loop;
    client2_->ShutDown(loop.QuitClosure());
    loop.Run();
    // Explicitly delete client before server to avoid gRPC 1.6.1 "magic" 10
    // seconds hangs to delete grpc::CompletionQueue. It affects HeavyRpcData
    // test only.
    // TODO(b/132969701): remove when gRPC won't have hangs bug.
    client2_.reset();
  }

  void RestartServer() {
    ShutDownServer();
    StartServer();
  }

  void TearDown() override {
    ShutDownClient();
    ShutDownServer();
  }

 protected:
  void ShutDownServer() {
    if (!server_)
      return;
    base::RunLoop loop;
    server_->ShutDown(loop.QuitClosure());
    loop.Run();
    server_.reset();
  }

  base::MessageLoopForIO message_loop_;
  std::unique_ptr<AsyncGrpcServer<test_rpcs::ExampleService::AsyncService>>
      server_;
  std::unique_ptr<AsyncGrpcClient<test_rpcs::ExampleService>> client_;
  std::unique_ptr<AsyncGrpcClient<test_rpcs::ExampleService>> client2_;

  PendingIncomingRpcQueue<test_rpcs::EmptyRpcRequest,
                          test_rpcs::EmptyRpcResponse>
      pending_empty_rpcs_;
  PendingIncomingRpcQueue<test_rpcs::EchoIntRpcRequest,
                          test_rpcs::EchoIntRpcResponse>
      pending_echo_int_rpcs_;
  PendingIncomingRpcQueue<test_rpcs::HeavyRpcRequest,
                          test_rpcs::HeavyRpcResponse>
      pending_heavy_rpcs_;

 private:
  std::string GetDomainSocketAddress() { return "unix:" + tmpfile_.value(); }

  void ShutDownClient() {
    base::RunLoop loop;
    client_->ShutDown(loop.QuitClosure());
    loop.Run();
    // Explicitly delete client before server to avoid gRPC 1.6.1 "magic" 10
    // seconds hangs to delete grpc::CompletionQueue. It affects HeavyRpcData
    // test only.
    // TODO(b/132969701): remove when gRPC won't have hangs bug.
    client_.reset();
  }

  base::ScopedTempDir tmpdir_;
  base::FilePath tmpfile_;

  DISALLOW_COPY_AND_ASSIGN(AsyncGrpcClientServerTest);
};

// Start and shutdown a server and a client.
TEST_F(AsyncGrpcClientServerTest, NoRpcs) {}

// Send one RPC and verify that the response arrives at the client.
// Verifies that the response contains the data transferred in the request.
TEST_F(AsyncGrpcClientServerTest, OneRpcWithResponse) {
  RpcReply<test_rpcs::EchoIntRpcResponse> rpc_reply;
  test_rpcs::EchoIntRpcRequest request;
  request.set_int_to_echo(42);
  client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEchoIntRpc, request,
                   rpc_reply.MakeWriter());

  pending_echo_int_rpcs_.WaitUntilPendingRpcCount(1);
  auto pending_rpc = pending_echo_int_rpcs_.GetOldestPendingRpc();
  EXPECT_EQ(42, pending_rpc->request->int_to_echo());

  auto response = std::make_unique<test_rpcs::EchoIntRpcResponse>();
  response->set_echoed_int(42);
  pending_rpc->handler_done_callback.Run(grpc::Status::OK, std::move(response));

  rpc_reply.Wait();
  EXPECT_FALSE(rpc_reply.IsError());
  EXPECT_EQ(42, rpc_reply.response().echoed_int());
}

TEST_F(AsyncGrpcClientServerTest, MultipleRpcTypes) {
  RpcReply<test_rpcs::EchoIntRpcResponse> echo_int_rpc_reply;
  RpcReply<test_rpcs::EmptyRpcResponse> empty_rpc_reply;

  // Start two different RPC types:
  // - The EmptyRpc first
  // - The EchoIntRpc second
  test_rpcs::EmptyRpcRequest empty_rpc_request;
  client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEmptyRpc,
                   empty_rpc_request, empty_rpc_reply.MakeWriter());

  test_rpcs::EchoIntRpcRequest echo_int_rpc_request;
  echo_int_rpc_request.set_int_to_echo(33);
  client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEchoIntRpc,
                   echo_int_rpc_request, echo_int_rpc_reply.MakeWriter());

  // Respond to the EchoIntRpc and wait for the response
  pending_echo_int_rpcs_.WaitUntilPendingRpcCount(1);
  auto pending_echo_int_rpc = pending_echo_int_rpcs_.GetOldestPendingRpc();
  EXPECT_EQ(33, pending_echo_int_rpc->request->int_to_echo());
  auto echo_int_response = std::make_unique<test_rpcs::EchoIntRpcResponse>();
  echo_int_response->set_echoed_int(33);
  pending_echo_int_rpc->handler_done_callback.Run(grpc::Status::OK,
                                                  std::move(echo_int_response));

  echo_int_rpc_reply.Wait();
  EXPECT_FALSE(echo_int_rpc_reply.IsError());
  EXPECT_EQ(33, echo_int_rpc_reply.response().echoed_int());

  // Respond to the EmptyRpc and wait for the response
  pending_empty_rpcs_.WaitUntilPendingRpcCount(1);
  auto pending_empty_rpc = pending_empty_rpcs_.GetOldestPendingRpc();
  auto empty_rpc_response = std::make_unique<test_rpcs::EmptyRpcResponse>();
  pending_empty_rpc->handler_done_callback.Run(grpc::Status::OK,
                                               std::move(empty_rpc_response));

  empty_rpc_reply.Wait();
  EXPECT_FALSE(empty_rpc_reply.IsError());
}

// Send one RPC, cancel it on the server side. Verify that the error arrives at
// the client.
TEST_F(AsyncGrpcClientServerTest, OneRpcExplicitCancellation) {
  RpcReply<test_rpcs::EmptyRpcResponse> rpc_reply;
  test_rpcs::EmptyRpcRequest request;
  client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEmptyRpc, request,
                   rpc_reply.MakeWriter());

  pending_empty_rpcs_.WaitUntilPendingRpcCount(1);
  auto pending_rpc = pending_empty_rpcs_.GetOldestPendingRpc();
  pending_rpc->handler_done_callback.Run(
    grpc::Status(grpc::StatusCode::CANCELLED, "Cancelled on the server side"),
    nullptr);

  rpc_reply.Wait();
  EXPECT_TRUE(rpc_reply.IsError());
}

// Send one RPC and don't answer, then shutdown the server.
// Verify that the client gets an error reply.
// Also implicitly verifies that shutting down the server when there's a pending
// RPC does not e.g. hang or crash.
TEST_F(AsyncGrpcClientServerTest, ShutDownWhileRpcIsPending) {
  RpcReply<test_rpcs::EmptyRpcResponse> rpc_reply;
  test_rpcs::EmptyRpcRequest request;
  client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEmptyRpc, request,
                   rpc_reply.MakeWriter());

  pending_empty_rpcs_.WaitUntilPendingRpcCount(1);
  auto pending_empty_rpc = pending_empty_rpcs_.GetOldestPendingRpc();
  ShutDownServer();

  rpc_reply.Wait();
  EXPECT_TRUE(rpc_reply.IsError());

  // Also test that providing a response now does not crash.
  auto empty_rpc_response = std::make_unique<test_rpcs::EmptyRpcResponse>();
  pending_empty_rpc->handler_done_callback.Run(grpc::Status::OK,
                                               std::move(empty_rpc_response));
}

// Initiate a shutdown of the server and immediately send a response.
// This should not crash, but we expect that the cancellation arrives at the
// sender.
TEST_F(AsyncGrpcClientServerTest, SendResponseAfterInitiatingShutdown) {
  RpcReply<test_rpcs::EmptyRpcResponse> rpc_reply;
  test_rpcs::EmptyRpcRequest request;
  client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEmptyRpc, request,
                   rpc_reply.MakeWriter());

  pending_empty_rpcs_.WaitUntilPendingRpcCount(1);
  auto pending_empty_rpc = pending_empty_rpcs_.GetOldestPendingRpc();

  base::RunLoop loop;
  server_->ShutDown(loop.QuitClosure());
  auto empty_rpc_response = std::make_unique<test_rpcs::EmptyRpcResponse>();
  pending_empty_rpc->handler_done_callback.Run(grpc::Status::OK,
                                               std::move(empty_rpc_response));

  loop.Run();
  server_.reset();

  rpc_reply.Wait();
  EXPECT_TRUE(rpc_reply.IsError());
}

// Send many RPCs. The server will accumulate pending RPCs, then respond to all
// of them in a batch.
TEST_F(AsyncGrpcClientServerTest, ManyRpcs) {
  const int kNumOfRpcs = 10;
  RpcReply<test_rpcs::EchoIntRpcResponse> rpc_replies[kNumOfRpcs];
  for (int i = 0; i < kNumOfRpcs; ++i) {
    test_rpcs::EchoIntRpcRequest request;
    request.set_int_to_echo(i);
    client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEchoIntRpc, request,
                     rpc_replies[i].MakeWriter());
  }

  pending_echo_int_rpcs_.WaitUntilPendingRpcCount(kNumOfRpcs);
  for (int i = 0; i < kNumOfRpcs; ++i) {
    auto pending_rpc = pending_echo_int_rpcs_.GetOldestPendingRpc();
    auto response = std::make_unique<test_rpcs::EchoIntRpcResponse>();
    response->set_echoed_int(pending_rpc->request->int_to_echo());
    pending_rpc->handler_done_callback.Run(grpc::Status::OK,
                                           std::move(response));
  }

  for (int i = 0; i < kNumOfRpcs; ++i) {
    rpc_replies[i].Wait();
    EXPECT_FALSE(rpc_replies[i].IsError());
    EXPECT_EQ(i, rpc_replies[i].response().echoed_int());
  }
}

// Test that heavy, but within the acceptable bounds, requests and responses are
// handled correctly.
TEST_F(AsyncGrpcClientServerTest, HeavyRpcData) {
  // kDataSize must be close to kMaxGrpcMessageSize, but also takes
  // into account protobuf/gRPC size overhead.
  const int kDataSize = kMaxGrpcMessageSize - 100;
  ASSERT_GE(kDataSize, 0);
  const std::string kData(kDataSize, '\1');

  RpcReply<test_rpcs::HeavyRpcResponse> rpc_reply;
  test_rpcs::HeavyRpcRequest request;
  request.set_data(kData);
  client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncHeavyRpc, request,
                   rpc_reply.MakeWriter());

  pending_heavy_rpcs_.WaitUntilPendingRpcCount(1);
  auto pending_rpc = pending_heavy_rpcs_.GetOldestPendingRpc();
  EXPECT_EQ(kData, pending_rpc->request->data());

  auto response = std::make_unique<test_rpcs::HeavyRpcResponse>();
  response->set_data(kData);
  pending_rpc->handler_done_callback.Run(grpc::Status::OK,
                                         std::move(response));

  rpc_reply.Wait();
  EXPECT_FALSE(rpc_reply.IsError());
  EXPECT_EQ(kData, rpc_reply.response().data());
}

// Test than an excessively big request gets rejected.
TEST_F(AsyncGrpcClientServerTest, ExcessivelyBigRpcRequest) {
  const int kDataSize = kMaxGrpcMessageSize + 1;
  const std::string kData(kDataSize, '\1');

  RpcReply<test_rpcs::HeavyRpcResponse> rpc_reply;
  test_rpcs::HeavyRpcRequest request;
  request.set_data(kData);
  client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncHeavyRpc, request,
                   rpc_reply.MakeWriter());

  rpc_reply.Wait();
  EXPECT_TRUE(rpc_reply.IsError());
}

// Test than an excessively big response gets rejected and results in the
// request being resolved with an error.
TEST_F(AsyncGrpcClientServerTest, ExcessivelyBigRpcResponse) {
  const int kDataSize = kMaxGrpcMessageSize + 1;
  const std::string kData(kDataSize, '\1');

  RpcReply<test_rpcs::HeavyRpcResponse> rpc_reply;
  client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncHeavyRpc,
                   test_rpcs::HeavyRpcRequest(), rpc_reply.MakeWriter());

  pending_heavy_rpcs_.WaitUntilPendingRpcCount(1);
  auto pending_rpc = pending_heavy_rpcs_.GetOldestPendingRpc();

  auto response = std::make_unique<test_rpcs::HeavyRpcResponse>();
  response->set_data(kData);
  pending_rpc->handler_done_callback.Run(grpc::Status::OK, std::move(response));

  rpc_reply.Wait();
  EXPECT_TRUE(rpc_reply.IsError());
}

// Set up two RPC clients. Send one RPC from each client and verify that
// the response arrives at the corresponding client.
// Verifies that the response contains the data transferred in the request.
TEST_F(AsyncGrpcClientServerTest, TwoRpcClients) {
  const int kNumOfRpcs = 3;
  RpcReply<test_rpcs::EchoIntRpcResponse> rpc_replies[kNumOfRpcs];
  {
    test_rpcs::EchoIntRpcRequest request;
    request.set_int_to_echo(0);
    client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEchoIntRpc, request,
                     rpc_replies[0].MakeWriter());
  }

  CreateSecondClient();
  {
    test_rpcs::EchoIntRpcRequest request;
    request.set_int_to_echo(1);
    client2_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEchoIntRpc,
                      request, rpc_replies[1].MakeWriter());
  }

  {
    test_rpcs::EchoIntRpcRequest request;
    request.set_int_to_echo(2);
    client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEchoIntRpc, request,
                     rpc_replies[2].MakeWriter());
  }

  pending_echo_int_rpcs_.WaitUntilPendingRpcCount(kNumOfRpcs);
  for (int i = 0; i < kNumOfRpcs; ++i) {
    auto pending_rpc = pending_echo_int_rpcs_.GetOldestPendingRpc();
    auto response = std::make_unique<test_rpcs::EchoIntRpcResponse>();
    response->set_echoed_int(pending_rpc->request->int_to_echo());
    pending_rpc->handler_done_callback.Run(grpc::Status::OK,
                                           std::move(response));
  }

  for (int i = 0; i < kNumOfRpcs; ++i) {
    rpc_replies[i].Wait();
    EXPECT_FALSE(rpc_replies[i].IsError());
    EXPECT_EQ(i, rpc_replies[i].response().echoed_int());
  }
  ShutDownSecondClient();
}

// Set up a RPC server, then restart it. Send one RPC to each instance.
TEST_F(AsyncGrpcClientServerTest, RpcServerRestarted) {
  {
    RpcReply<test_rpcs::EchoIntRpcResponse> rpc_reply;
    test_rpcs::EchoIntRpcRequest request;
    request.set_int_to_echo(1);
    client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEchoIntRpc, request,
                     rpc_reply.MakeWriter());

    pending_echo_int_rpcs_.WaitUntilPendingRpcCount(1);
    auto pending_rpc = pending_echo_int_rpcs_.GetOldestPendingRpc();
    EXPECT_EQ(1, pending_rpc->request->int_to_echo());

    auto response = std::make_unique<test_rpcs::EchoIntRpcResponse>();
    response->set_echoed_int(1);
    pending_rpc->handler_done_callback.Run(grpc::Status::OK,
                                           std::move(response));

    rpc_reply.Wait();
    EXPECT_FALSE(rpc_reply.IsError());
    EXPECT_EQ(1, rpc_reply.response().echoed_int());
  }

  RestartServer();

  {
    RpcReply<test_rpcs::EchoIntRpcResponse> rpc_reply;
    test_rpcs::EchoIntRpcRequest request;
    request.set_int_to_echo(2);
    client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEchoIntRpc, request,
                     rpc_reply.MakeWriter());

    pending_echo_int_rpcs_.WaitUntilPendingRpcCount(1);
    auto pending_rpc = pending_echo_int_rpcs_.GetOldestPendingRpc();
    EXPECT_EQ(2, pending_rpc->request->int_to_echo());

    auto response = std::make_unique<test_rpcs::EchoIntRpcResponse>();
    response->set_echoed_int(2);
    pending_rpc->handler_done_callback.Run(grpc::Status::OK,
                                           std::move(response));

    rpc_reply.Wait();
    EXPECT_FALSE(rpc_reply.IsError());
    EXPECT_EQ(2, rpc_reply.response().echoed_int());
  }
}

// Send a request to a stopped server. The request should not fail immediately,
// it should wait for the rpc deadline to pass.
TEST_F(AsyncGrpcClientServerTest, RpcServerStopped) {
  ShutDownServer();

  client_->SetRpcDeadlineForTesting(base::TimeDelta::FromMilliseconds(50));

  base::TimeTicks start = base::TimeTicks::Now();

  RpcReply<test_rpcs::EchoIntRpcResponse> rpc_reply;
  test_rpcs::EchoIntRpcRequest request;
  request.set_int_to_echo(1);
  client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEchoIntRpc, request,
                   rpc_reply.MakeWriter());

  rpc_reply.Wait();
  EXPECT_TRUE(rpc_reply.IsError());

  base::TimeDelta duration = base::TimeTicks::Now() - start;

  EXPECT_GT(duration.InMilliseconds(), 40);  // Forgiving time comparison.
}

// Send a request to a server that starts after the request is made. The client
// should only send the request after the connection has been established.
TEST_F(AsyncGrpcClientServerTest, RpcServerStartedAfter) {
  ShutDownServer();

  RpcReply<test_rpcs::EchoIntRpcResponse> rpc_reply;
  test_rpcs::EchoIntRpcRequest request;
  request.set_int_to_echo(1);
  client_->CallRpc(&test_rpcs::ExampleService::Stub::AsyncEchoIntRpc, request,
                   rpc_reply.MakeWriter());

  base::TimeTicks start = base::TimeTicks::Now();

  StartServer();

  pending_echo_int_rpcs_.WaitUntilPendingRpcCount(1);
  auto pending_rpc = pending_echo_int_rpcs_.GetOldestPendingRpc();
  EXPECT_EQ(1, pending_rpc->request->int_to_echo());

  auto response = std::make_unique<test_rpcs::EchoIntRpcResponse>();
  response->set_echoed_int(2);
  pending_rpc->handler_done_callback.Run(grpc::Status::OK, std::move(response));

  rpc_reply.Wait();
  EXPECT_FALSE(rpc_reply.IsError());
  EXPECT_EQ(2, rpc_reply.response().echoed_int());

  base::TimeDelta duration = base::TimeTicks::Now() - start;

  // Check the reduced initial reconnect time. 1 second is the gRPC default.
  EXPECT_LT(duration.InMilliseconds(), 1000);
}

}  // namespace brillo
