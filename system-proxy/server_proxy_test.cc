// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/server_proxy.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback_helpers.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/message_loop/message_loop.h>
#include <base/strings/string_util.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/message_loops/base_message_loop.h>
#include <chromeos/patchpanel/socket.h>
#include <chromeos/patchpanel/socket_forwarder.h>
#include "bindings/worker_common.pb.h"
#include "system-proxy/protobuf_util.h"
#include "system-proxy/proxy_connect_job.h"

namespace system_proxy {
namespace {
constexpr char kUsername[] = "proxy:user";
constexpr char kUsernameEncoded[] = "proxy%3Auser";
constexpr char kPassword[] = "proxy password";
constexpr char kPasswordEncoded[] = "proxy%20password";
constexpr int kTestPort = 3128;
constexpr char kFakeProxyAddress[] = "http://127.0.0.1";

}  // namespace

using ::testing::_;
using ::testing::Return;

class MockServerProxy : public ServerProxy {
 public:
  explicit MockServerProxy(base::OnceClosure quit_closure)
      : ServerProxy(std::move(quit_closure)) {}
  MockServerProxy(const MockServerProxy&) = delete;
  MockServerProxy& operator=(const MockServerProxy&) = delete;
  ~MockServerProxy() override = default;

  MOCK_METHOD(int, GetStdinPipe, (), (override));
  MOCK_METHOD(int, GetStdoutPipe, (), (override));
};

class MockProxyConnectJob : public ProxyConnectJob {
 public:
  MockProxyConnectJob(std::unique_ptr<patchpanel::Socket> socket,
                      const std::string& credentials,
                      ResolveProxyCallback resolve_proxy_callback,
                      OnConnectionSetupFinishedCallback setup_finished_callback)
      : ProxyConnectJob(std::move(socket),
                        credentials,
                        std::move(resolve_proxy_callback),
                        std::move(setup_finished_callback)) {}
  MockProxyConnectJob(const MockProxyConnectJob&) = delete;
  MockProxyConnectJob& operator=(const MockProxyConnectJob&) = delete;
  ~MockProxyConnectJob() override = default;

  MOCK_METHOD(bool, Start, (), (override));
};

class ServerProxyTest : public ::testing::Test {
 public:
  ServerProxyTest() {
    server_proxy_ =
        std::make_unique<MockServerProxy>(brillo_loop_.QuitClosure());
  }

  ServerProxyTest(const ServerProxyTest&) = delete;
  ServerProxyTest& operator=(const ServerProxyTest&) = delete;
  ~ServerProxyTest() override {}

