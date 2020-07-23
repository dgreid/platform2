// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/vsock_proxy/vsock_proxy.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <tuple>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/logging.h>

#include "arc/vm/vsock_proxy/file_descriptor_util.h"
#include "arc/vm/vsock_proxy/local_file.h"

namespace arc {
namespace {

std::unique_ptr<LocalFile> CreateFile(
    base::ScopedFD fd,
    arc_proxy::FileDescriptor::Type fd_type,
    base::OnceClosure error_handler,
    scoped_refptr<base::TaskRunner> blocking_task_runner) {
  switch (fd_type) {
    case arc_proxy::FileDescriptor::SOCKET_STREAM:
    case arc_proxy::FileDescriptor::SOCKET_DGRAM:
    case arc_proxy::FileDescriptor::SOCKET_SEQPACKET: {
      // Set non-blocking.
      int flags = fcntl(fd.get(), F_GETFL);
      PCHECK(flags != -1);
      flags = fcntl(fd.get(), F_SETFL, flags | O_NONBLOCK);
      PCHECK(flags != -1);
      return std::make_unique<LocalFile>(std::move(fd), true,
                                         std::move(error_handler), nullptr);
    }
    case arc_proxy::FileDescriptor::FIFO_READ:
    case arc_proxy::FileDescriptor::FIFO_WRITE: {
      // Set non-blocking.
      int flags = fcntl(fd.get(), F_GETFL);
      PCHECK(flags != -1);
      flags = fcntl(fd.get(), F_SETFL, flags | O_NONBLOCK);
      PCHECK(flags != -1);
      return std::make_unique<LocalFile>(std::move(fd), false,
                                         std::move(error_handler), nullptr);
    }
    case arc_proxy::FileDescriptor::REGULAR_FILE:
      return std::make_unique<LocalFile>(
          std::move(fd), false, std::move(error_handler), blocking_task_runner);
    case arc_proxy::FileDescriptor::TRANSPORTABLE:
      return nullptr;
    default:
      LOG(ERROR) << "Unknown FileDescriptor::Type: " << fd_type;
      return nullptr;
  }
}

}  // namespace

VSockProxy::VSockProxy(Delegate* delegate)
    : delegate_(delegate),
      next_handle_(delegate_->GetType() == Type::SERVER ? 1 : -1),
      next_cookie_(delegate_->GetType() == Type::SERVER ? 1 : -1) {
  // Note: this needs to be initialized after weak_factory_, which is
  // declared after message_watcher_ in order to destroy it first.
  message_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      delegate_->GetPollFd(), base::BindRepeating(&VSockProxy::OnVSockReadReady,
                                                  weak_factory_.GetWeakPtr()));

  CHECK(blocking_task_thread_.Start());
}

VSockProxy::~VSockProxy() {
  Stop();
}

int64_t VSockProxy::RegisterFileDescriptor(
    base::ScopedFD fd,
    arc_proxy::FileDescriptor::Type fd_type,
    int64_t handle) {
  if (!fd.is_valid()) {
    LOG(ERROR) << "Registering invalid fd.";
    return 0;
  }

  const int raw_fd = fd.get();
  if (handle == 0) {
    // TODO(hidehiko): Ensure handle is unique in case of overflow.
    if (delegate_->GetType() == Type::SERVER)
      handle = next_handle_++;
    else
      handle = next_handle_--;
  }

  auto file = CreateFile(std::move(fd), fd_type,
                         base::BindOnce(&VSockProxy::HandleLocalFileError,
                                        weak_factory_.GetWeakPtr(), handle),
                         blocking_task_thread_.task_runner());
  std::unique_ptr<base::FileDescriptorWatcher::Controller> controller;
  if (fd_type != arc_proxy::FileDescriptor::REGULAR_FILE &&
      fd_type != arc_proxy::FileDescriptor::TRANSPORTABLE) {
    controller = base::FileDescriptorWatcher::WatchReadable(
        raw_fd, base::BindRepeating(&VSockProxy::OnLocalFileDesciptorReadReady,
                                    weak_factory_.GetWeakPtr(), handle));
  }
  fd_map_.emplace(handle,
                  FileDescriptorInfo{std::move(file), std::move(controller)});
  return handle;
}

