// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/vsock_proxy/vsock_proxy.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/macros.h>
#include <base/message_loop/message_loop.h>
#include <base/optional.h>
#include <base/posix/unix_domain_socket.h>
#include <base/run_loop.h>
#include <gtest/gtest.h>

#include "arc/vm/vsock_proxy/file_descriptor_util.h"
#include "arc/vm/vsock_proxy/message.pb.h"

namespace arc {
namespace {

class TestDelegate : public VSockProxy::Delegate {
 public:
  explicit TestDelegate(VSockProxy::Type type) : type_(type) {}
  ~TestDelegate() override = default;

  bool is_stopped() const { return is_stopped_; }

  VSockProxy::Type GetType() const override { return type_; }
  bool ConvertFileDescriptorToProto(int fd,
                                    arc_proxy::FileDescriptor* proto) override {
    NOTREACHED();
    return false;
  }
  base::ScopedFD ConvertProtoToFileDescriptor(
      const arc_proxy::FileDescriptor& proto) override {
    NOTREACHED();
    return {};
  }
  void OnStopped() override { is_stopped_ = true; }

 private:
  const VSockProxy::Type type_;
  bool is_stopped_ = false;
};

class VSockProxyTest : public testing::Test {
 public:
  VSockProxyTest() = default;
  ~VSockProxyTest() override = default;

  void SetUp() override {
    // Use a blocking socket pair instead of VSOCK for testing.
    auto vsock_pair = CreateSocketPair(SOCK_STREAM);
    ASSERT_TRUE(vsock_pair.has_value());
    server_ = std::make_unique<VSockProxy>(&server_delegate_,
                                           std::move(vsock_pair->first));
    client_ = std::make_unique<VSockProxy>(&client_delegate_,
                                           std::move(vsock_pair->second));

    // Register initial socket pairs.
    auto server_socket_pair = CreateSocketPair(SOCK_STREAM | SOCK_NONBLOCK);
    ASSERT_TRUE(server_socket_pair.has_value());
    auto client_socket_pair = CreateSocketPair(SOCK_STREAM | SOCK_NONBLOCK);
    ASSERT_TRUE(client_socket_pair.has_value());

    int64_t handle = server_->RegisterFileDescriptor(
        std::move(server_socket_pair->first),
        arc_proxy::FileDescriptor::SOCKET_STREAM, 0 /* handle */);
    server_fd_ = std::move(server_socket_pair->second);

    client_->RegisterFileDescriptor(std::move(client_socket_pair->first),
                                    arc_proxy::FileDescriptor::SOCKET_STREAM,
                                    handle);
    client_fd_ = std::move(client_socket_pair->second);
  }

  void TearDown() override {
    client_fd_.reset();
    server_fd_.reset();
    client_ = nullptr;
    server_ = nullptr;
  }

  VSockProxy* server() { return server_.get(); }
  VSockProxy* client() { return client_.get(); }

  TestDelegate& server_delegate() { return server_delegate_; }
  TestDelegate& client_delegate() { return client_delegate_; }

  int server_fd() const { return server_fd_.get(); }
  int client_fd() const { return client_fd_.get(); }

  void ResetServerFD() { server_fd_.reset(); }
  void ResetClientFD() { client_fd_.reset(); }

  void ResetServer() { server_.reset(); }
  void ResetClient() { client_.reset(); }

 private:
  base::MessageLoopForIO message_loop_;
  base::FileDescriptorWatcher watcher_{&message_loop_};

  TestDelegate server_delegate_{VSockProxy::Type::SERVER};
  TestDelegate client_delegate_{VSockProxy::Type::CLIENT};

  std::unique_ptr<VSockProxy> server_;
  std::unique_ptr<VSockProxy> client_;

  base::ScopedFD server_fd_;
  base::ScopedFD client_fd_;

