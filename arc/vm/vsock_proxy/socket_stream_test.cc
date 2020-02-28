// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/vsock_proxy/socket_stream.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/message_loop/message_loop.h>
#include <base/optional.h>
#include <base/posix/eintr_wrapper.h>
#include <base/posix/unix_domain_socket.h>
#include <base/run_loop.h>
#include <base/strings/string_piece.h>
#include <gtest/gtest.h>

#include "arc/vm/vsock_proxy/file_descriptor_util.h"

namespace arc {
namespace {

// Tests SocketStream with a socket.
class SocketStreamTest : public testing::Test {
 public:
  SocketStreamTest() = default;
  ~SocketStreamTest() override = default;

  void SetUp() override {
    auto sockets = CreateSocketPair();
    ASSERT_TRUE(sockets.has_value());
    stream_ =
        std::make_unique<SocketStream>(std::move(sockets.value().first), true,
                                       base::BindOnce([]() { ADD_FAILURE(); }));
    socket_ = std::move(sockets.value().second);
  }

 protected:
  std::unique_ptr<SocketStream> stream_;  // Paired with socket_.
  base::ScopedFD socket_;                 // Paired with stream_.

 private:
  base::MessageLoopForIO message_loop_;
  base::FileDescriptorWatcher watcher_{&message_loop_};

  DISALLOW_COPY_AND_ASSIGN(SocketStreamTest);
};

TEST_F(SocketStreamTest, Read) {
  base::ScopedFD attached_fd(HANDLE_EINTR(open("/dev/null", O_RDONLY)));
  ASSERT_TRUE(attached_fd.is_valid());

  constexpr char kData[] = "abcdefghijklmnopqrstuvwxyz";
  ASSERT_TRUE(base::UnixDomainSocket::SendMsg(
      socket_.get(), kData, sizeof(kData), {attached_fd.get()}));

  auto read_result = stream_->Read();
  EXPECT_EQ(0, read_result.error_code);
  EXPECT_EQ(base::StringPiece(kData, sizeof(kData)), read_result.blob);
  EXPECT_EQ(1, read_result.fds.size());
}

TEST_F(SocketStreamTest, ReadEOF) {
  // Close the other side immediately.
  socket_.reset();

  auto read_result = stream_->Read();
  EXPECT_EQ(0, read_result.error_code);
  EXPECT_TRUE(read_result.blob.empty());
  EXPECT_TRUE(read_result.fds.empty());
}

TEST_F(SocketStreamTest, ReadError) {
  // Pass invalid FD.
  auto read_result =
      SocketStream(base::ScopedFD(), true, base::BindOnce([]() {})).Read();
  EXPECT_EQ(EBADF, read_result.error_code);
}

TEST_F(SocketStreamTest, Write) {
  base::ScopedFD attached_fd(HANDLE_EINTR(open("/dev/null", O_RDONLY)));
  ASSERT_TRUE(attached_fd.is_valid());

  constexpr char kData[] = "abcdefghijklmnopqrstuvwxyz";
  {
    std::vector<base::ScopedFD> fds;
    fds.emplace_back(std::move(attached_fd));
    ASSERT_TRUE(
        stream_->Write(std::string(kData, sizeof(kData)), std::move(fds)));
  }
  std::string read_data;
  read_data.resize(sizeof(kData));
  std::vector<base::ScopedFD> fds;
  ASSERT_EQ(sizeof(kData),
            base::UnixDomainSocket::RecvMsg(socket_.get(), &read_data[0],
                                            sizeof(kData), &fds));
  EXPECT_EQ(1, fds.size());
}

TEST_F(SocketStreamTest, PendingWrite) {
  int sndbuf_value = 0;
  socklen_t len = sizeof(sndbuf_value);
  ASSERT_EQ(
      0, getsockopt(socket_.get(), SOL_SOCKET, SO_SNDBUF, &sndbuf_value, &len));

  const std::string data1(sndbuf_value, 'a');
  const std::string data2(sndbuf_value, 'b');
  const std::string data3(1, 'c');

  std::vector<base::ScopedFD> attached_fds;
  attached_fds.emplace_back(HANDLE_EINTR(open("/dev/null", O_RDONLY)));
  ASSERT_TRUE(attached_fds.back().is_valid());

  // Write data1, data2, and data3 (with a FD) to the stream.
  ASSERT_TRUE(stream_->Write(data1, {}));
  ASSERT_TRUE(stream_->Write(data2, {}));
  ASSERT_TRUE(stream_->Write(data3, std::move(attached_fds)));

  // Read data1 from the other socket.
  std::string read_data;
  read_data.resize(sndbuf_value);
  std::vector<base::ScopedFD> fds;
  ASSERT_EQ(data1.size(),
            base::UnixDomainSocket::RecvMsg(socket_.get(), &read_data[0],
                                            read_data.size(), &fds));
  read_data.resize(data1.size());
  EXPECT_EQ(data1, read_data);

  // data2 is still pending.
  ASSERT_EQ(-1, base::UnixDomainSocket::RecvMsg(socket_.get(), &read_data[0],
                                                read_data.size(), &fds));
  ASSERT_EQ(EAGAIN, errno);

  // Now the socket's buffer is empty. Let the stream write data2 to the socket.
  base::RunLoop().RunUntilIdle();

  // Read data2 from the other socket.
  read_data.resize(sndbuf_value);
  ASSERT_EQ(data2.size(),
            base::UnixDomainSocket::RecvMsg(socket_.get(), &read_data[0],
                                            read_data.size(), &fds));
  read_data.resize(data2.size());
  EXPECT_EQ(data2, read_data);

  // data3 is still pending.
  ASSERT_EQ(-1, base::UnixDomainSocket::RecvMsg(socket_.get(), &read_data[0],
                                                read_data.size(), &fds));
  ASSERT_EQ(EAGAIN, errno);

  // Let the stream write data3 to the socket.
  base::RunLoop().RunUntilIdle();

  // Read data3 from the other socket.
  read_data.resize(sndbuf_value);
  ASSERT_EQ(data3.size(),
            base::UnixDomainSocket::RecvMsg(socket_.get(), &read_data[0],
                                            read_data.size(), &fds));
  read_data.resize(data3.size());
  EXPECT_EQ(data3, read_data);
  EXPECT_EQ(1, fds.size());
}

TEST_F(SocketStreamTest, WriteError) {
  constexpr char kData[] = "abcdefghijklmnopqrstuvwxyz";
  bool error_handler_was_run = false;
  base::OnceClosure error_handler =
      base::BindOnce([](bool* run) { *run = true; }, &error_handler_was_run);
  // Write to an invalid FD.
  SocketStream(base::ScopedFD(), true, std::move(error_handler))
      .Write(kData, {});
  EXPECT_TRUE(error_handler_was_run);
}

// Tests SocketStream with a pipe.
class PipeStreamTest : public testing::Test {
 public:
  PipeStreamTest() = default;
  ~PipeStreamTest() override = default;