void VSockProxy::Connect(const base::FilePath& path, ConnectCallback callback) {
  const int64_t cookie = GenerateCookie();

  arc_proxy::VSockMessage message;
  auto* request = message.mutable_connect_request();
  request->set_cookie(cookie);
  request->set_path(path.value());
  pending_connect_.emplace(cookie, std::move(callback));
  if (!delegate_->SendMessage(message, {}))
    Stop();
}

void VSockProxy::Pread(int64_t handle,
                       uint64_t count,
                       uint64_t offset,
                       PreadCallback callback) {
  const int64_t cookie = GenerateCookie();

  arc_proxy::VSockMessage message;
  auto* request = message.mutable_pread_request();
  request->set_cookie(cookie);
  request->set_handle(handle);
  request->set_count(count);
  request->set_offset(offset);
  pending_pread_.emplace(cookie, std::move(callback));
  if (!delegate_->SendMessage(message, {}))
    Stop();
}

void VSockProxy::Fstat(int64_t handle, FstatCallback callback) {
  const int64_t cookie = GenerateCookie();

  arc_proxy::VSockMessage message;
  auto* request = message.mutable_fstat_request();
  request->set_cookie(cookie);
  request->set_handle(handle);
  pending_fstat_.emplace(cookie, std::move(callback));
  if (!delegate_->SendMessage(message, {}))
    Stop();
}

void VSockProxy::Close(int64_t handle) {
  arc_proxy::VSockMessage message;
  message.mutable_close()->set_handle(handle);
  if (!delegate_->SendMessage(message, {}))
    Stop();
}

void VSockProxy::OnVSockReadReady() {
  arc_proxy::VSockMessage message;
  std::vector<base::ScopedFD> fds;
  if (!delegate_->ReceiveMessage(&message, &fds) ||
      !HandleMessage(&message, &fds))
    Stop();
}

bool VSockProxy::HandleMessage(arc_proxy::VSockMessage* message,
                               std::vector<base::ScopedFD>* received_fds) {
  switch (message->command_case()) {
    case arc_proxy::VSockMessage::kClose:
      return OnClose(message->mutable_close());
    case arc_proxy::VSockMessage::kData:
      return OnData(message->mutable_data(), received_fds);
    case arc_proxy::VSockMessage::kConnectRequest:
      return OnConnectRequest(message->mutable_connect_request());
    case arc_proxy::VSockMessage::kConnectResponse:
      return OnConnectResponse(message->mutable_connect_response());
    case arc_proxy::VSockMessage::kPreadRequest:
      OnPreadRequest(message->mutable_pread_request());
      return true;
    case arc_proxy::VSockMessage::kPreadResponse:
      return OnPreadResponse(message->mutable_pread_response());
    case arc_proxy::VSockMessage::kFstatRequest:
      OnFstatRequest(message->mutable_fstat_request());
      return true;
    case arc_proxy::VSockMessage::kFstatResponse:
      return OnFstatResponse(message->mutable_fstat_response());
    default:
      LOG(ERROR) << "Unknown message type: " << message->command_case();
      return false;
  }
}

void VSockProxy::Stop() {
  if (!message_watcher_)  // Do nothing if already stopped.
    return;

  // Run all pending callbacks.
  for (auto& x : pending_fstat_) {
    FstatCallback& callback = x.second;
    std::move(callback).Run(ECONNREFUSED, 0);
  }
  for (auto& x : pending_pread_) {
    PreadCallback& callback = x.second;
    std::move(callback).Run(ECONNREFUSED, std::string());
  }
  for (auto& x : pending_connect_) {
    ConnectCallback& callback = x.second;
    std::move(callback).Run(ECONNREFUSED, 0);
  }
  // Clear registered file descriptors.
  fd_map_.clear();
  // Stop watching the message stream.
  message_watcher_.reset();

  delegate_->OnStopped();
}

