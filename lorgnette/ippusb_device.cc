// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/ippusb_device.h"

#include <vector>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <base/timer/elapsed_timer.h>
#include <re2/re2.h>

namespace lorgnette {

namespace {

const char kIppUsbSocketDir[] = "/run/ippusb";
const char kIppUsbManagerSocket[] = "ippusb_manager.sock";
const base::TimeDelta kSocketCreationTimeout = base::TimeDelta::FromSeconds(3);

// Get a file descriptor connected to the ippusb_manager socket.  Upstart will
// auto-start ippusb_manager if needed, so this should be ready to send messages
// as soon as this function is done.  Returns an invalid fd if the connection
// fails.
base::ScopedFD ConnectIppusbManager() {
  int raw_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (raw_fd < 0) {
    PLOG(ERROR) << "Unable to create AF_UNIX socket";
    return base::ScopedFD();
  }
  base::ScopedFD fd(raw_fd);

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s", kIppUsbSocketDir,
           kIppUsbManagerSocket);

  if (connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr),
              sizeof(addr)) < 0) {
    PLOG(ERROR) << "Unable to connect to " << kIppUsbManagerSocket;
    return base::ScopedFD();
  }

  return fd;
}

// Send a message through |fd| to ippusb_manager requesting the socket name for
// the |vid|:|pid| device.  ippusb_manager will check and start ippusb_bridge as
// needed.
// The expected message format is 1 byte of length followed by <vid>_<pid> and a
// null byte.  |vid| and |pid| should each be 4 hex characters.  The length byte
// is not included in the length, but the trailing null byte is.
bool SendDeviceRequest(int fd, const std::string& vid, const std::string& pid) {
  std::string payload = base::JoinString({vid, pid}, "_");
  size_t payload_len = payload.length() + 1;  // +1 to include NULL byte.
  if (payload_len > UINT8_MAX) {
    LOG(ERROR) << "Message '" << payload << "' is too long for ippusb_manager";
    return false;
  }

  auto msg = std::vector<char>(payload_len + 1);  // +1 for size byte.
  msg[0] = payload_len;
  memcpy(msg.data() + 1, payload.c_str(), payload_len);

  size_t sent = 0;
  size_t remaining = msg.size();
  while (remaining > 0) {
    ssize_t bytes =
        HANDLE_EINTR(send(fd, msg.data() + sent, remaining, MSG_NOSIGNAL));
    if (bytes < 0) {
      PLOG(ERROR) << "Failed to send message body";
      return false;
    }
    sent += bytes;
    remaining -= bytes;
  }

  return true;
}

// Read an ippusb_manager response from |fd|.  Only the socket name is
// returned; the full path can be constructed by looking in kIppUsbSocketDir.
// Returns an empty string if the response is not valid.  The socket may not yet
// exist when the response arrives.
// The expected message format is one byte of length followed by the name of an
// AF_UNIX socket that can be used to connect to the previously requested
// device.  The length byte is not included in the length.
std::string ReadDeviceResponse(int fd) {
  // Set a timeout so we don't wait indefinitely if ippusb_manager has crashed
  // before writing its response.
  struct timeval timeout;
  memset(&timeout, 0, sizeof(timeout));
  timeout.tv_sec = kSocketCreationTimeout.InSeconds();
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    PLOG(ERROR) << "Failed to set socket timeout";
    return "";
  }

  uint8_t msg_len;
  if (HANDLE_EINTR(recv(fd, &msg_len, sizeof(uint8_t), 0)) != sizeof(msg_len)) {
    PLOG(ERROR) << "Failed to read response length";
    return "";
  }

  // It's not clear if ippusb_manager will always include a trailing null in the
  // response, so append an extra one just in case.
  auto response = std::vector<char>(msg_len + 1);
  if (HANDLE_EINTR(recv(fd, response.data(), msg_len, MSG_WAITALL)) !=
      msg_len) {
    PLOG(ERROR) << "Failed to read response body";
    return "";
  }
  response[msg_len] = '\0';

  // ippusb_manager will return a socket name of "Device not found" if it can't
  // find the requested USB device.  Validate the path to make sure we don't try
  // to connect to a string like this.
  if (!RE2::FullMatch(response.data(), "^[0-9A-Fa-f_-]+\\.sock$")) {
    LOG(ERROR) << "Socket response (" << response.data() << ") is not valid.";
    return "";
  }

  return std::string(response.data());
}

// ippusb_manager returns a socket path without waiting for ippusb_bridge to
// finish starting.  This function waits for the expected socket file to appear
// in the filesystem.  It returns true if that happens, or false if the socket
// doesn't appear within |timeout|.
bool WaitForSocket(const std::string& sock_name, base::TimeDelta timeout) {
  base::FilePath socket_path(kIppUsbSocketDir);
  socket_path = socket_path.Append(sock_name);

  base::ElapsedTimer timer;
  while (!base::PathExists(socket_path)) {
    if (timer.Elapsed() > timeout) {
      LOG(ERROR) << "Timed out waiting for socket " << socket_path;
      return false;
    }

    base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(10));
  }

  return true;
}

}  // namespace

base::Optional<std::string> BackendForDevice(const std::string& device_name) {
  std::string protocol, name, vid, pid, path;
  if (!RE2::FullMatch(
          device_name,
          "ippusb:([^:]+):([^:]+):([0-9A-Fa-f]{4})_([0-9A-Fa-f]{4})(/.*)",
          &protocol, &name, &vid, &pid, &path)) {
    return base::nullopt;
  }

  base::ScopedFD fd = ConnectIppusbManager();
  if (!fd.is_valid()) {
    return base::nullopt;
  }
  if (!SendDeviceRequest(fd.get(), vid, pid)) {
    return base::nullopt;
  }
  std::string socket = ReadDeviceResponse(fd.get());
  if (socket.empty()) {
    return base::nullopt;
  }
  if (!WaitForSocket(socket, kSocketCreationTimeout)) {
    return base::nullopt;
  }

  std::string real_device =
      base::StringPrintf("airscan:%s:%s:unix://%s%s", protocol.c_str(),
                         name.c_str(), socket.c_str(), path.c_str());
  return real_device;
}

}  // namespace lorgnette