  DISALLOW_COPY_AND_ASSIGN(VSockProxyTest);
};

// Runs the message loop until the given |fd| gets read ready.
void WaitUntilReadable(int fd) {
  base::RunLoop run_loop;
  auto controller =
      base::FileDescriptorWatcher::WatchReadable(fd, run_loop.QuitClosure());
  run_loop.Run();
}

// Exercises if simple data tranferring from |write_fd| to |read_fd| works.
void TestDataTransfer(int write_fd, int read_fd) {
  constexpr char kData[] = "abcdefg";
  if (!base::UnixDomainSocket::SendMsg(write_fd, kData, sizeof(kData), {})) {
    ADD_FAILURE() << "Failed to send message.";
    return;
  }

  WaitUntilReadable(read_fd);
  char buf[256];
  std::vector<base::ScopedFD> fds;
  ssize_t size =
      base::UnixDomainSocket::RecvMsg(read_fd, buf, sizeof(buf), &fds);
  EXPECT_EQ(size, sizeof(kData));
  EXPECT_STREQ(buf, kData);
  EXPECT_TRUE(fds.empty());
}

// Checks if EOF is read from the give socket |fd|.
void ExpectSocketEof(int fd) {
  char buf[256];
  std::vector<base::ScopedFD> fds;
  ssize_t size = base::UnixDomainSocket::RecvMsg(fd, buf, sizeof(buf), &fds);
  EXPECT_EQ(size, 0);
  EXPECT_TRUE(fds.empty());
}

TEST_F(VSockProxyTest, ServerToClient) {
  TestDataTransfer(server_fd(), client_fd());
}

TEST_F(VSockProxyTest, ClientToServer) {
  TestDataTransfer(client_fd(), server_fd());
}

TEST_F(VSockProxyTest, CloseServer) {
  ResetServerFD();
  WaitUntilReadable(client_fd());
  ExpectSocketEof(client_fd());
}

TEST_F(VSockProxyTest, CloseClient) {
  ResetClientFD();
  WaitUntilReadable(server_fd());
  ExpectSocketEof(server_fd());
}

TEST_F(VSockProxyTest, ResetServer) {
  ResetServer();
  EXPECT_TRUE(server_delegate().is_stopped());
  WaitUntilReadable(client_fd());
  ExpectSocketEof(client_fd());
  EXPECT_TRUE(client_delegate().is_stopped());
}

TEST_F(VSockProxyTest, ResetClient) {
  ResetClient();
  EXPECT_TRUE(client_delegate().is_stopped());
  WaitUntilReadable(server_fd());
  ExpectSocketEof(server_fd());
  EXPECT_TRUE(server_delegate().is_stopped());
}

TEST_F(VSockProxyTest, FileWriteError) {
  // Register a socket pair to the server.
  auto server_socket_pair = CreateSocketPair(SOCK_STREAM | SOCK_NONBLOCK);
  ASSERT_TRUE(server_socket_pair.has_value());
  int64_t handle = server()->RegisterFileDescriptor(
      std::move(server_socket_pair->first),
      arc_proxy::FileDescriptor::SOCKET_STREAM, 0 /* handle */);
  auto server_fd = std::move(server_socket_pair->second);

  // Register a read only FD to the client. This will cause a write error.
  base::ScopedFD null_fd(HANDLE_EINTR(open("/dev/null", O_RDONLY)));
  ASSERT_TRUE(null_fd.is_valid());
  client()->RegisterFileDescriptor(
      std::move(null_fd), arc_proxy::FileDescriptor::FIFO_READ, handle);

  // Try to send data from the server to the client, but it fails because of a
  // write error in the client.
  constexpr char kData[] = "abcdefg";
  ASSERT_TRUE(base::WriteFileDescriptor(server_fd.get(), kData, sizeof(kData)));
  // Write error on the client results in closing the server socket.
  WaitUntilReadable(server_fd.get());
  ExpectSocketEof(server_fd.get());
}

TEST_F(VSockProxyTest, PassStreamSocketFromServer) {
  auto sockpair = CreateSocketPair(SOCK_STREAM | SOCK_NONBLOCK);
  ASSERT_TRUE(sockpair.has_value());
  constexpr char kData[] = "testdata";
  if (!base::UnixDomainSocket::SendMsg(server_fd(), kData, sizeof(kData),
                                       {sockpair->second.get()})) {
    ADD_FAILURE() << "Failed to send message.";
    return;
  }
  sockpair->second.reset();

  base::ScopedFD received_fd;
  {
    WaitUntilReadable(client_fd());
    char buf[256];
    std::vector<base::ScopedFD> fds;
    ssize_t size =
        base::UnixDomainSocket::RecvMsg(client_fd(), buf, sizeof(buf), &fds);
    EXPECT_EQ(sizeof(kData), size);
    EXPECT_STREQ(kData, buf);
    EXPECT_EQ(1, fds.size());
    received_fd = std::move(fds[0]);
  }
  EXPECT_EQ(SOCK_STREAM, GetSocketType(received_fd.get()));
  TestDataTransfer(sockpair->first.get(), received_fd.get());
  TestDataTransfer(received_fd.get(), sockpair->first.get());
}

TEST_F(VSockProxyTest, PassStreamSocketSocketFromClient) {
  auto sockpair = CreateSocketPair(SOCK_STREAM | SOCK_NONBLOCK);
  ASSERT_TRUE(sockpair.has_value());
  constexpr char kData[] = "testdata";
  if (!base::UnixDomainSocket::SendMsg(client_fd(), kData, sizeof(kData),
                                       {sockpair->second.get()})) {
    ADD_FAILURE() << "Failed to send message.";
    return;
  }
  sockpair->second.reset();

  base::ScopedFD received_fd;
  {
    WaitUntilReadable(server_fd());
    char buf[256];
    std::vector<base::ScopedFD> fds;
    ssize_t size =
        base::UnixDomainSocket::RecvMsg(server_fd(), buf, sizeof(buf), &fds);
    EXPECT_EQ(sizeof(kData), size);
    EXPECT_STREQ(kData, buf);
    EXPECT_EQ(1, fds.size());
    received_fd = std::move(fds[0]);
  }
  EXPECT_EQ(SOCK_STREAM, GetSocketType(received_fd.get()));
  TestDataTransfer(sockpair->first.get(), received_fd.get());
  TestDataTransfer(received_fd.get(), sockpair->first.get());
}

TEST_F(VSockProxyTest, PassDgramSocketFromServer) {
  auto sockpair = CreateSocketPair(SOCK_DGRAM | SOCK_NONBLOCK);
  ASSERT_TRUE(sockpair.has_value());
  constexpr char kData[] = "testdata";
  if (!base::UnixDomainSocket::SendMsg(server_fd(), kData, sizeof(kData),
                                       {sockpair->second.get()})) {
    ADD_FAILURE() << "Failed to send message.";
    return;
  }
  sockpair->second.reset();

  base::ScopedFD received_fd;
  {
    WaitUntilReadable(client_fd());
    char buf[256];
    std::vector<base::ScopedFD> fds;
    ssize_t size =
        base::UnixDomainSocket::RecvMsg(client_fd(), buf, sizeof(buf), &fds);
    EXPECT_EQ(sizeof(kData), size);
    EXPECT_STREQ(kData, buf);
    EXPECT_EQ(1, fds.size());
    received_fd = std::move(fds[0]);
  }
  EXPECT_EQ(SOCK_DGRAM, GetSocketType(received_fd.get()));
  TestDataTransfer(sockpair->first.get(), received_fd.get());
  TestDataTransfer(received_fd.get(), sockpair->first.get());
}

TEST_F(VSockProxyTest, PassSeqpacketSocketFromServer) {
  auto sockpair = CreateSocketPair(SOCK_SEQPACKET | SOCK_NONBLOCK);
  ASSERT_TRUE(sockpair.has_value());
  constexpr char kData[] = "testdata";
  if (!base::UnixDomainSocket::SendMsg(server_fd(), kData, sizeof(kData),
                                       {sockpair->second.get()})) {
    ADD_FAILURE() << "Failed to send message.";
    return;
  }
  sockpair->second.reset();

  base::ScopedFD received_fd;
  {
    WaitUntilReadable(client_fd());
    char buf[256];
    std::vector<base::ScopedFD> fds;
    ssize_t size =
        base::UnixDomainSocket::RecvMsg(client_fd(), buf, sizeof(buf), &fds);
    EXPECT_EQ(sizeof(kData), size);
    EXPECT_STREQ(kData, buf);
    EXPECT_EQ(1, fds.size());
    received_fd = std::move(fds[0]);
  }
  EXPECT_EQ(SOCK_SEQPACKET, GetSocketType(received_fd.get()));
  TestDataTransfer(sockpair->first.get(), received_fd.get());
  TestDataTransfer(received_fd.get(), sockpair->first.get());
}

TEST_F(VSockProxyTest, Connect) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath socket_path = temp_dir.GetPath().Append("test.sock");

