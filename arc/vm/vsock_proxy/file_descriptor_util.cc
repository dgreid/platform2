// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/vsock_proxy/file_descriptor_util.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Needs to be included after sys/socket.h
#include <linux/un.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

namespace arc {
namespace {

bool ToSockAddr(const base::FilePath& path, struct sockaddr_un* sa) {
  // sun_path needs to include trailing '\0' byte.
  if (path.value().size() >= sizeof(sa->sun_path)) {
    LOG(ERROR) << "Path is too long: " << path.value();
    return false;
  }

  memset(sa, 0, sizeof(*sa));
  sa->sun_family = AF_UNIX;
  strncpy(sa->sun_path, path.value().c_str(), sizeof(sa->sun_path) - 1);
  return true;
}

}  // namespace

base::Optional<std::pair<base::ScopedFD, base::ScopedFD>> CreatePipe() {
  int fds[2];
  if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) == -1) {
    PLOG(ERROR) << "Failed to create pipe";
    return base::nullopt;
  }

  return base::make_optional(
      std::make_pair(base::ScopedFD(fds[0]), base::ScopedFD(fds[1])));
}

base::Optional<std::pair<base::ScopedFD, base::ScopedFD>> CreateSocketPair(
    int type) {
  int fds[2];
  if (socketpair(AF_UNIX, type | SOCK_CLOEXEC, 0 /* protocol */, fds) == -1) {
    PLOG(ERROR) << "Failed to create socketpair";
    return base::nullopt;
  }

  return base::make_optional(
      std::make_pair(base::ScopedFD(fds[0]), base::ScopedFD(fds[1])));
}

base::ScopedFD CreateUnixDomainSocket(const base::FilePath& path) {
  LOG(INFO) << "Creating " << path.value();

  struct sockaddr_un sa;
  if (!ToSockAddr(path, &sa))
    return {};

  base::ScopedFD fd(
      socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 /* protocol */));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to create unix domain socket";
    return {};
  }

  // Remove stale file first. Ignore the error intentionally.
  base::DeleteFile(path, false /* recursive */);

  if (bind(fd.get(), reinterpret_cast<const struct sockaddr*>(&sa),
           sizeof(sa)) == -1) {
    PLOG(ERROR) << "Failed to bind a unix domain socket";
    return {};
  }

  if (fchmod(fd.get(), 0666) == -1) {
    PLOG(ERROR) << "Failed to set permission";
    return {};
  }

  if (listen(fd.get(), 5 /* backlog */) == -1) {
    PLOG(ERROR) << "Failed to start listening a socket";
    return {};
  }

  LOG(INFO) << path.value() << " created.";
  return fd;
}

base::ScopedFD AcceptSocket(int raw_fd) {
  base::ScopedFD fd(
      HANDLE_EINTR(accept4(raw_fd, nullptr, nullptr, SOCK_CLOEXEC)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to accept() unix domain socket";
    return {};
  }
  return fd;
}

std::pair<int, base::ScopedFD> ConnectUnixDomainSocket(
    const base::FilePath& path) {
  LOG(INFO) << "Connecting to " << path.value();

  struct sockaddr_un sa;
  if (!ToSockAddr(path, &sa))
    return std::make_pair(EFAULT, base::ScopedFD());

  base::ScopedFD fd(
      socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 /* protocol */));
  if (!fd.is_valid()) {
    int result_errno = errno;
    PLOG(ERROR) << "Failed to create unix domain socket";
    return std::make_pair(result_errno, base::ScopedFD());
  }

  if (HANDLE_EINTR(connect(fd.get(),
                           reinterpret_cast<const struct sockaddr*>(&sa),
                           sizeof(sa))) == -1) {
    int result_errno = errno;
    PLOG(ERROR) << "Failed to connect.";
    return std::make_pair(result_errno, base::ScopedFD());
  }

  LOG(INFO) << "Connected to " << path.value();
  return std::make_pair(0, std::move(fd));
}

int GetSocketType(int fd) {
  int type = 0;
  socklen_t length = sizeof(type);
  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &length) == -1) {
    PLOG(ERROR) << "getsockopt failed";
    return -1;
  }
  return type;
}

}  // namespace arc