  void SetUp() override {
    auto pipes = CreatePipe();
    ASSERT_TRUE(pipes.has_value());
    std::tie(read_fd_, write_fd_) = std::move(pipes.value());
  }

 protected:
  base::ScopedFD read_fd_;
  base::ScopedFD write_fd_;

 private:
  base::MessageLoopForIO message_loop_;
  base::FileDescriptorWatcher watcher_{&message_loop_};

  DISALLOW_COPY_AND_ASSIGN(PipeStreamTest);
};

TEST_F(PipeStreamTest, Read) {
  constexpr char kData[] = "abcdefghijklmnopqrstuvwxyz";
  ASSERT_TRUE(base::WriteFileDescriptor(write_fd_.get(), kData, sizeof(kData)));

  auto read_result = SocketStream(std::move(read_fd_), false,
                                  base::BindOnce([]() { ADD_FAILURE(); }))
                         .Read();
  EXPECT_EQ(0, read_result.error_code);
  EXPECT_EQ(base::StringPiece(kData, sizeof(kData)), read_result.blob);
  EXPECT_TRUE(read_result.fds.empty());
}

TEST_F(PipeStreamTest, ReadEOF) {
  // Close the write end immediately.
  write_fd_.reset();

  auto read_result = SocketStream(std::move(read_fd_), false,
                                  base::BindOnce([]() { ADD_FAILURE(); }))
                         .Read();
  EXPECT_EQ(0, read_result.error_code);
  EXPECT_TRUE(read_result.blob.empty());
  EXPECT_TRUE(read_result.fds.empty());
}

TEST_F(PipeStreamTest, ReadError) {
  // Pass invalid FD.
  auto read_result = SocketStream(base::ScopedFD(), false,
                                  base::BindOnce([]() { ADD_FAILURE(); }))
                         .Read();
  EXPECT_EQ(EBADF, read_result.error_code);
}

TEST_F(PipeStreamTest, Write) {
  constexpr char kData[] = "abcdefghijklmnopqrstuvwxyz";
  ASSERT_TRUE(SocketStream(std::move(write_fd_), false,
                           base::BindOnce([]() { ADD_FAILURE(); }))
                  .Write(std::string(kData, sizeof(kData)), {}));

  std::string read_data;
  read_data.resize(sizeof(kData));
  ASSERT_TRUE(base::ReadFromFD(read_fd_.get(), &read_data[0], sizeof(kData)));
  EXPECT_EQ(std::string(kData, sizeof(kData)), read_data);
}

TEST_F(PipeStreamTest, WriteFD) {
  base::ScopedFD attached_fd(HANDLE_EINTR(open("/dev/null", O_RDONLY)));
  ASSERT_TRUE(attached_fd.is_valid());

  constexpr char kData[] = "abcdefghijklmnopqrstuvwxyz";
  std::vector<base::ScopedFD> fds;
  fds.emplace_back(std::move(attached_fd));
  // Not supported.
  bool error_handler_was_run = false;
  base::OnceClosure error_handler =
      base::BindOnce([](bool* run) { *run = true; }, &error_handler_was_run);
  EXPECT_TRUE(
      SocketStream(std::move(write_fd_), false, std::move(error_handler))
          .Write(std::string(kData, sizeof(kData)), std::move(fds)));
  EXPECT_TRUE(error_handler_was_run);
}

TEST_F(PipeStreamTest, PendingWrite) {
  const int pipe_size = HANDLE_EINTR(fcntl(write_fd_.get(), F_GETPIPE_SZ));
  ASSERT_NE(-1, pipe_size);

  SocketStream stream(std::move(write_fd_), false,
                      base::BindOnce([]() { ADD_FAILURE(); }));

  const std::string data1(pipe_size, 'a');
  const std::string data2(pipe_size, 'b');
  const std::string data3(pipe_size, 'c');

  // Write data1, data2, and data3 to the stream.
  ASSERT_TRUE(stream.Write(data1, {}));
  ASSERT_TRUE(stream.Write(data2, {}));
  ASSERT_TRUE(stream.Write(data3, {}));

  // Read data1 from the pipe.
  std::string read_data;
  read_data.resize(pipe_size);
  ASSERT_EQ(data1.size(), HANDLE_EINTR(read(read_fd_.get(), &read_data[0],
                                            read_data.size())));
  read_data.resize(data1.size());
  EXPECT_EQ(data1, read_data);

  // data2 is still pending.
  ASSERT_EQ(
      -1, HANDLE_EINTR(read(read_fd_.get(), &read_data[0], read_data.size())));
  ASSERT_EQ(EAGAIN, errno);

  // Now the pipe's buffer is empty. Let the stream write data2 to the pipe.
  base::RunLoop().RunUntilIdle();

  // Read data2 from the pipe.
  read_data.resize(pipe_size);
  ASSERT_EQ(data2.size(), HANDLE_EINTR(read(read_fd_.get(), &read_data[0],
                                            read_data.size())));
  read_data.resize(data2.size());
  EXPECT_EQ(data2, read_data);

  // data3 is still pending.
  ASSERT_EQ(
      -1, HANDLE_EINTR(read(read_fd_.get(), &read_data[0], read_data.size())));
  ASSERT_EQ(EAGAIN, errno);

  // Let the stream write data3 to the pipe.
  base::RunLoop().RunUntilIdle();

  // Read data3 from the pipe.
  read_data.resize(pipe_size);
  ASSERT_EQ(data3.size(), HANDLE_EINTR(read(read_fd_.get(), &read_data[0],
                                            read_data.size())));
  read_data.resize(data3.size());
  EXPECT_EQ(data3, read_data);
}

}  // namespace
}  // namespace arc