bool VSockProxy::OnClose(arc_proxy::Close* close) {
  auto it = fd_map_.find(close->handle());
  if (it == fd_map_.end()) {
    // The file was already closed.
    return true;
  }
  fd_map_.erase(it);
  return true;
}

bool VSockProxy::OnData(arc_proxy::Data* data,
                        std::vector<base::ScopedFD>* received_fds) {
  auto it = fd_map_.find(data->handle());
  if (it == fd_map_.end()) {
    // The file was already closed.
    return true;
  }

  // First, create file descriptors for the received message.
  size_t received_fd_index = 0;
  std::vector<base::ScopedFD> transferred_fds;
  transferred_fds.reserve(data->transferred_fd().size());
  for (const auto& transferred_fd : data->transferred_fd()) {
    base::ScopedFD local_fd;
    base::ScopedFD remote_fd;
    switch (transferred_fd.type()) {
      case arc_proxy::FileDescriptor::FIFO_READ: {
        auto created = CreatePipe();
        if (!created)
          return false;
        std::tie(remote_fd, local_fd) = std::move(*created);
        break;
      }
      case arc_proxy::FileDescriptor::FIFO_WRITE: {
        auto created = CreatePipe();
        if (!created)
          return false;
        std::tie(local_fd, remote_fd) = std::move(*created);
        break;
      }
      case arc_proxy::FileDescriptor::REGULAR_FILE: {
        remote_fd =
            delegate_->CreateProxiedRegularFile(transferred_fd.handle());
        if (!remote_fd.is_valid())
          return false;
        break;
      }
      case arc_proxy::FileDescriptor::TRANSPORTABLE: {
        if (received_fd_index >= received_fds->size()) {
          LOG(ERROR) << "Type in proto is TRANSPORTABLE but no FD remaining.";
          return false;
        }
        remote_fd = std::move((*received_fds)[received_fd_index]);
        ++received_fd_index;
        break;
      }
      case arc_proxy::FileDescriptor::SOCKET_STREAM: {
        auto created = CreateSocketPair(SOCK_STREAM | SOCK_NONBLOCK);
        if (!created)
          return false;
        std::tie(local_fd, remote_fd) = std::move(*created);
        break;
      }
      case arc_proxy::FileDescriptor::SOCKET_DGRAM: {
        auto created = CreateSocketPair(SOCK_DGRAM | SOCK_NONBLOCK);
        if (!created)
          return false;
        std::tie(local_fd, remote_fd) = std::move(*created);
        break;
      }
      case arc_proxy::FileDescriptor::SOCKET_SEQPACKET: {
        auto created = CreateSocketPair(SOCK_SEQPACKET | SOCK_NONBLOCK);
        if (!created)
          return false;
        std::tie(local_fd, remote_fd) = std::move(*created);
        break;
      }
      default:
        LOG(ERROR) << "Invalid type value: " << transferred_fd.type();
        return false;
    }

    // |local_fd| is set iff the descriptor's read readiness needs to be
    // watched, so register it.
    if (local_fd.is_valid()) {
      RegisterFileDescriptor(std::move(local_fd), transferred_fd.type(),
                             transferred_fd.handle());
    }
    transferred_fds.emplace_back(std::move(remote_fd));
  }

  // All received FDs must be consumed.
  if (received_fd_index != received_fds->size()) {
    LOG(ERROR) << "Received FDs not consumed." << received_fd_index << " "
               << received_fds->size();
    return false;
  }

  if (!it->second.file->Write(std::move(*data->mutable_blob()),
                              std::move(transferred_fds)))
    HandleLocalFileError(data->handle());
  return true;
}

bool VSockProxy::OnConnectRequest(arc_proxy::ConnectRequest* request) {
  arc_proxy::VSockMessage reply;
  auto* response = reply.mutable_connect_response();

  // Currently, this actually uses only on ArcBridgeService's initial
  // connection establishment, and the request comes from the guest to the host
  // including the |path|.
  // TODO(hidehiko): Consider allowlist the path which is allowed to access.
  auto result = ConnectUnixDomainSocket(base::FilePath(request->path()));
  response->set_cookie(request->cookie());
  response->set_error_code(result.first);
  if (result.first == 0) {
    response->set_handle(RegisterFileDescriptor(
        std::move(result.second), arc_proxy::FileDescriptor::SOCKET_STREAM,
        0 /* generate handle */));
  }
  return delegate_->SendMessage(reply, {});
}

