// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <linux/vm_sockets.h>

#include <memory>

#include <base/at_exit.h>
#include <base/files/scoped_file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/memory/ref_counted.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/file_utils.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <grpcpp/grpcpp.h>
#include <vm_protos/proto_bindings/vm_host.grpc.pb.h>

#include "vm_tools/syslog/forwarder.h"

namespace {
constexpr unsigned int kPort = 9999;
// Default syslogd path. When |FLAGS_log_destination| is |kDevLog| we forward
// logs using a unix domain socket.
constexpr char kDevLog[] = "/dev/log";
// Cryptohome token to be replaced in |FLAGS_log_destination|.
constexpr char kCryptohome[] = "cryptohome/";
// Cryptohome root base path.
constexpr char kCryptohomeRoot[] = "/home/root";
}  // namespace

std::string GetPrimaryUserHash() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));

  if (!bus->Connect()) {
    PLOG(ERROR) << "Failed to connect to system D-Bus";
  }

  auto session_manager_proxy = bus->GetObjectProxy(
      login_manager::kSessionManagerServiceName,
      dbus::ObjectPath(login_manager::kSessionManagerServicePath));

  dbus::MethodCall method_call(
      login_manager::kSessionManagerInterface,
      login_manager::kSessionManagerRetrievePrimarySession);
  std::unique_ptr<dbus::Response> response =
      session_manager_proxy->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);

  if (!response.get()) {
    PLOG(ERROR) << "Cannot retrieve username for primary session.";
    return "";
  }
  dbus::MessageReader response_reader(response.get());
  std::string username;
  if (!response_reader.PopString(&username)) {
    PLOG(ERROR) << "Primary session username bad format.";
    return "";
  }
  std::string sanitized_username;
  if (!response_reader.PopString(&sanitized_username)) {
    PLOG(ERROR) << "Primary session sanitized username bad format.";
    return "";
  }
  if (sanitized_username.empty()) {
    PLOG(ERROR) << "Primary session does not exist.";
    return "";
  }
  return sanitized_username;
}

base::FilePath ReplaceCryptohome(base::StringPiece in_path) {
  if (in_path.starts_with(kCryptohome)) {
    std::string user_hash = GetPrimaryUserHash();
    if (!user_hash.empty()) {
      in_path.remove_prefix(base::StringPiece(kCryptohome).length());
      base::FilePath path =
          base::FilePath(kCryptohomeRoot).Append(user_hash).Append(in_path);
      // Ensure the parent dir exists.
      base::FilePath parent_dir = path.DirName();
      if (!base::DirectoryExists(parent_dir)) {
        base::File::Error dir_error;
        if (!base::CreateDirectoryAndGetError(parent_dir, &dir_error)) {
          LOG(ERROR) << "Failed to create directory in /home/root: "
                     << base::File::ErrorToString(dir_error);
          return base::FilePath(in_path);
        }
      }
      return path;
    }
  }
  return base::FilePath(in_path);
}

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  DEFINE_string(
      log_destination, kDevLog,
      "Path to unix domain datagram socket to which logs will be forwarded");
  brillo::FlagHelper::Init(argc, argv, "VM log forwarding tool");

  bool is_socket_dest = true;
  base::ScopedFD dest;
  if (FLAGS_log_destination == kDevLog) {
    dest.reset(socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    if (!dest.is_valid()) {
      PLOG(ERROR) << "Failed to create unix domain datagram socket";
      return EXIT_FAILURE;
    }

    struct sockaddr_un un = {
        .sun_family = AF_UNIX,
    };
    if (FLAGS_log_destination.size() >= sizeof(un.sun_path)) {
      LOG(ERROR) << "Requested log destination path (" << FLAGS_log_destination
                 << ") is too long.  Maximum path length: "
                 << sizeof(un.sun_path) << " characters";
      return EXIT_FAILURE;
    }

    // sun_path is zero-initialized above so we just need to copy the path.
    memcpy(un.sun_path, FLAGS_log_destination.c_str(),
           FLAGS_log_destination.size());

    if (connect(dest.get(), reinterpret_cast<struct sockaddr*>(&un),
                sizeof(un)) != 0) {
      PLOG(ERROR) << "Failed to connect to " << FLAGS_log_destination;
      return EXIT_FAILURE;
    }
  } else {
    is_socket_dest = false;
    base::FilePath dest_path = ReplaceCryptohome(FLAGS_log_destination);
    dest.reset(open(dest_path.value().c_str(),
                    O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC, 0640));
    if (!dest.is_valid()) {
      PLOG(ERROR) << "Failed to open log file";
      return EXIT_FAILURE;
    }
    LOG(INFO) << "Vm log forwarder writing to " << dest_path;
  }

  vm_tools::syslog::Forwarder forwarder(std::move(dest), is_socket_dest);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(
      base::StringPrintf("vsock:%u:%u", VMADDR_CID_ANY, kPort),
      grpc::InsecureServerCredentials());
  builder.RegisterService(&forwarder);

  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  CHECK(server);

  LOG(INFO) << "VM log forwarder listening on port " << kPort;

  server->Wait();

  return EXIT_SUCCESS;
}
