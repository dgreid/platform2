// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/socket_forwarder.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/time/time.h>

namespace patchpanel {
namespace {
constexpr int kWaitTimeoutMs = 1000;
// Maximum number of epoll events to process per wait.
constexpr int kMaxEvents = 4;
}  // namespace

SocketForwarder::SocketForwarder(const std::string& name,
                                 std::unique_ptr<Socket> sock0,
                                 std::unique_ptr<Socket> sock1)
    : base::SimpleThread(name),
      sock0_(std::move(sock0)),
      sock1_(std::move(sock1)),
      len0_(0),
      len1_(0),
      poll_(false),
      done_(false) {
  DCHECK(sock0_);
  DCHECK(sock1_);
}

SocketForwarder::~SocketForwarder() {
  // Ensure the polling loop exits.
  poll_ = false;
  Join();
}

bool SocketForwarder::IsRunning() const {
  return !done_;
}

void SocketForwarder::Run() {
  LOG(INFO) << "Starting forwarder: " << *sock0_ << " <-> " << *sock1_;

  // We need these sockets to be non-blocking.
  if (fcntl(sock0_->fd(), F_SETFL,
            fcntl(sock0_->fd(), F_GETFL, 0) | O_NONBLOCK) < 0 ||
      fcntl(sock1_->fd(), F_SETFL,
            fcntl(sock1_->fd(), F_GETFL, 0) | O_NONBLOCK) < 0) {
    PLOG(ERROR) << "fcntl failed";
    return;
  }

  Poll();

  LOG(INFO) << "Forwarder stopped: " << *sock0_ << " <-> " << *sock1_;
  done_ = true;
  sock1_.reset();
  sock0_.reset();
}

void SocketForwarder::Poll() {
  base::ScopedFD cfd(epoll_create1(0));
  if (!cfd.is_valid()) {
    PLOG(ERROR) << "epoll_create1 failed";
    return;
  }
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLRDHUP;
  ev.data.fd = sock0_->fd();
  if (epoll_ctl(cfd.get(), EPOLL_CTL_ADD, sock0_->fd(), &ev) == -1) {
    PLOG(ERROR) << "epoll_ctl failed";
    return;
  }
  ev.data.fd = sock1_->fd();
  if (epoll_ctl(cfd.get(), EPOLL_CTL_ADD, sock1_->fd(), &ev) == -1) {
    PLOG(ERROR) << "epoll_ctl failed";
    return;
  }

  poll_ = true;
  struct epoll_event events[kMaxEvents];
  while (poll_) {
    int n = epoll_wait(cfd.get(), events, kMaxEvents, kWaitTimeoutMs);
    if (n == -1) {
      if (errno == EINTR) {
        LOG(INFO) << "Resume epoll_wait from interruption.";
        continue;
      }
      PLOG(ERROR) << "epoll_wait failed";
      return;
    }
    for (int i = 0; i < n; ++i) {
      if (!poll_ ||
          !ProcessEvents(events[i].events, events[i].data.fd, cfd.get()))
        return;
    }
  }
}

bool SocketForwarder::ProcessEvents(uint32_t events, int efd, int cfd) {
  if (events & EPOLLERR) {
    int so_error;
    socklen_t optlen = sizeof(so_error);
    getsockopt(efd, SOL_SOCKET, SO_ERROR, &so_error, &optlen);
    PLOG(WARNING) << "Socket error: (" << so_error << ") " << *sock0_ << " <-> "
                  << *sock1_;
    return false;
  }
  if (events & (EPOLLHUP | EPOLLRDHUP)) {
    LOG(INFO) << "Peer closed connection: " << *sock0_ << " <-> " << *sock1_;
    return false;
  }

  if (events & EPOLLOUT) {
    Socket* dst;
    char* buf;
    ssize_t* len;
    if (sock0_->fd() == efd) {
      dst = sock0_.get();
      buf = buf1_;
      len = &len1_;
    } else {
      dst = sock1_.get();
      buf = buf0_;
      len = &len0_;
    }

    ssize_t bytes = dst->SendTo(buf, *len);
    if (bytes < 0)
      return false;

    // Still unavailable.
    if (bytes == 0)
      return true;

    // Partial write.
    if (bytes < *len)
      memmove(&buf[0], &buf[bytes], *len - bytes);
    *len -= bytes;

    if (*len == 0) {
      struct epoll_event ev;
      ev.events = EPOLLIN | EPOLLRDHUP;
      ev.data.fd = dst->fd();
      if (epoll_ctl(cfd, EPOLL_CTL_MOD, dst->fd(), &ev) == -1) {
        PLOG(ERROR) << "epoll_ctl failed";
        return false;
      }
    }
  }

  if (events & EPOLLIN) {
    Socket *src, *dst;
    char* buf;
    ssize_t* len;
    if (sock0_->fd() == efd) {
      src = sock0_.get();
      dst = sock1_.get();
      buf = buf0_;
      len = &len0_;
    } else {
      src = sock1_.get();
      dst = sock0_.get();
      buf = buf1_;
      len = &len1_;
    }

    // Skip the read if this buffer is still pending write: requires that
    // epoll_wait is in level-triggered mode.
    if (*len > 0)
      return true;

    *len = src->RecvFrom(buf, kBufSize);
    if (*len < 0)
      return false;

    if (*len == 0)
      return true;

    ssize_t bytes = dst->SendTo(buf, *len);
    if (bytes < 0)
      return false;

    if (bytes > 0) {
      // Partial write.
      if (bytes < *len)
        memmove(&buf[0], &buf[bytes], *len - bytes);
      *len -= bytes;
    }

    if (*len > 0) {
      struct epoll_event ev;
      ev.events = EPOLLOUT | EPOLLRDHUP;
      ev.data.fd = dst->fd();
      if (epoll_ctl(cfd, EPOLL_CTL_MOD, dst->fd(), &ev) == -1) {
        PLOG(ERROR) << "epoll_ctl failed";
        return false;
      }
    }
  }

  return true;
}

}  // namespace patchpanel
