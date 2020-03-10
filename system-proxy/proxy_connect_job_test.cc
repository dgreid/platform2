// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/proxy_connect_job.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <utility>

#include <arc/network/socket.h>
#include <arc/network/socket_forwarder.h>
#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback_helpers.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <brillo/message_loops/base_message_loop.h>

#include "bindings/worker_common.pb.h"
#include "system-proxy/protobuf_util.h"

namespace {
constexpr char kProxyServerUrl[] = "172.0.0.1:8888";
}  // namespace

namespace system_proxy {

using ::testing::_;
using ::testing::Return;

class ProxyConnectJobTest : public ::testing::Test {
 public:
  ProxyConnectJobTest() = default;
  ProxyConnectJobTest(const ProxyConnectJobTest&) = delete;
  ProxyConnectJobTest& operator=(const ProxyConnectJobTest&) = delete;
  ~ProxyConnectJobTest() = default;

  void SetUp() override {
    int fds[2];
    ASSERT_NE(-1,
              socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                         0 /* protocol */, fds));
    cros_client_socket_ =
        std::make_unique<arc_networkd::Socket>(base::ScopedFD(fds[1]));

    connect_job_ = std::make_unique<ProxyConnectJob>(
        std::make_unique<arc_networkd::Socket>(base::ScopedFD(fds[0])), "",
        base::BindOnce(&ProxyConnectJobTest::ResolveProxy,
                       base::Unretained(this)),
        base::BindOnce(&ProxyConnectJobTest::OnConnectionSetupFinished,
                       base::Unretained(this)));
    connect_job_->Start();
  }

 protected:
  void ResolveProxy(
      const std::string& target_url,
      base::OnceCallback<void(const std::list<std::string>&)> callback) {
    std::move(callback).Run({kProxyServerUrl});
  }

  void OnConnectionSetupFinished(
      std::unique_ptr<arc_networkd::SocketForwarder> fwd,
      ProxyConnectJob* connect_job) {
    ASSERT_EQ(connect_job, connect_job_.get());
  }

  std::unique_ptr<ProxyConnectJob> connect_job_;
  base::MessageLoopForIO loop_;
  brillo::BaseMessageLoop brillo_loop_{&loop_};
  std::unique_ptr<arc_networkd::Socket> cros_client_socket_;
};

TEST_F(ProxyConnectJobTest, SuccessfulConnection) {
  char validConnRequest[] =
      "CONNECT www.example.server.com:443 HTTP/1.1\r\n\r\n";
  cros_client_socket_->SendTo(validConnRequest, std::strlen(validConnRequest));
  brillo_loop_.RunOnce(false);

  EXPECT_EQ("http://www.example.server.com:443", connect_job_->target_url_);
  EXPECT_EQ(1, connect_job_->proxy_servers_.size());
  EXPECT_EQ(kProxyServerUrl, connect_job_->proxy_servers_.front());
}

TEST_F(ProxyConnectJobTest, BadHttpRequestWrongMethod) {
  char badConnRequest[] = "GET www.example.server.com:443 HTTP/1.1\r\n\r\n";
  cros_client_socket_->SendTo(badConnRequest, std::strlen(badConnRequest));
  brillo_loop_.RunOnce(false);

  EXPECT_EQ("", connect_job_->target_url_);
  EXPECT_EQ(0, connect_job_->proxy_servers_.size());
  const std::string expected_http_response =
      "HTTP/1.1 400 Bad Request - Origin: local proxy\r\n\r\n";
  std::vector<char> buf(expected_http_response.size());
  ASSERT_TRUE(
      base::ReadFromFD(cros_client_socket_->fd(), buf.data(), buf.size()));
  std::string actual_response(buf.data(), buf.size());
  EXPECT_EQ(expected_http_response, actual_response);
}

TEST_F(ProxyConnectJobTest, BadHttpRequestNoEmptyLine) {
  // No empty line after http message.
  char badConnRequest[] = "CONNECT www.example.server.com:443 HTTP/1.1\r\n";
  cros_client_socket_->SendTo(badConnRequest, std::strlen(badConnRequest));
  brillo_loop_.RunOnce(false);

  EXPECT_EQ("", connect_job_->target_url_);
  EXPECT_EQ(0, connect_job_->proxy_servers_.size());
  const std::string expected_http_response =
      "HTTP/1.1 400 Bad Request - Origin: local proxy\r\n\r\n";
  std::vector<char> buf(expected_http_response.size());
  ASSERT_TRUE(
      base::ReadFromFD(cros_client_socket_->fd(), buf.data(), buf.size()));
  std::string actual_response(buf.data(), buf.size());
  EXPECT_EQ(expected_http_response, actual_response);
}

}  // namespace system_proxy