bool VSockProxy::OnConnectResponse(arc_proxy::ConnectResponse* response) {
  auto it = pending_connect_.find(response->cookie());
  if (it == pending_connect_.end()) {
    LOG(ERROR) << "Unexpected connect response: cookie=" << response->cookie();
    return false;
  }

  auto callback = std::move(it->second);
  pending_connect_.erase(it);
  std::move(callback).Run(response->error_code(), response->handle());
  return true;
}

void VSockProxy::OnPreadRequest(arc_proxy::PreadRequest* request) {
  auto it = fd_map_.find(request->handle());
  if (it == fd_map_.end()) {
    LOG(ERROR) << "Couldn't find handle: handle=" << request->handle();
    arc_proxy::PreadResponse response;
    response.set_error_code(EBADF);
    SendPreadResponse(request->cookie(), response);
    return;
  }
  it->second.file->Pread(
      request->count(), request->offset(),
      base::BindOnce(&VSockProxy::SendPreadResponse, weak_factory_.GetWeakPtr(),
                     request->cookie()));
}

void VSockProxy::SendPreadResponse(int64_t cookie,
                                   arc_proxy::PreadResponse response) {
  response.set_cookie(cookie);
  arc_proxy::VSockMessage reply;
  *reply.mutable_pread_response() = std::move(response);

  if (!delegate_->SendMessage(reply, {}))
    Stop();
}

bool VSockProxy::OnPreadResponse(arc_proxy::PreadResponse* response) {
  auto it = pending_pread_.find(response->cookie());
  if (it == pending_pread_.end()) {
    LOG(ERROR) << "Unexpected pread response: cookie=" << response->cookie();
    return false;
  }

  auto callback = std::move(it->second);
  pending_pread_.erase(it);
  std::move(callback).Run(response->error_code(),
                          std::move(*response->mutable_blob()));
  return true;
}

void VSockProxy::OnFstatRequest(arc_proxy::FstatRequest* request) {
  auto it = fd_map_.find(request->handle());
  if (it == fd_map_.end()) {
    LOG(ERROR) << "Couldn't find handle: handle=" << request->handle();
    arc_proxy::FstatResponse response;
    response.set_error_code(EBADF);
    SendFstatResponse(request->cookie(), std::move(response));
    return;
  }
  it->second.file->Fstat(base::BindOnce(&VSockProxy::SendFstatResponse,
                                        weak_factory_.GetWeakPtr(),
                                        request->cookie()));
}

void VSockProxy::SendFstatResponse(int64_t cookie,
                                   arc_proxy::FstatResponse response) {
  response.set_cookie(cookie);
  arc_proxy::VSockMessage reply;
  *reply.mutable_fstat_response() = std::move(response);

  if (!delegate_->SendMessage(reply, {}))
    Stop();
}

bool VSockProxy::OnFstatResponse(arc_proxy::FstatResponse* response) {
  auto it = pending_fstat_.find(response->cookie());
  if (it == pending_fstat_.end()) {
    LOG(ERROR) << "Unexpected pread response: cookie=" << response->cookie();
    return false;
  }

  auto callback = std::move(it->second);
  pending_fstat_.erase(it);
  std::move(callback).Run(response->error_code(), response->size());
  return true;
}

