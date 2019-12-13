// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/adb_proxy.h"

#include <linux/vm_sockets.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sysexits.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/key_value_store.h>

#include "arc/network/manager.h"
#include "arc/network/minijailed_process_runner.h"
#include "arc/network/net_util.h"

namespace arc_networkd {
namespace {
// adb-proxy will connect to adbd on its standard TCP port.
constexpr uint16_t kTcpConnectPort = 5555;
constexpr uint32_t kTcpAddr = Ipv4Addr(100, 115, 92, 2);
constexpr uint32_t kVsockPort = 5555;
constexpr int kMaxConn = 16;
// Reference: "device/google/cheets2/init.usb.rc".
constexpr char kUnixConnectAddr[] = "/run/arc/adb/adb.sock";

const std::set<GuestMessage::GuestType> kArcGuestTypes{
    GuestMessage::ARC, GuestMessage::ARC_LEGACY, GuestMessage::ARC_VM};

// TODO(b/133378083): Remove once ADB over AF_UNIX is stable.
constexpr char kAdbUnixDomainSocketFeatureName[] =
    "ADB over UNIX domain socket";
constexpr int kUnixDomainSocketMinAndroidSdkVersion = 28;  // P
const std::vector<std::string> kUnixDomainSocketSupportedBoards = {"atlas"};

}  // namespace

AdbProxy::AdbProxy(base::ScopedFD control_fd)
    : msg_dispatcher_(std::move(control_fd)),
      arc_type_(GuestMessage::UNKNOWN_GUEST),
      arcvm_vsock_cid_(-1) {
  msg_dispatcher_.RegisterFailureHandler(
      base::Bind(&AdbProxy::OnParentProcessExit, weak_factory_.GetWeakPtr()));

  msg_dispatcher_.RegisterGuestMessageHandler(
      base::Bind(&AdbProxy::OnGuestMessage, weak_factory_.GetWeakPtr()));
}

AdbProxy::~AdbProxy() = default;

int AdbProxy::OnInit() {
  // Prevent the main process from sending us any signals.
  if (setsid() < 0) {
    PLOG(ERROR) << "Failed to created a new session with setsid; exiting";
    return EX_OSERR;
  }

  enable_unix_domain_socket_ = Manager::ShouldEnableFeature(
      kUnixDomainSocketMinAndroidSdkVersion, 0,
      kUnixDomainSocketSupportedBoards, kAdbUnixDomainSocketFeatureName);
  EnterChildProcessJail();
  return Daemon::OnInit();
}

void AdbProxy::Reset() {
  src_watcher_.reset();
  src_.reset();
  fwd_.clear();
  arcvm_vsock_cid_ = -1;
  arc_type_ = GuestMessage::UNKNOWN_GUEST;
}

void AdbProxy::OnParentProcessExit() {
  LOG(ERROR) << "Quitting because the parent process died";
  Reset();
  Quit();
}

void AdbProxy::OnFileCanReadWithoutBlocking() {
  if (auto conn = src_->Accept()) {
    if (auto dst = Connect()) {
      LOG(INFO) << "Connection established: " << *conn << " <-> " << *dst;
      auto fwd = std::make_unique<SocketForwarder>(
          base::StringPrintf("adbp%d-%d", conn->fd(), dst->fd()),
          std::move(conn), std::move(dst));
      fwd->Start();
      fwd_.emplace_back(std::move(fwd));
    }
  }

  // Cleanup any defunct forwarders.
  for (auto it = fwd_.begin(); it != fwd_.end();) {
    if (!(*it)->IsRunning() && (*it)->HasBeenStarted())
      it = fwd_.erase(it);
    else
      ++it;
  }
}

std::unique_ptr<Socket> AdbProxy::Connect() const {
  switch (arc_type_) {
    case GuestMessage::ARC: {
      if (enable_unix_domain_socket_) {
        struct sockaddr_un addr_un = {0};
        addr_un.sun_family = AF_UNIX;
        snprintf(addr_un.sun_path, sizeof(addr_un.sun_path), "%s",
                 kUnixConnectAddr);
        auto dst = std::make_unique<Socket>(AF_UNIX, SOCK_STREAM);
        if (dst->Connect((const struct sockaddr*)&addr_un, sizeof(addr_un)))
          return dst;
        LOG(WARNING) << "Failed to connect to UNIX domain socket: "
                     << kUnixConnectAddr;
      }
      // We need to be able to fallback on TCP while doing UNIX domain socket
      // migration to prevent unwanted failures.
      LOG(INFO) << "Fallback to TCP";
      FALLTHROUGH;
    }
    case GuestMessage::ARC_LEGACY: {
      struct sockaddr_in addr_in = {0};
      addr_in.sin_family = AF_INET;
      addr_in.sin_port = htons(kTcpConnectPort);
      addr_in.sin_addr.s_addr = kTcpAddr;
      auto dst = std::make_unique<Socket>(AF_INET, SOCK_STREAM);
      return dst->Connect((const struct sockaddr*)&addr_in, sizeof(addr_in))
                 ? std::move(dst)
                 : nullptr;
    }
    case GuestMessage::ARC_VM: {
      struct sockaddr_vm addr_vm = {0};
      addr_vm.svm_family = AF_VSOCK;
      addr_vm.svm_port = kVsockPort;
      addr_vm.svm_cid = arcvm_vsock_cid_;
      auto dst = std::make_unique<Socket>(AF_VSOCK, SOCK_STREAM);
      return dst->Connect((const struct sockaddr*)&addr_vm, sizeof(addr_vm))
                 ? std::move(dst)
                 : nullptr;
    }
    default:
      LOG(DFATAL) << "Unexpected connect - no ARC guest";
      return nullptr;
  }
}

void AdbProxy::OnGuestMessage(const GuestMessage& msg) {
  if (msg.type() == GuestMessage::UNKNOWN_GUEST) {
    LOG(DFATAL) << "Unexpected message from unknown guest";
    return;
  }

  if (kArcGuestTypes.find(msg.type()) == kArcGuestTypes.end()) {
    return;
  }

  arc_type_ = msg.type();
  arcvm_vsock_cid_ = msg.arcvm_vsock_cid();

  // On ARC up, start accepting connections.
  if (msg.event() == GuestMessage::START) {
    src_ = std::make_unique<Socket>(AF_INET, SOCK_STREAM | SOCK_NONBLOCK);
    // Need to set this to reuse the port on localhost.
    int on = 1;
    if (setsockopt(src_->fd(), SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) <
        0) {
      PLOG(ERROR) << "setsockopt(SO_REUSEADDR) failed";
      return;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kAdbProxyTcpListenPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (!src_->Bind((const struct sockaddr*)&addr, sizeof(addr))) {
      LOG(ERROR) << "Cannot bind source socket";
      return;
    }

    if (!src_->Listen(kMaxConn)) {
      LOG(ERROR) << "Cannot listen on source socket";
      return;
    }

    // Run the accept loop.
    LOG(INFO) << "Accepting connections...";
    src_watcher_ = base::FileDescriptorWatcher::WatchReadable(
        src_->fd(), base::BindRepeating(&AdbProxy::OnFileCanReadWithoutBlocking,
                                        base::Unretained(this)));
    return;
  }

  // On ARC down, cull any open connections and stop listening.
  if (msg.event() == GuestMessage::STOP) {
    Reset();
  }
}

}  // namespace arc_networkd
