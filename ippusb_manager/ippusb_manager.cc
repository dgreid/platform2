// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/syslog_logging.h>
#include <libminijail.h>
#include <libusb.h>
#include <scoped_minijail.h>

#include "ippusb_manager/socket_manager.h"
#include "ippusb_manager/usb.h"

namespace ippusb_manager {

namespace {

constexpr char kRunDir[] = "/run/ippusb";
constexpr char kManagerSocketPath[] = "/run/ippusb/ippusb_manager.sock";

// Convenience container that holds
// *  the main socket on which ippusb_bridge communicates and
// *  the ippusb_bridge keep-alive socket.
struct IppusbBridgeSocketPaths {
  explicit IppusbBridgeSocketPaths(const UsbPrinterInfo* const printer_info)
      : main_socket(base::StringPrintf("%s/%04x_%04x.sock",
                                       kRunDir,
                                       printer_info->vid(),
                                       printer_info->pid())),
        keepalive_socket(base::StringPrintf("%s/%04x_%04x_keep_alive.sock",
                                            kRunDir,
                                            printer_info->vid(),
                                            printer_info->pid())) {}

  base::FilePath main_socket;
  base::FilePath keepalive_socket;
};

// Get the file descriptor of the socket created by upstart.
base::ScopedFD GetFileDescriptor() {
  const char* e = getenv("UPSTART_FDS");
  if (!e) {
    LOG(ERROR) << "No match for the environment variable \"UPSTART_FDS\"";
    exit(1);
  }

  int fd;
  if (!base::StringToInt(e, &fd)) {
    LOG(ERROR) << "Failed to parse the environment variable \"UPSTART_FDS\"";
    exit(1);
  }

  return base::ScopedFD(fd);
}

// Wait up to a maximum of |timeout| seconds for the |socket_paths| to
// disappear. Will return true if the sockets are closed before the
// timeout period, false otherwise.
bool WaitForSocketsClose(const IppusbBridgeSocketPaths& socket_paths,
                         int timeout) {
  base::ElapsedTimer timer;
  while (base::PathExists(socket_paths.main_socket) ||
         base::PathExists(socket_paths.keepalive_socket)) {
    if (timer.Elapsed().InSeconds() > timeout) {
      return false;
    }
    usleep(100000);
  }
  return true;
}

// Attempts to ping the keep alive socket at the given |keep_alive_path| and
// receive an acknowledgement from ippusb_bridge. Returns true if this was
// successful.
bool CheckKeepAlive(const std::string& keep_alive_path) {
  auto keep_alive_connection =
      ClientSocketManager::Create(keep_alive_path.c_str());

  if (keep_alive_connection == nullptr) {
    LOG(ERROR) << "Failed to open keep alive socket";
    return false;
  }

  LOG(INFO) << "Attempting to connect to to keep alive socket at "
            << keep_alive_path;

  if (!keep_alive_connection->OpenConnection()) {
    LOG(ERROR) << "Failed to open connection to keep alive socket";
    return false;
  }

  // send 'keep-alive' message.
  if (!keep_alive_connection->SendMessage("keep-alive")) {
    DLOG(ERROR) << "Failed to send keep-alive to ippusb_bridge";
    return false;
  }

  // Verify acknowledgement of 'keep-alive' message.
  std::string response;
  if (!keep_alive_connection->GetMessage(&response) || response != "ack") {
    DLOG(ERROR) << "Expected keep-alive ``ack'' from ippusb_bridge but got ``"
                << response << "''";
    return false;
  }

  return true;
}

// Uses minijail to start a new instance of ippusb_bridge using the
// specified |socket_paths| and the printer described by
// |printer_info| for printing.
void SpawnXD(const IppusbBridgeSocketPaths socket_paths,
             std::unique_ptr<UsbPrinterInfo> printer_info) {
  std::vector<std::string> string_args = {
      "/usr/bin/ippusb_bridge",
      base::StringPrintf("--bus-device=%03d:%03d", printer_info->bus(),
                         printer_info->device()),
      "--unix-socket=" + socket_paths.main_socket.value(),
      "--keep-alive=" + socket_paths.keepalive_socket.value(),
  };

  LOG(INFO) << "Keep alive path: " << socket_paths.keepalive_socket;

  // This vector does not modify the underlying strings, it's just used for
  // compatibility with the call to execve() which libminijail makes.
  std::vector<char*> ptr_args;
  for (const std::string& s : string_args)
    ptr_args.push_back(const_cast<char*>(s.c_str()));
  ptr_args.push_back(nullptr);

  ScopedMinijail jail(minijail_new());

  // Set namespaces.
  minijail_namespace_ipc(jail.get());
  minijail_namespace_uts(jail.get());
  minijail_namespace_net(jail.get());
  minijail_namespace_cgroups(jail.get());
  minijail_namespace_pids(jail.get());
  minijail_namespace_vfs(jail.get());

  minijail_parse_seccomp_filters(
      jail.get(), "/usr/share/policy/ippusb-bridge-seccomp.policy");

  // Change the umask to 660 so XD will be able to write to the socket that it
  // creates.
  umask(0117);
  minijail_run(jail.get(), ptr_args[0], ptr_args.data());
}

// Attempts to ensure that an instance of ippusb_bridge, appropriately bound
// to the specified |socket_paths|, is running. Returns whether or not
// that is so.
bool CheckOrSpawnIppusbBridge(const IppusbBridgeSocketPaths& socket_paths,
                              std::unique_ptr<UsbPrinterInfo> printer_info) {
  LOG(INFO) << "Checking to see if ippusb_bridge is already running";

  // Leap before you look: if we can squeak a keep-alive message to
  // an already-running ippusb_bridge instance, we're good.
  if (CheckKeepAlive(socket_paths.keepalive_socket.value())) {
    return true;
  }
  LOG(INFO) << "Couldn't contact ippusb_bridge. Waiting for sockets to be "
               "closed before launching a new process";

  // Wait a maximum of 3 seconds for the ippusb_bridge sockets to be closed
  // before spawning the new process.
  if (!WaitForSocketsClose(socket_paths, /*timeout=*/3)) {
    LOG(ERROR) << "The sockets at " << socket_paths.main_socket << " and "
               << socket_paths.keepalive_socket << " still exist";
    return false;
  }

  LOG(INFO) << "Launching a new instance of ippusb_bridge";
  SpawnXD(socket_paths, std::move(printer_info));
  return true;
}

}  // namespace

int ippusb_manager_main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  // Get the file descriptor of the socket created by upstart and begin
  // listening on the socket for client connections.
  auto ippusb_socket =
      ServerSocketManager::Create(kManagerSocketPath, GetFileDescriptor());
  if (ippusb_socket == nullptr) {
    return 1;
  }