void VSockProxy::OnLocalFileDesciptorReadReady(int64_t handle) {
  auto it = fd_map_.find(handle);
  if (it == fd_map_.end()) {
    LOG(ERROR) << "Unknown FD gets read ready: handle=" << handle;
    return;
  }

  auto read_result = it->second.file->Read();
  arc_proxy::VSockMessage message;
  std::vector<base::ScopedFD> fds_to_send;
  if (read_result.error_code != 0) {
    LOG(ERROR) << "Failed to read from file descriptor. handle=" << handle;
    // Notify the other side to close.
    message.mutable_close();
  } else if (read_result.blob.empty() && read_result.fds.empty()) {
    // Read empty message, i.e. reached EOF.
    message.mutable_close();
  } else if (!ConvertDataToVSockMessage(std::move(read_result.blob),
                                        std::move(read_result.fds), &message,
                                        &fds_to_send)) {
    // Failed to convert read result into proto.
    message.Clear();
    message.mutable_close();
  }

  if (message.has_close()) {
    // In case of EOF on the other side of the |fd|, |fd| needs to be closed.
    // Otherwise it will be kept read-ready and this callback will be
    // repeatedly called.
    message.mutable_close()->set_handle(handle);
    // Close the corresponding fd, too.
    fd_map_.erase(it);
  } else {
    DCHECK(message.has_data());
    message.mutable_data()->set_handle(handle);
  }
  if (!delegate_->SendMessage(message, fds_to_send))
    Stop();
}

bool VSockProxy::ConvertDataToVSockMessage(
    std::string blob,
    std::vector<base::ScopedFD> fds,
    arc_proxy::VSockMessage* message,
    std::vector<base::ScopedFD>* fds_to_send) {
  DCHECK(!blob.empty() || !fds.empty());

  // Build returning message.
  auto* data = message->mutable_data();
  *data->mutable_blob() = std::move(blob);
  for (auto& fd : fds) {
    auto* transferred_fd = data->add_transferred_fd();

    struct stat st;
    if (fstat(fd.get(), &st) == -1) {
      PLOG(ERROR) << "Failed to fstat";
      return false;
    }

    if (S_ISFIFO(st.st_mode)) {
      int flags = fcntl(fd.get(), F_GETFL, 0);
      if (flags < 0) {
        PLOG(ERROR) << "Failed to find file status flags";
        return false;
      }
      switch (flags & O_ACCMODE) {
        case O_RDONLY:
          transferred_fd->set_type(arc_proxy::FileDescriptor::FIFO_READ);
          break;
        case O_WRONLY:
          transferred_fd->set_type(arc_proxy::FileDescriptor::FIFO_WRITE);
          break;
        default:
          LOG(ERROR) << "Unsupported access mode: " << (flags & O_ACCMODE);
          return false;
      }
    } else if (S_ISSOCK(st.st_mode)) {
      const int type = GetSocketType(fd.get());
      switch (type) {
        case SOCK_STREAM:
          transferred_fd->set_type(arc_proxy::FileDescriptor::SOCKET_STREAM);
          break;
        case SOCK_DGRAM:
          transferred_fd->set_type(arc_proxy::FileDescriptor::SOCKET_DGRAM);
          break;
        case SOCK_SEQPACKET:
          transferred_fd->set_type(arc_proxy::FileDescriptor::SOCKET_SEQPACKET);
          break;
        default:
          LOG(ERROR) << "Unexpected socket type: " << type;
          return false;
      }
    } else if (S_ISREG(st.st_mode)) {
      transferred_fd->set_type(arc_proxy::FileDescriptor::REGULAR_FILE);
    } else {
      // Just send it over virtio-wl.
      transferred_fd->set_type(arc_proxy::FileDescriptor::TRANSPORTABLE);
      fds_to_send->push_back(std::move(fd));
    }
    if (transferred_fd->type() != arc_proxy::FileDescriptor::TRANSPORTABLE) {
      transferred_fd->set_handle(RegisterFileDescriptor(
          std::move(fd), transferred_fd->type(), 0 /* generate handle */));
    }
  }
  return true;
}

void VSockProxy::HandleLocalFileError(int64_t handle) {
  fd_map_.erase(handle);
  Close(handle);
}

int64_t VSockProxy::GenerateCookie() {
  // TODO(hidehiko): Ensure cookie is unique in case of overflow.
  return delegate_->GetType() == Type::SERVER ? next_cookie_++ : next_cookie_--;
}

}  // namespace arc
