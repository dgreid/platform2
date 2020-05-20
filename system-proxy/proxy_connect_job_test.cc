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

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback_helpers.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/test/test_mock_time_task_runner.h>
#include <brillo/message_loops/base_message_loop.h>
#include <chromeos/patchpanel/socket.h>
#include <chromeos/patchpanel/socket_forwarder.h>

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
        std::make_unique<patchpanel::Socket>(base::ScopedFD(fds[1]));

    connect_job_ = std::make_unique<ProxyConnectJob>(
        std::make_unique<patchpanel::Socket>(base::ScopedFD(fds[0])), "",
        base::BindOnce(&ProxyConnectJobTest::ResolveProxy,
                       base::Unretained(this)),
        base::BindOnce(&ProxyConnectJobTest::OnConnectionSetupFinished,
                       base::Unretained(this)));
  }

 protected:
  void ResolveProxy(
      const std::string& target_url,
      base::OnceCallback<void(const std::list<std::string>&)> callback) {
    std::move(callback).Run({kProxyServerUrl});
  }

  void OnConnectionSetupFinished(
      std::unique_ptr<patchpanel::SocketForwarder> fwd,
      ProxyConnectJob* connect_job) {
    ASSERT_EQ(connect_job, connect_job_.get());
  }

  std::unique_ptr<ProxyConnectJob> connect_job_;
  base::MessageLoopForIO loop_;
  brillo::BaseMessageLoop brillo_loop_{&loop_};
  std::unique_ptr<patchpanel::Socket> cros_client_socket_;

 private:
  FRIEND_TEST(ProxyConnectJobTest, ClientConnectTimeoutJobCanceled);
};

TEST_F(ProxyConnectJobTest, SuccessfulConnection) {
  connect_job_->Start();
  char validConnRequest[] =
      "CONNECT www.example.server.com:443 HTTP/1.1\r\n\r\n";
  cros_client_socket_->SendTo(validConnRequest, std::strlen(validConnRequest));
  brillo_loop_.RunOnce(false);

  EXPECT_EQ("www.example.server.com:443", connect_job_->target_url_);
  EXPECT_EQ(1, connect_job_->proxy_servers_.size());
  EXPECT_EQ(kProxyServerUrl, connect_job_->proxy_servers_.front());
}

TEST_F(ProxyConnectJobTest, SuccessfulConnectionAltEnding) {
  connect_job_->Start();
  char validConnRequest[] =
      "CONNECT www.example.server.com:443 HTTP/1.1\r\n\n";
  cros_client_socket_->SendTo(validConnRequest, std::strlen(validConnRequest));
  brillo_loop_.RunOnce(false);

  EXPECT_EQ("www.example.server.com:443", connect_job_->target_url_);
  EXPECT_EQ(1, connect_job_->proxy_servers_.size());
  EXPECT_EQ(kProxyServerUrl, connect_job_->proxy_servers_.front());
}

TEST_F(ProxyConnectJobTest, BadHttpRequestWrongMethod) {
  connect_job_->Start();
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
  connect_job_->Start();
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

TEST_F(ProxyConnectJobTest, WaitClientConnectTimeout) {
  // Add a TaskRunner where we can control time.
  scoped_refptr<base::TestMockTimeTaskRunner> task_runner{
      new base::TestMockTimeTaskRunner()};
  loop_.SetTaskRunner(task_runner);
  base::TestMockTimeTaskRunner::ScopedContext scoped_context(task_runner.get());

  connect_job_->Start();

  EXPECT_EQ(1, task_runner->GetPendingTaskCount());
  // Move the time ahead so that the client connection timeout callback is
  // triggered.
  task_runner->FastForwardBy(task_runner->NextPendingTaskDelay());

  const std::string expected_http_response =
      "HTTP/1.1 408 Request Timeout - Origin: local proxy\r\n\r\n";
  std::vector<char> buf(expected_http_response.size());
  ASSERT_TRUE(
      base::ReadFromFD(cros_client_socket_->fd(), buf.data(), buf.size()));
  std::string actual_response(buf.data(), buf.size());

  EXPECT_EQ(expected_http_response, actual_response);
}

// Check that the client connect timeout callback is not fired if the owning
// proxy connect job is destroyed.
TEST_F(ProxyConnectJobTest, ClientConnectTimeoutJobCanceled) {
  // Add a TaskRunner where we can control time.
  scoped_refptr<base::TestMockTimeTaskRunner> task_runner{
      new base::TestMockTimeTaskRunner()};
  loop_.SetTaskRunner(task_runner);
  base::TestMockTimeTaskRunner::ScopedContext scoped_context(task_runner.get());

  // Create a proxy connect job and start the client connect timeout counter.
  {
    int fds[2];
    ASSERT_NE(-1,
              socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                         0 /* protocol */, fds));
    auto client_socket =
        std::make_unique<patchpanel::Socket>(base::ScopedFD(fds[1]));

    auto connect_job = std::make_unique<ProxyConnectJob>(
        std::make_unique<patchpanel::Socket>(base::ScopedFD(fds[0])), "",
        base::BindOnce(&ProxyConnectJobTest::ResolveProxy,
                       base::Unretained(this)),
        base::BindOnce(&ProxyConnectJobTest::OnConnectionSetupFinished,
                       base::Unretained(this)));
    // Post the timeout task.
    connect_job->Start();
    EXPECT_TRUE(task_runner->HasPendingTask());
  }
  // Check that the task was canceled.
  EXPECT_FALSE(task_runner->HasPendingTask());
}

}  // namespace system_proxy
