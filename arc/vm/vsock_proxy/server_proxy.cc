// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/vsock_proxy/server_proxy.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Needs to be included after sys/socket.h
#include <linux/un.h>
#include <linux/vm_sockets.h>

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/posix/unix_domain_socket.h>
#include <base/stl_util.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/userdb_utils.h>

#include "arc/vm/vsock_proxy/file_descriptor_util.h"
#include "arc/vm/vsock_proxy/message.pb.h"
#include "arc/vm/vsock_proxy/proxy_file_system.h"
#include "arc/vm/vsock_proxy/vsock_proxy.h"

namespace arc {
namespace {

// Port for VSOCK.
constexpr unsigned int kVSockPort = 9900;

// Crosvm connects to this socket when creating a new virtwl context.
constexpr char kVirtwlSocketPath[] = "/run/arcvm/mojo/mojo-proxy.sock";

base::ScopedFD CreateVSock() {
  LOG(INFO) << "Creating VSOCK...";
  struct sockaddr_vm sa = {};
  sa.svm_family = AF_VSOCK;
  sa.svm_cid = VMADDR_CID_ANY;
  sa.svm_port = kVSockPort;

  base::ScopedFD fd(
      socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0 /* protocol */));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to create VSOCK";
    return {};
  }

  if (bind(fd.get(), reinterpret_cast<const struct sockaddr*>(&sa),
           sizeof(sa)) == -1) {
    PLOG(ERROR) << "Failed to bind a unix domain socket";
    return {};
  }

  if (listen(fd.get(), 5 /* backlog */) == -1) {
    PLOG(ERROR) << "Failed to start listening a socket";
    return {};
  }

  LOG(INFO) << "VSOCK created.";
  return fd;
}

// Sets up a socket to accept virtwl connections.
base::ScopedFD SetupVirtwlSocket() {
  // Delete the socket created by a previous run if any.
  if (!base::DeleteFile(base::FilePath(kVirtwlSocketPath))) {
    PLOG(ERROR) << "DeleteFile() failed " << kVirtwlSocketPath;
    return {};
  }
  // Bind a socket to the path.
  base::ScopedFD sock(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!sock.is_valid()) {
    PLOG(ERROR) << "socket() failed";
    return {};
  }
  struct sockaddr_un unix_addr = {};
  unix_addr.sun_family = AF_UNIX;
  strncpy(unix_addr.sun_path, kVirtwlSocketPath, sizeof(unix_addr.sun_path));
  if (bind(sock.get(), reinterpret_cast<const sockaddr*>(&unix_addr),
           sizeof(unix_addr)) < 0) {
    PLOG(ERROR) << "bind failed " << kVirtwlSocketPath;
    return {};
  }
  // Make it accessible to crosvm.
  uid_t uid = 0;
  gid_t gid = 0;
  if (!brillo::userdb::GetUserInfo("crosvm", &uid, &gid)) {
    LOG(ERROR) << "Failed to get crosvm user info.";
    return {};
  }
  if (lchown(kVirtwlSocketPath, uid, gid) != 0) {
    PLOG(ERROR) << "lchown failed";
    return {};
  }
  // Start listening on the socket.
  if (listen(sock.get(), SOMAXCONN) < 0) {
    PLOG(ERROR) << "listen failed";
    return {};
  }
  return sock;
}

}  // namespace

ServerProxy::ServerProxy(
    scoped_refptr<base::TaskRunner> proxy_file_system_task_runner,
    const base::FilePath& proxy_file_system_mount_path,
    base::OnceClosure quit_closure)
    : proxy_file_system_task_runner_(proxy_file_system_task_runner),
      proxy_file_system_(this,
                         base::ThreadTaskRunnerHandle::Get(),
                         proxy_file_system_mount_path),
      quit_closure_(std::move(quit_closure)) {}

ServerProxy::~ServerProxy() = default;

