// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <utility>

#include <base/logging.h>
#include <base/memory/ptr_util.h>

#include "arc/network/net_util.h"

namespace arc_networkd {
namespace {

bool WouldBlock() {
  return errno == EAGAIN || errno == EWOULDBLOCK;
}
}  // namespace

Socket::Socket(int family, int type) : fd_(socket(family, type, 0)) {
  if (!fd_.is_valid())
    PLOG(ERROR) << "socket failed (" << family << ", " << type << ")";
}

Socket::Socket(base::ScopedFD fd) : fd_(std::move(fd)) {
  if (!fd_.is_valid())
    LOG(ERROR) << "invalid fd";
}

bool Socket::Bind(const struct sockaddr* addr, socklen_t addrlen) {
  if (bind(fd_.get(), addr, addrlen) < 0) {
    PLOG(WARNING) << "bind failed: " << *addr;
    return false;
  }
  return true;
}

bool Socket::Connect(const struct sockaddr* addr, socklen_t addrlen) {
  if (connect(fd_.get(), addr, addrlen) < 0) {
    PLOG(WARNING) << "connect failed: " << *addr;
    return false;
  }
  return true;
}

bool Socket::Listen(int backlog) const {
  if (listen(fd_.get(), backlog) != 0) {
    PLOG(WARNING) << "listen failed";
    return false;
  }
  return true;
}

std::unique_ptr<Socket> Socket::Accept(struct sockaddr* addr,
                                       socklen_t* addrlen) const {
  base::ScopedFD fd(accept(fd_.get(), addr, addrlen));
  if (!fd.is_valid()) {
    if (!WouldBlock())
      PLOG(WARNING) << "accept failed";
    return nullptr;
  }
  return std::make_unique<Socket>(std::move(fd));
}

ssize_t Socket::SendTo(const void* data,
                       size_t len,
                       const struct sockaddr* addr,
                       socklen_t addrlen) {
  if (!fd_.is_valid()) {
    return -1;
  }
  if (!addr) {
    addrlen = 0;
  } else if (addrlen == 0) {
    addrlen = sizeof(*addr);
  }

  ssize_t bytes = sendto(fd_.get(), data, len, MSG_NOSIGNAL, addr, addrlen);
  if (bytes >= 0)
    return bytes;

  if (WouldBlock())
    return 0;

  PLOG(WARNING) << "sendto failed";
  return bytes;
}

ssize_t Socket::RecvFrom(void* data,
                         size_t len,
                         struct sockaddr* addr,
                         socklen_t addrlen) {
  socklen_t recvlen = addrlen;
  ssize_t bytes = recvfrom(fd_.get(), data, len, 0, addr, &recvlen);
  if (bytes >= 0) {
    if (recvlen != addrlen)
      PLOG(WARNING) << "recvfrom failed: unexpected src addr length "
                    << recvlen;
    return bytes;
  }

  if (WouldBlock())
    return 0;

  PLOG(WARNING) << "recvfrom failed";
  return bytes;
}

std::ostream& operator<<(std::ostream& stream, const Socket& socket) {
  stream << "{fd: " << socket.fd() << "}";
  return stream;
}

}  // namespace arc_networkd
