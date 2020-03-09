// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/server_proxy.h"

#include <netinet/in.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback_helpers.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/message_loop/message_loop.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/message_loops/base_message_loop.h>

#include "bindings/worker_common.pb.h"
#include "system-proxy/protobuf_util.h"

namespace system_proxy {
namespace {
constexpr char kUser[] = "proxy_user";
constexpr char kPassword[] = "proxy_password";
constexpr int kTestPort = 3128;
}  // namespace

using ::testing::Return;

class MockServerProxy : public ServerProxy {
 public:
  explicit MockServerProxy(base::OnceClosure quit_closure)
      : ServerProxy(std::move(quit_closure)) {}
  MockServerProxy(const MockServerProxy&) = delete;
  MockServerProxy& operator=(const MockServerProxy&) = delete;
  ~MockServerProxy() override = default;

  MOCK_METHOD(int, GetStdinPipe, (), (override));
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
  void RedirectStdPipes() {
    int fds[2];
    CHECK(base::CreateLocalNonBlockingPipe(fds));
    read_scoped_fd_.reset(fds[0]);
    write_scoped_fd_.reset(fds[1]);
    EXPECT_CALL(*server_proxy_, GetStdinPipe())
        .WillRepeatedly(Return(read_scoped_fd_.get()));

    server_proxy_->Init();
  }
  // SystemProxyAdaptor instance that creates fake worker processes.
  std::unique_ptr<MockServerProxy> server_proxy_;
  base::MessageLoopForIO loop_;
  brillo::BaseMessageLoop brillo_loop_{&loop_};
  base::ScopedFD read_scoped_fd_, write_scoped_fd_;
};

TEST_F(ServerProxyTest, FetchCredentials) {
  Credentials credentials;
  credentials.set_username(kUser);
  credentials.set_password(kPassword);
  WorkerConfigs configs;
  *configs.mutable_credentials() = credentials;
  RedirectStdPipes();

  EXPECT_TRUE(WriteProtobuf(write_scoped_fd_.get(), configs));

  brillo_loop_.RunOnce(false);

  EXPECT_EQ(server_proxy_->username_, kUser);
  EXPECT_EQ(server_proxy_->password_, kPassword);
}

TEST_F(ServerProxyTest, FetchListeningAddress) {
  SocketAddress address;
  address.set_addr(INADDR_ANY);
  address.set_port(kTestPort);
  WorkerConfigs configs;
  *configs.mutable_listening_address() = address;
  RedirectStdPipes();
  // Redirect the worker stdin and stdout pipes.

  EXPECT_TRUE(WriteProtobuf(write_scoped_fd_.get(), configs));
  brillo_loop_.RunOnce(false);

  EXPECT_EQ(server_proxy_->listening_addr_, INADDR_ANY);
  EXPECT_EQ(server_proxy_->listening_port_, kTestPort);
}

}  // namespace system_proxy
