// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/vsock_proxy/server_proxy.h"

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
#include <base/synchronization/waitable_event.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/posix/unix_domain_socket.h>
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
  if (!base::DeleteFile(base::FilePath(kVirtwlSocketPath),
                        false /* recursive */)) {
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

  LOG(INFO) << "Start observing VSOCK";
  auto accepted = AcceptSocket(vsock.get());
  if (!accepted.is_valid())
    return false;

  // Initialize virtwl context.
  virtwl_socket_ = SetupVirtwlSocket();
  if (!virtwl_socket_.is_valid()) {
    LOG(ERROR) << "Failed to set up virtwl socket.";
    return false;
  }
  // Accept virtwl connection asynchronously to avoid blocking the
  // initialization in case the guest proxy is old (i.e. no connection coming).
  // TODO(hashimoto): Do this synchronously for proper error handling after the
  // guest proxy code gets updated.
  virtwl_socket_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      virtwl_socket_.get(),
      base::BindRepeating(&ServerProxy::AcceptVirtwlConnection,
                          base::Unretained(this)));

  vsock.reset();
  LOG(INFO) << "Initial socket connection comes";
  vsock_proxy_ = std::make_unique<VSockProxy>(this, std::move(accepted));
  LOG(INFO) << "ServerProxy has started to work.";
  return true;
}

bool ServerProxy::ConvertFileDescriptorToProto(
    int fd, arc_proxy::FileDescriptor* proto) {
  LOG(ERROR) << "Unsupported FD type.";
  return false;
}

base::ScopedFD ServerProxy::ConvertProtoToFileDescriptor(
    const arc_proxy::FileDescriptor& proto) {
  switch (proto.type()) {
    case arc_proxy::FileDescriptor::REGULAR_FILE: {
      // Create a file descriptor which is handled by |proxy_file_system_|.
      return proxy_file_system_.RegisterHandle(proto.handle());
    }
    case arc_proxy::FileDescriptor::DMABUF: {
      char dummy_data = 0;
      std::vector<base::ScopedFD> fds;
      ssize_t size =
          Recvmsg(virtwl_context_.get(), &dummy_data, sizeof(dummy_data), &fds);
      if (size != sizeof(dummy_data)) {
        PLOG(ERROR) << "Failed to receive a message";
        return {};
      }
      if (fds.size() != 1) {
        LOG(ERROR) << "Wrong FD size: " << fds.size();
        return {};
      }
      vsock_proxy_->Close(proto.handle());  // Close the FD owned by guest.
      return std::move(fds[0]);
    }
    default:
      LOG(ERROR) << "Unsupported FD type: " << proto.type();
      return {};
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

void ServerProxy::Close(int64_t handle) {
  vsock_proxy_->Close(handle);
}

void ServerProxy::Fstat(int64_t handle, FstatCallback callback) {
  vsock_proxy_->Fstat(handle, std::move(callback));
}

void ServerProxy::AcceptVirtwlConnection() {
  virtwl_socket_watcher_.reset();
  virtwl_context_ = AcceptSocket(virtwl_socket_.get());
  LOG_IF(ERROR, !virtwl_context_.is_valid())
      << "Failed to accept virtwl connection";
}

}  // namespace arc
