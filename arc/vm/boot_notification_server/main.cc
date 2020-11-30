// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include <linux/vm_sockets.h>

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/optional.h>
#include <brillo/syslog_logging.h>

#include "arc/vm/boot_notification_server/util.h"

// Port that the server listens on
constexpr unsigned int kVsockPort = 5500;
// Location of host-side UDS
constexpr char kHostSocketPath[] =
    "/run/arcvm_boot_notification_server/host.socket";
// Command that signals to client that /data is ready
constexpr char kDataReadyCommand[] = "DATA_READY";

int main(int argc, const char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::OpenLog(base::CommandLine::ForCurrentProcess()
                      ->GetProgram()
                      .BaseName()
                      .value()
                      .c_str(),
                  true /* log_pid */);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader |
                  brillo::kLogToStderrIfTty);

  // Listen for connection from ARCVM.
  sockaddr_vm vm_addr{.svm_family = AF_VSOCK,
                      .svm_port = kVsockPort,
                      .svm_cid = VMADDR_CID_HOST};
  base::ScopedFD vm_fd = StartListening(reinterpret_cast<sockaddr*>(&vm_addr));

  if (!vm_fd.is_valid())
    return -1;

  // Delete host socket path if it exists.
  if (!base::DeleteFile(base::FilePath(kHostSocketPath))) {
    LOG(FATAL) << "Unable to delete pre-existing socket at " << kHostSocketPath;
  }

  // Listen for connection from host/Chrome. Chrome expects that by the time it
  // connects to this server, we are already listening for connections from
  // ARCVM as well. Thus, we must listen on the VSOCK before listening on the
  // Unix socket.
  sockaddr_un host_addr{.sun_family = AF_UNIX};
  memcpy(host_addr.sun_path, kHostSocketPath, sizeof(kHostSocketPath));
  base::ScopedFD host_fd =
      StartListening(reinterpret_cast<sockaddr*>(&host_addr));
  if (!host_fd.is_valid())
    return -1;

  // Allow access to socket.
  if (!base::SetPosixFilePermissions(base::FilePath(kHostSocketPath), 0733))
    LOG(FATAL) << "Unable to chmod 0733 " << kHostSocketPath;

  // Chrome will connect first to check that the server is listening, without
  // sending anything.
  {
    base::ScopedFD conn = WaitForClientConnect(host_fd.get());
    if (!conn.is_valid())
      LOG(FATAL) << "Unable to accept connection from host";
  }

  // Receive props from Chrome.
  base::ScopedFD host_client = WaitForClientConnect(host_fd.get());
  if (!host_client.is_valid())
    LOG(FATAL) << "Unable to accept connection from host";

  base::Optional<std::string> props = ReadFD(host_client.get());
  if (!props)
    LOG(FATAL) << "Did not receive props from host";

  LOG(INFO) << "Received " << *props << " from host.";

  // Accept connection from ARCVM, then send DATA_READY followed by props.
  base::ScopedFD vm_client = WaitForClientConnect(vm_fd.get());
  if (!vm_client.is_valid())
    return -1;

  LOG(INFO) << "Sending " << kDataReadyCommand << " to VM client.";
  if (!base::WriteFileDescriptor(vm_client.get(), kDataReadyCommand,
                                 strlen(kDataReadyCommand))) {
    LOG(FATAL) << "Unable to send " << kDataReadyCommand << " to client.";
  }
  if (!base::WriteFileDescriptor(vm_client.get(), props->c_str(),
                                 props->size())) {
    LOG(FATAL) << "Unable to send props to client";
  }

  return 0;
}