bool ServerProxy::Initialize() {
  // Initialize ProxyFileSystem.
  base::WaitableEvent file_system_initialized(
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED);
  bool result = false;
  proxy_file_system_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](ProxyFileSystem* proxy_file_system,
             base::WaitableEvent* file_system_initialized, bool* result) {
            *result = proxy_file_system->Init();
            file_system_initialized->Signal();
          },
          &proxy_file_system_, &file_system_initialized, &result));
  file_system_initialized.Wait();
  if (!result) {
    LOG(ERROR) << "Failed to initialize ProxyFileSystem.";
    return false;
  }
  // The connection is established as follows.
  // 1) Chrome creates a socket at /run/chrome/arc_bridge.sock (in host).
  // 2) Start ARCVM, then starts host proxy in host OS.
  // 3) Host proxy prepares VSOCK and listens it.
  // 4) ClientProxy in arcbridgeservice connects to VSOCK, and initializes
  //    VSockProxy, then creates /var/run/chrome/arc_bridge.sock in guest.
  // 5) ArcBridgeService in arcbridgeservice connects to the guest
  //    arc_bridge.sock.
  // 6) VSockProxy in client is notified, so send a message to request connect
  //    to the /run/chrome/arc_bridge.sock to host via VSOCK.
  // 7) Host proxy connects as client requested, then returns its corresponding
  //    handle to client.
  // 8) Finally, ClientProxy accept(2)s the /var/run/chrome/arc_bridge.sock,
  //    and register the file descriptor with the returned handle.
  //    Now ArcBridge connection between ARCVM and host is established.
  auto vsock = CreateVSock();
  if (!vsock.is_valid())
    return false;

  // Initialize virtwl context.
  virtwl_socket_ = SetupVirtwlSocket();
  if (!virtwl_socket_.is_valid()) {
    LOG(ERROR) << "Failed to set up virtwl socket.";
    return false;
  }

  // Wait for vsock connection and virtwl connection from the guest.
  // If virtwl connection comes before vsock, that means the guest is running
  // new code which doesn't use vsock.
  LOG(INFO) << "Waiting for a guest connection...";
  struct pollfd fds[] = {
      {.fd = vsock.get(), .events = POLLIN},
      {.fd = virtwl_socket_.get(), .events = POLLIN},
  };
  if (HANDLE_EINTR(poll(fds, base::size(fds), -1)) == -1) {
    PLOG(ERROR) << "poll() failed";
    return false;
  }
  guest_is_using_vsock_ = fds[0].revents & POLLIN;
  LOG(INFO) << "Guest is using vsock: "
            << (guest_is_using_vsock_ ? "true" : "false");

  LOG(INFO) << "Accepting guest virtwl connection...";
  virtwl_context_ = AcceptSocket(virtwl_socket_.get());
  if (!virtwl_context_.is_valid()) {
    LOG(ERROR) << "Failed to accept virtwl connection";
    return false;
  }

  if (guest_is_using_vsock_) {
    // The guest code is old and still using vsock.
    // Use vsock to receive messages from guest.
    // TODO(hashimoto): Remove vsock support.
    LOG(INFO) << "Accepting guest vsock connection...";
    auto accepted = AcceptSocket(vsock.get());
    if (!accepted.is_valid())
      return false;
    message_stream_ = std::make_unique<MessageStream>(std::move(accepted));
  } else {
    // Use virtwl to receive messages from guest.
    LOG(INFO) << "Using virtwl to receive messages.";
    message_stream_ =
        std::make_unique<MessageStream>(std::move(virtwl_context_));
  }

  vsock.reset();
  LOG(INFO) << "Initial socket connection comes";
  vsock_proxy_ = std::make_unique<VSockProxy>(this);
  LOG(INFO) << "ServerProxy has started to work.";
  return true;
}

base::ScopedFD ServerProxy::CreateProxiedRegularFile(int64_t handle,
                                                     int32_t flags) {
  // Create a file descriptor which is handled by |proxy_file_system_|.
  return proxy_file_system_.RegisterHandle(handle, flags);
}

bool ServerProxy::SendMessage(const arc_proxy::VSockMessage& message,
                              const std::vector<base::ScopedFD>& fds) {
  if (!fds.empty()) {
    LOG(ERROR) << "It's not allowed to send FDs from host to guest.";
    return false;
  }
  return message_stream_->Write(message);
}

bool ServerProxy::ReceiveMessage(arc_proxy::VSockMessage* message,
                                 std::vector<base::ScopedFD>* fds) {
  if (guest_is_using_vsock_) {
    if (!message_stream_->Read(message, nullptr))
      return false;
    if (message->has_data()) {
      for (const auto& fd : message->data().transferred_fd()) {
        // Receive FD via virtwl if type == TRANSPORTABLE.
        if (fd.type() == arc_proxy::FileDescriptor::TRANSPORTABLE) {
          char dummy_data = 0;
          std::vector<base::ScopedFD> transported_fds;
          ssize_t size = Recvmsg(virtwl_context_.get(), &dummy_data,
                                 sizeof(dummy_data), &transported_fds);
          if (size != sizeof(dummy_data)) {
            PLOG(ERROR) << "Failed to receive a message";
            return false;
          }
          if (transported_fds.size() != 1) {
            LOG(ERROR) << "Wrong FD size: " << transported_fds.size();
            return false;
          }
          vsock_proxy_->Close(fd.handle());  // Close the FD owned by guest.
          fds->push_back(std::move(transported_fds[0]));
        }
      }
    }
    return true;
  } else {
    return message_stream_->Read(message, fds);
  }
}

void ServerProxy::OnStopped() {
  std::move(quit_closure_).Run();
}

void ServerProxy::Pread(int64_t handle,
                        uint64_t count,
                        uint64_t offset,
                        PreadCallback callback) {
  vsock_proxy_->Pread(handle, count, offset, std::move(callback));
}

void ServerProxy::Pwrite(int64_t handle,
                         std::string blob,
                         uint64_t offset,
                         PwriteCallback callback) {
  vsock_proxy_->Pwrite(handle, std::move(blob), offset, std::move(callback));
}

void ServerProxy::Close(int64_t handle) {
  vsock_proxy_->Close(handle);
}

void ServerProxy::Fstat(int64_t handle, FstatCallback callback) {
  vsock_proxy_->Fstat(handle, std::move(callback));
}

}  // namespace arc
