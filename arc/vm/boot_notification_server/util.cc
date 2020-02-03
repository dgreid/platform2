// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/boot_notification_server/util.h"

#include <linux/vm_sockets.h>
#include <string.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

constexpr size_t kChunkSize = 256;

socklen_t GetSockLen(sa_family_t family) {
  switch (family) {
    case AF_VSOCK:
      return sizeof(sockaddr_vm);
    case AF_UNIX:
      return sizeof(sockaddr_un);
    default:
      LOG(ERROR) << "Using unsupported socket type " << family;
      return sizeof(sockaddr);
  }
}

base::ScopedFD StartListening(sockaddr* addr) {
  LOG(INFO) << "Creating socket";
  base::ScopedFD fd(
      socket(addr->sa_family, SOCK_STREAM | SOCK_CLOEXEC, 0 /* protocol */));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to create socket";
    return {};
  }

  LOG(INFO) << "Binding socket";
  if (HANDLE_EINTR(bind(fd.get(), addr, GetSockLen(addr->sa_family))) == -1) {
    PLOG(ERROR) << "Failed to bind to socket address";
    return {};
  }

  LOG(INFO) << "Listening on socket";
  if (HANDLE_EINTR(listen(fd.get(), 5 /* backlog */)) == -1) {
    PLOG(ERROR) << "Failed to listen to socket";
    return {};
  }

  return fd;
}

base::ScopedFD WaitForClientConnect(int fd) {
  LOG(INFO) << "Waiting for client to connect";
  base::ScopedFD client_fd(HANDLE_EINTR(accept(fd, nullptr, nullptr)));
  if (!client_fd.is_valid()) {
    PLOG(ERROR) << "Failed to accept connection on socket";
    return {};
  }

  LOG(INFO) << "Client connected";
  return client_fd;
}

base::Optional<std::string> ReadFD(int fd) {
  std::string out;
  char buf[kChunkSize];

  while (true) {
    ssize_t len = HANDLE_EINTR(read(fd, buf, kChunkSize));
    if (len == -1) {
      PLOG(ERROR) << "Unable to read from fd " << fd;
      return base::nullopt;
    }
    if (len == 0)
      break;

    out.append(buf, len);
  }

  if (out.empty())
    return base::nullopt;

  return out;
}