  // Since this program is only started by the upstart-socket-bridge once the
  // socket is ready to be read from, if the connection fails to open then
  // something must have gone wrong.
  if (!ippusb_socket->OpenConnection()) {
    LOG(ERROR) << "Failed to open connection to socket";
    return 1;
  }

  // Attempt to receive the message sent by the client.
  std::string usb_info;
  if (!ippusb_socket->GetMessage(&usb_info)) {
    LOG(ERROR) << "Failed to receive message";
    return 1;
  }

  // Use the message sent by the client to create a UsbPrinterInfo object.
  uint16_t vid;
  uint16_t pid;
  if (!GetUsbInfo(usb_info, &vid, &pid)) {
    LOG(ERROR) << "Failed to parse usb info string: " << usb_info;
    return 1;
  }

  auto printer_info = UsbPrinterInfo::Create(vid, pid);
  LOG(INFO) << "Received usb info: " << static_cast<int>(printer_info->vid())
            << " " << static_cast<int>(printer_info->pid());

  // Attempt to initialize the default libusb context in order to search for the
  // printer defined by |printer_info|.
  if (libusb_init(nullptr)) {
    LOG(ERROR) << "Failed to initialize libusb";
    return 1;
  }

  if (!printer_info->FindDeviceLocation()) {
    LOG(INFO) << "Couldn't find device";
    ippusb_socket->SendMessage("Device not found");
    ippusb_socket->CloseConnection();
    ippusb_socket->CloseSocket();
    return 0;
  }

  LOG(INFO) << "Found device on " << static_cast<int>(printer_info->bus())
            << " " << static_cast<int>(printer_info->device());

  const IppusbBridgeSocketPaths socket_paths =
      IppusbBridgeSocketPaths(printer_info.get());
  if (!CheckOrSpawnIppusbBridge(socket_paths, std::move(printer_info))) {
    return 1;
  }

  // Sends the basename of the ippusb_bridge socket to the listener.
  std::string main_socket_basename =
      socket_paths.main_socket.BaseName().value();
  ippusb_socket->SendMessage(main_socket_basename);
  ippusb_socket->CloseConnection();
  ippusb_socket->CloseSocket();

  return 0;
}

}  // namespace ippusb_manager

int main(int argc, char* argv[]) {
  return ippusb_manager::ippusb_manager_main(argc, argv);
}
