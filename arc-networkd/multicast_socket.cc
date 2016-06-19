// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc-networkd/multicast_socket.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <base/logging.h>

namespace arc_networkd {

MulticastSocket::~MulticastSocket() {
  if (fd_.is_valid())
    watcher_.StopWatchingFileDescriptor();
}

bool MulticastSocket::Bind(const std::string& ifname,
                           const struct in_addr& mcast_addr,
                           unsigned short port,
                           MessageLoopForIO::Watcher* parent) {
  CHECK(!fd_.is_valid());

  base::ScopedFD fd(socket(AF_INET, SOCK_DGRAM, 0));
  if (!fd.is_valid()) {
    LOG(ERROR) << "socket() failed";
    return false;
  }

  // The socket needs to be bound to INADDR_ANY rather than a specific
  // interface, or it will not receive multicast traffic.  Therefore
  // we use SO_BINDTODEVICE to force TX from this interface, and
  // specify the interface address in IP_ADD_MEMBERSHIP to control RX.
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ);
  if (ioctl(fd.get(), SIOCGIFADDR, &ifr) < 0) {
    LOG(ERROR) << "SIOCGIFADDR failed";
    return false;
  }

  struct sockaddr_in* if_addr =
      reinterpret_cast<struct sockaddr_in*>(&ifr.ifr_addr);

  if (setsockopt(fd.get(), SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr))) {
    LOG(ERROR) << "setsockopt(SOL_SOCKET) failed";
    return false;
  }

  struct ip_mreq mreq;
  memset(&mreq, 0, sizeof(mreq));
  mreq.imr_interface = if_addr->sin_addr;
  mreq.imr_multiaddr = mcast_addr;

  struct sockaddr_in bind_addr;
  memset(&bind_addr, 0, sizeof(bind_addr));

  if (mcast_addr.s_addr == INADDR_BROADCAST) {
    // FIXME: RX needs to be limited to the given interface.
    int on = 1;
    if (setsockopt(fd.get(), SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) < 0) {
      LOG(ERROR) << "setsockopt(SO_BROADCAST) failed";
      return false;
    }
    bind_addr.sin_addr.s_addr = INADDR_BROADCAST;
  } else {
    if (setsockopt(fd.get(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
                   sizeof(mreq)) < 0) {
      LOG(ERROR) << "can't add multicast membership";
      return false;
    }
  }

  int off = 0;
  if (setsockopt(fd.get(), IPPROTO_IP, IP_MULTICAST_LOOP, &off, sizeof(off))) {
    LOG(ERROR) << "setsockopt(IP_MULTICAST_LOOP) failed";
    return false;
  }

  int on = 1;
  if (setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    LOG(ERROR) << "setsockopt(SO_REUSEADDR) failed";
    return false;
  }

  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(port);

  if (bind(fd.get(), (const struct sockaddr *)&bind_addr,
           sizeof(bind_addr)) < 0) {
    LOG(ERROR) << "bind(" << port << ") failed";
    return false;
  }

  MessageLoopForIO::current()->WatchFileDescriptor(
      fd.get(), true, MessageLoopForIO::WATCH_READ, &watcher_, parent);

  fd_ = std::move(fd);
  return true;
}

bool MulticastSocket::SendTo(const void* data,
                             size_t len,
                             const struct sockaddr_in& addr) {
  if (sendto(fd_.get(), data, len, 0,
             reinterpret_cast<const struct sockaddr*>(&addr),
             sizeof(struct sockaddr_in)) < 0) {
    LOG(WARNING) << "sendto failed";
    return false;
  } else {
    last_used_ = time(NULL);
    return true;
  }
}

// static
ssize_t MulticastSocket::RecvFromFd(int fd,
                                    void* data,
                                    size_t len,
                                    struct sockaddr_in* addr) {
  socklen_t addrlen = sizeof(*addr);
  ssize_t bytes = recvfrom(fd, data, len, 0,
                           reinterpret_cast<struct sockaddr*>(addr),
                           &addrlen);
  if (bytes < 0 || addrlen != sizeof(*addr)) {
    LOG(WARNING) << "recvfrom failed";
    return -1;
  }
  return bytes;
}

}  // namespace arc_networkd
