// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/vsock_proxy/socket_stream.h"

#include <errno.h>
#include <unistd.h>

#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/posix/unix_domain_socket.h>

namespace arc {

SocketStream::SocketStream(base::ScopedFD fd,
                           bool can_send_fds,
                           base::OnceClosure error_handler)
    : fd_(std::move(fd)),
      can_send_fds_(can_send_fds),
      error_handler_(std::move(error_handler)) {}

SocketStream::~SocketStream() = default;

StreamBase::ReadResult SocketStream::Read() {
  char buf[4096];
  std::vector<base::ScopedFD> fds;
  ssize_t size =
      can_send_fds_
          ? base::UnixDomainSocket::RecvMsg(fd_.get(), buf, sizeof(buf), &fds)
          : HANDLE_EINTR(read(fd_.get(), buf, sizeof(buf)));
  if (size == -1) {
    int error_code = errno;
    PLOG(ERROR) << "Failed to read";
    return {error_code, std::string(), {}};
  }
  return {0 /* succeed */, std::string(buf, size), std::move(fds)};
}

bool SocketStream::Write(std::string blob, std::vector<base::ScopedFD> fds) {
  pending_write_.emplace_back(Data{std::move(blob), std::move(fds)});
  if (!writable_watcher_)  // TrySendMsg will be called later if watching.
    TrySendMsg();
  return true;
}

bool SocketStream::Pread(uint64_t count,
                         uint64_t offset,
                         arc_proxy::PreadResponse* response) {
  LOG(ERROR) << "Pread for socket file descriptor is unsupported.";
  return false;
}

bool SocketStream::Fstat(arc_proxy::FstatResponse* response) {
  LOG(ERROR) << "Fstat for socket file descriptor is unsupported.";
  return false;
}

void SocketStream::TrySendMsg() {
  DCHECK(!pending_write_.empty());
  for (; !pending_write_.empty(); pending_write_.pop_front()) {
    const auto& data = pending_write_.front();

    bool result = false;
    if (data.fds.empty()) {
      result = base::WriteFileDescriptor(fd_.get(), data.blob.data(),
                                         data.blob.size());
    } else {
      std::vector<int> raw_fds;
      raw_fds.reserve(data.fds.size());
      for (const auto& fd : data.fds)
        raw_fds.push_back(fd.get());

      result = base::UnixDomainSocket::SendMsg(fd_.get(), data.blob.data(),
                                               data.blob.size(), raw_fds);
    }
    if (!result) {
      if (errno == EAGAIN) {
        // Will retry later.
        if (!writable_watcher_) {
          writable_watcher_ = base::FileDescriptorWatcher::WatchWritable(
              fd_.get(), base::BindRepeating(&SocketStream::TrySendMsg,
                                             weak_factory_.GetWeakPtr()));
        }
        return;
      }
      PLOG(ERROR) << "Failed to write";
      writable_watcher_.reset();
      std::move(error_handler_).Run();  // May result in deleting this object.
      return;
    }
  }
  // No pending data left. Stop watching.
  writable_watcher_.reset();
}

}  // namespace arc