 protected:
  // Redirects the standard streams of the worker so that the tests can write
  // data in the worker's stdin input and read data from the worker's stdout
  // output.
  void RedirectStdPipes() {
    int fds[2];
    CHECK(base::CreateLocalNonBlockingPipe(fds));
    stdin_read_fd_.reset(fds[0]);
    stdin_write_fd_.reset(fds[1]);
    CHECK(base::CreateLocalNonBlockingPipe(fds));
    stdout_read_fd_.reset(fds[0]);
    stdout_write_fd_.reset(fds[1]);

    ON_CALL(*server_proxy_, GetStdinPipe())
        .WillByDefault(Return(stdin_read_fd_.get()));
    // Don't redirect all the calls to |stdout_write_fd_| or the test result
    // will not be printed in the console. Instead, when wanting to read the
    // standard output, set the expectation to once return |stdout_write_fd_|.
    ON_CALL(*server_proxy_, GetStdoutPipe())
        .WillByDefault(Return(STDOUT_FILENO));
    server_proxy_->Init();
  }
  // SystemProxyAdaptor instance that creates fake worker processes.
  std::unique_ptr<MockServerProxy> server_proxy_;
  base::MessageLoopForIO loop_;
  brillo::BaseMessageLoop brillo_loop_{&loop_};
  base::ScopedFD stdin_read_fd_, stdin_write_fd_, stdout_read_fd_,
      stdout_write_fd_;
};

TEST_F(ServerProxyTest, FetchCredentials) {
  Credentials credentials;
  credentials.set_username(kUsername);
  credentials.set_password(kPassword);
  WorkerConfigs configs;
  *configs.mutable_credentials() = credentials;
  RedirectStdPipes();

  EXPECT_TRUE(WriteProtobuf(stdin_write_fd_.get(), configs));

  brillo_loop_.RunOnce(false);

  std::string expected_credentials =
      base::JoinString({kUsernameEncoded, kPasswordEncoded}, ":");
  EXPECT_EQ(server_proxy_->credentials_, expected_credentials);
}

TEST_F(ServerProxyTest, FetchListeningAddress) {
  SocketAddress address;
  address.set_addr(INADDR_ANY);
  address.set_port(kTestPort);
  WorkerConfigs configs;
  *configs.mutable_listening_address() = address;
  // Redirect the worker stdin and stdout pipes.
  RedirectStdPipes();
  // Send the config to the worker's stdin input.
  EXPECT_TRUE(WriteProtobuf(stdin_write_fd_.get(), configs));
  brillo_loop_.RunOnce(false);

  EXPECT_EQ(server_proxy_->listening_addr_, INADDR_ANY);
  EXPECT_EQ(server_proxy_->listening_port_, kTestPort);
}

// Tests that ServerProxy handles the basic flow of a connect request:
// - server accepts a connection a creates a job for it until the connection is
// finished;
// - the connect request from the client socket is read and parsed;
// - proxy resolution request is correctly handled by the job and ServerProxy;
// - client is sent an HTTP error code in case of failure;
// - the failed connection job is removed from the queue.
TEST_F(ServerProxyTest, HandleConnectRequest) {
  server_proxy_->listening_addr_ = htonl(INADDR_LOOPBACK);
  server_proxy_->listening_port_ = kTestPort;
  // Redirect the worker stdin and stdout pipes.
  RedirectStdPipes();
  server_proxy_->CreateListeningSocket();
  CHECK_NE(-1, server_proxy_->listening_fd_->fd());
  brillo_loop_.RunOnce(false);

  struct sockaddr_in ipv4addr;
  ipv4addr.sin_family = AF_INET;
  ipv4addr.sin_port = htons(kTestPort);
  ipv4addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  auto client_socket =
      std::make_unique<patchpanel::Socket>(AF_INET, SOCK_STREAM);
  EXPECT_TRUE(client_socket->Connect((const struct sockaddr*)&ipv4addr,
                                     sizeof(ipv4addr)));
  brillo_loop_.RunOnce(false);

  EXPECT_EQ(1, server_proxy_->pending_connect_jobs_.size());
  const std::string_view http_req =
      "CONNECT www.example.server.com:443 HTTP/1.1\r\n\r\n";
  client_socket->SendTo(http_req.data(), http_req.size());

  EXPECT_CALL(*server_proxy_, GetStdoutPipe())
      .WillOnce(Return(stdout_write_fd_.get()));
  brillo_loop_.RunOnce(false);
  WorkerRequest request;
  // Read the request from the worker's stdout output.
  ASSERT_TRUE(ReadProtobuf(stdout_read_fd_.get(), &request));
  ASSERT_TRUE(request.has_proxy_resolution_request());

  EXPECT_EQ("http://www.example.server.com:443",
            request.proxy_resolution_request().target_url());

  EXPECT_EQ(1, server_proxy_->pending_proxy_resolution_requests_.size());

  // Write reply with a fake proxy to the worker's standard input.
  ProxyResolutionReply reply;
  reply.set_target_url(request.proxy_resolution_request().target_url());
  reply.add_proxy_servers(kFakeProxyAddress);
  WorkerConfigs configs;
  *configs.mutable_proxy_resolution_reply() = reply;

  ASSERT_TRUE(WriteProtobuf(stdin_write_fd_.get(), configs));
  brillo_loop_.RunOnce(false);

  // Verify that the correct HTTP error code is sent to the client. Because
  // curl_perform will fail, this will be reported as an internal server error.
  const std::string expected_http_reply =
      "HTTP/1.1 500 Internal Server Error - Origin: local proxy\r\n\r\n";
  std::vector<char> buf(expected_http_reply.size());
  ASSERT_TRUE(base::ReadFromFD(client_socket->fd(), buf.data(), buf.size()));
  buf.push_back('\0');
  const std::string actual_http_reply(buf.data());
  EXPECT_EQ(expected_http_reply, actual_http_reply);
  EXPECT_EQ(0, server_proxy_->pending_connect_jobs_.size());
}

// Tests the |OnConnectionSetupFinished| callback is handled correctly in case
// of success or error.
TEST_F(ServerProxyTest, HandlePendingJobs) {
  int connection_count = 100;
  int success_count = 51;
  int failure_count = 49;
  // Create |connection_count| connections.
  for (int i = 0; i < connection_count; ++i) {
    auto client_socket =
        std::make_unique<patchpanel::Socket>(AF_INET, SOCK_STREAM);
    auto mock_connect_job = std::make_unique<MockProxyConnectJob>(
        std::move(client_socket), "" /* credentials */,
        base::BindOnce([](const std::string& target_url,
                          OnProxyResolvedCallback callback) {}),
        base::BindOnce(&ServerProxy::OnConnectionSetupFinished,
                       base::Unretained(server_proxy_.get())));
    server_proxy_->pending_connect_jobs_[mock_connect_job.get()] =
        std::move(mock_connect_job);
  }
  // Resolve |failure_count| pending connections with error.
  for (int i = 0; i < failure_count; ++i) {
    auto job_iter = server_proxy_->pending_connect_jobs_.begin();
    std::move(job_iter->second->setup_finished_callback_)
        .Run(nullptr, job_iter->first);
  }
  // Expect failed requests have been cleared from the pending list and no
  // forwarder.
  EXPECT_EQ(success_count, server_proxy_->pending_connect_jobs_.size());
  EXPECT_EQ(0, server_proxy_->forwarders_.size());

  // Resolve |success_count| successful connections.
  for (int i = 0; i < success_count; ++i) {
    auto fwd = std::make_unique<patchpanel::SocketForwarder>(
        "" /* thread name */,
        std::make_unique<patchpanel::Socket>(AF_INET, SOCK_STREAM),
        std::make_unique<patchpanel::Socket>(AF_INET, SOCK_STREAM));
    fwd->Start();
    auto job_iter = server_proxy_->pending_connect_jobs_.begin();
    std::move(job_iter->second->setup_finished_callback_)
        .Run(std::move(fwd), job_iter->first);
  }

  // Expect the successful requests to have been cleared and |success_count|
  // active forwarders.
  EXPECT_EQ(0, server_proxy_->pending_connect_jobs_.size());
  EXPECT_EQ(success_count, server_proxy_->forwarders_.size());
}

}  // namespace system_proxy