  // Create unix domain socket for testing, which is connected by the following
  // Connect() invocation from client side.
  auto server_sock = CreateUnixDomainSocket(socket_path);

  // Try to follow the actual initial connection procedure.
  base::RunLoop run_loop;
  base::Optional<int> error_code;
  base::Optional<int64_t> handle;
  client()->Connect(socket_path, base::BindOnce(
                                     [](base::RunLoop* run_loop,
                                        base::Optional<int>* error_code_out,
                                        base::Optional<int64_t>* handle_out,
                                        int error_code, int64_t handle) {
                                       *error_code_out = error_code;
                                       *handle_out = handle;
                                       run_loop->Quit();
                                     },
                                     &run_loop, &error_code, &handle));
  run_loop.Run();
  ASSERT_EQ(0, error_code);
  ASSERT_TRUE(handle.has_value());
  // TODO(hidehiko): Remove the cast on next libchrome uprev.
  ASSERT_TRUE(handle != static_cast<int64_t>(0));

  // Register client side socket.
  auto client_sock_pair = CreateSocketPair(SOCK_STREAM | SOCK_NONBLOCK);
  ASSERT_TRUE(client_sock_pair.has_value());
  client()->RegisterFileDescriptor(std::move(client_sock_pair->first),
                                   arc_proxy::FileDescriptor::SOCKET_STREAM,
                                   handle.value());

