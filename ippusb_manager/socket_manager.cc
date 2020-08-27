// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include "ippusb_manager/socket_manager.h"

#include <errno.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

namespace ippusb_manager {

SocketManager::SocketManager(base::ScopedFD fd, struct sockaddr_un addr)
    : socket_fd_(std::move(fd)), addr_(addr) {}

SocketManager::~SocketManager() {
  socket_fd_.reset();
}

// Note: In this function we do not want to call unlink() on the socket. This is
// because the socket was created by upstart and we want it to persist.
void SocketManager::CloseSocket() {
  socket_fd_.reset();
}

bool SocketManager::GetMessage(int fd, std::string* msg) {
  uint8_t message_length;
  // Receive the length of the message which is stored in the first byte.
  if (HANDLE_EINTR(recv(fd, &message_length, 1, 0)) < 0) {
    PLOG(ERROR) << "Failed to get message length";
    return false;
  }

  auto buf = std::make_unique<char[]>(message_length);
  ssize_t gotten_size;
  size_t total_size = 0;

  while (total_size < message_length) {
    gotten_size = HANDLE_EINTR(recv(fd, buf.get() + total_size,
                                    message_length - total_size, MSG_DONTWAIT));
    if (gotten_size < 0) {
      PLOG(ERROR) << "Failed to receive message: " << std::strerror(errno);
      return false;
    }
    total_size += gotten_size;
  }

  if (total_size > 0) {
    msg->assign(buf.get(), message_length - 1);
    return true;
  }
  return false;
}

bool SocketManager::SendMessage(int fd, const std::string& msg) {
  size_t remaining = msg.size() + 1;
  size_t total = 0;

  if (remaining > std::numeric_limits<uint8_t>::max()) {
    LOG(ERROR) << "Requested message is too long to send: " << msg.size()
               << " > " << std::numeric_limits<uint8_t>::max();
    return false;
  }

  // Send the length of the message in the first byte.
  uint8_t message_length = static_cast<uint8_t>(remaining);
  if (HANDLE_EINTR(send(fd, &message_length, 1, MSG_NOSIGNAL)) < 0) {
    PLOG(ERROR) << "Failed to send message length";
    return false;
  }

  while (remaining > 0) {
    ssize_t sent =
        HANDLE_EINTR(send(fd, msg.data() + total, remaining, MSG_NOSIGNAL));
    if (sent < 0) {
      if (errno == EPIPE) {
        LOG(INFO) << "Client closed socket";
        return false;
      }
      PLOG(ERROR) << "Failed to send data over UDS";
      return false;
    }

    total += sent;
    if (sent >= remaining)
      remaining = 0;
    else
      remaining -= sent;
  }

  LOG(INFO) << "Sent " << total << " bytes";
  return true;
}

ServerSocketManager::ServerSocketManager(base::ScopedFD fd,
                                         struct sockaddr_un addr)
    : SocketManager(std::move(fd), addr) {}

bool ServerSocketManager::OpenConnection() {
  struct pollfd poll_fd;
  poll_fd.fd = GetFd();
  poll_fd.events = POLLIN;

  int retval = HANDLE_EINTR(poll(&poll_fd, 1, 0));
  if (retval < 1) {
    PLOG(INFO) << "The connection isn't ready to be opened yet";
    return false;
  }

  LOG(INFO) << "Socket is ready - attempting to connect";

  int connection_fd = HANDLE_EINTR(accept(GetFd(), nullptr, nullptr));
  if (connection_fd < 0) {
    PLOG(ERROR) << "Failed to open connection";
    return false;
  }
  connection_fd_ = base::ScopedFD(connection_fd);

  LOG(INFO) << "Connected to socket";
  return true;
}

void ServerSocketManager::CloseConnection() {
  shutdown(connection_fd_.get(), SHUT_RDWR);
  connection_fd_.reset();
}

bool ServerSocketManager::GetMessage(std::string* msg) {
  return SocketManager::GetMessage(connection_fd_.get(), msg);
}

bool ServerSocketManager::SendMessage(const std::string& msg) {
  return SocketManager::SendMessage(connection_fd_.get(), msg);
}

ClientSocketManager::ClientSocketManager(base::ScopedFD fd,
                                         struct sockaddr_un addr)
    : SocketManager(std::move(fd), addr) {}

bool ClientSocketManager::OpenConnection() {
  const struct sockaddr_un* addr = GetAddr();
  return connect(GetFd(), reinterpret_cast<const struct sockaddr*>(addr),
                 sizeof(*addr)) != -1;
}

bool ClientSocketManager::GetMessage(std::string* msg) {
  return SocketManager::GetMessage(GetFd(), msg);
}

bool ClientSocketManager::SendMessage(const std::string& msg) {
  return SocketManager::SendMessage(GetFd(), msg);
}

// static
std::unique_ptr<ServerSocketManager> ServerSocketManager::Create(
    const char* socket_path, base::ScopedFD fd) {
  // Set options for the socket.
  int val = 1;
  if (setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
    PLOG(ERROR) << "Failed to set socket options";
    return nullptr;
  }

  // Get the bound address of the opened socket.
  struct sockaddr_un addr;
  socklen_t addrlen = sizeof(addr);
  if (getsockname(fd.get(), reinterpret_cast<struct sockaddr*>(&addr),
                  &addrlen) < 0) {
    PLOG(ERROR) << "Failed to get socket name";
    return nullptr;
  }

  // Verify that the bound address is what we expect.
  if (strcmp(addr.sun_path, socket_path)) {
    LOG(ERROR) << "Bound socket " << addr.sun_path
               << " does not match expected address";
    return nullptr;
  }

  // Attempt to listen on the socket for connections.
  if (listen(fd.get(), 0)) {
    PLOG(ERROR) << "Failed to listen on socket";
    return nullptr;
  }

  return std::make_unique<ServerSocketManager>(std::move(fd), addr);
}

// static
std::unique_ptr<ClientSocketManager> ClientSocketManager::Create(
    const char* socket_path) {
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    PLOG(ERROR) << "Failed to open socket: " << socket_path;
    return nullptr;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

  return std::make_unique<ClientSocketManager>(base::ScopedFD(fd), addr);
}

}  // namespace ippusb_manager