  auto client_fd = std::move(client_sock_pair->second);
  auto server_fd = AcceptSocket(server_sock.get());
  ASSERT_TRUE(server_fd.is_valid());

  TestDataTransfer(client_fd.get(), server_fd.get());
  TestDataTransfer(server_fd.get(), client_fd.get());
}

TEST_F(VSockProxyTest, Pread) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath file_path = temp_dir.GetPath().Append("test.txt");
  constexpr char kFileContent[] = "abcdefghijklmnopqrstuvwxyz";
  // Trim trailing '\0'.
  ASSERT_EQ(sizeof(kFileContent) - 1,
            base::WriteFile(file_path, kFileContent, sizeof(kFileContent) - 1));

  base::ScopedFD fd(HANDLE_EINTR(open(file_path.value().c_str(), O_RDONLY)));
  ASSERT_TRUE(fd.is_valid());
  const int64_t handle = client()->RegisterFileDescriptor(
      std::move(fd), arc_proxy::FileDescriptor::REGULAR_FILE, 0);

  base::RunLoop run_loop;
  server()->Pread(
      handle, 10, 10,
      base::BindOnce(
          [](base::RunLoop* run_loop, int error_code, const std::string& blob) {
            run_loop->Quit();
            EXPECT_EQ(0, error_code);
            EXPECT_EQ("klmnopqrst", blob);
          },
          &run_loop));
  run_loop.Run();
}

TEST_F(VSockProxyTest, Pread_UnknownHandle) {
  constexpr int64_t kUnknownHandle = 100;
  base::RunLoop run_loop;
  server()->Pread(
      kUnknownHandle, 10, 10,
      base::BindOnce(
          [](base::RunLoop* run_loop, int error_code, const std::string& blob) {
            run_loop->Quit();
            EXPECT_EQ(EBADF, error_code);
          },
          &run_loop));
  run_loop.Run();
}

TEST_F(VSockProxyTest, Fstat) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath file_path = temp_dir.GetPath().Append("test.txt");
  constexpr char kFileContent[] = "abcdefghijklmnopqrstuvwxyz";
  // Trim trailing '\0'.
  constexpr size_t kContentSize = sizeof(kFileContent) - 1;
  ASSERT_EQ(kContentSize,
            base::WriteFile(file_path, kFileContent, kContentSize));

  base::ScopedFD fd(HANDLE_EINTR(open(file_path.value().c_str(), O_RDONLY)));
  ASSERT_TRUE(fd.is_valid());
  const int64_t handle = client()->RegisterFileDescriptor(
      std::move(fd), arc_proxy::FileDescriptor::REGULAR_FILE, 0);

  base::RunLoop run_loop;
  server()->Fstat(
      handle, base::BindOnce(
                  [](base::RunLoop* run_loop, int error_code, int64_t size) {
                    run_loop->Quit();
                    EXPECT_EQ(0, error_code);
                    EXPECT_EQ(26, size);
                  },
                  &run_loop));
  run_loop.Run();
}

TEST_F(VSockProxyTest, Fstat_Unsupported) {
  auto sockpair = CreateSocketPair(SOCK_STREAM | SOCK_NONBLOCK);
  ASSERT_TRUE(sockpair.has_value());
  ASSERT_TRUE(sockpair->first.is_valid());
  const int64_t handle = client()->RegisterFileDescriptor(
      std::move(sockpair->first), arc_proxy::FileDescriptor::SOCKET_STREAM, 0);

  base::RunLoop run_loop;
  server()->Fstat(
      handle, base::BindOnce(
                  [](base::RunLoop* run_loop, int error_code, int64_t size) {
                    run_loop->Quit();
                    EXPECT_EQ(EOPNOTSUPP, error_code);
                  },
                  &run_loop));
  run_loop.Run();
}

TEST_F(VSockProxyTest, Fstat_UnknownHandle) {
  constexpr int64_t kUnknownHandle = 100;
  base::RunLoop run_loop;
  server()->Fstat(kUnknownHandle, base::BindOnce(
                                      [](base::RunLoop* run_loop,
                                         int error_code, int64_t size) {
                                        run_loop->Quit();
                                        EXPECT_EQ(EBADF, error_code);
                                      },
                                      &run_loop));
  run_loop.Run();
}

}  // namespace
}  // namespace arc
