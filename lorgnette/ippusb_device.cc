// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/ippusb_device.h"

#include <memory>

#include <libusb.h>
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
const char kScannerTypeMFP[] = "multi-function peripheral";  // Matches SANE.
const uint8_t kIppUsbInterfaceProtocol = 0x04;

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

  LOG(INFO) << "Connecting ippusb_manager socket to " << addr.sun_path;
  if (connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr),
              sizeof(addr)) < 0) {
    PLOG(ERROR) << "Unable to connect to " << kIppUsbManagerSocket;
    return base::ScopedFD();
  }

  LOG(INFO) << "Connected to ippusb_manager on fd " << fd.get();
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
  LOG(INFO) << "Waiting for socket " << socket_path;

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

std::string VidPid(const libusb_device_descriptor& descriptor) {
  return base::StringPrintf("%04x:%04x", descriptor.idVendor,
                            descriptor.idProduct);
}

// Loop through all altsettings for all interfaces in |config| and return true
// if any is a printer interface class that implements the IPP-USB protocol.
// Also sets |isPrinter| to true if any interface has the printer class
// regardless of whether it supports IPP-USB.
bool ContainsIppUsbInterface(const libusb_config_descriptor* config,
                             bool* isPrinter) {
  for (uint8_t i = 0; i < config->bNumInterfaces; i++) {
    for (uint8_t j = 0; j < config->interface[i].num_altsetting; j++) {
      const libusb_interface_descriptor* interface =
          &config->interface[i].altsetting[j];

      if (interface->bInterfaceClass != LIBUSB_CLASS_PRINTER) {
        continue;
      }

      *isPrinter = true;
      if (interface->bInterfaceProtocol == kIppUsbInterfaceProtocol) {
        return true;
      }
    }
  }
  return false;
}

// Create a ScannerInfo protobuf describing |device|, which is presumed to be an
// IPP-USB capable printer.  The resulting |device_name| member will claim escl
// support through the ippusb backend, but this function will not check for
// proper support.  The caller must connect to the device and probe it before
// attempting to scan.
base::Optional<ScannerInfo> ScannerInfoForDevice(
    libusb_device* device, const libusb_device_descriptor& descriptor) {
  const std::string vid_pid = VidPid(descriptor);

  libusb_device_handle* h;
  int status = libusb_open(device, &h);
  if (status < 0) {
    LOG(ERROR) << "Failed to open device " << vid_pid << ": "
               << libusb_error_name(status);
    return base::nullopt;
  }
  auto handle = std::unique_ptr<libusb_device_handle, decltype(&libusb_close)>(
      h, libusb_close);

  std::vector<uint8_t> buf(256);
  int bytes = libusb_get_string_descriptor_ascii(
      handle.get(), descriptor.iManufacturer, buf.data(), buf.size());
  if (bytes < 0) {
    LOG(ERROR) << "Failed to read manufacturer from device " << vid_pid << ": "
               << libusb_error_name(bytes);
    return base::nullopt;
  }
  std::string mfgr_name((const char*)buf.data(), bytes);

  bytes = libusb_get_string_descriptor_ascii(handle.get(), descriptor.iProduct,
                                             buf.data(), buf.size());
  if (bytes < 0) {
    LOG(ERROR) << "Failed to read product name from device " << vid_pid << ": "
               << libusb_error_name(bytes);
    return base::nullopt;
  }
  std::string model_name((const char*)buf.data(), bytes);

  std::string printer_name;
  if (base::StartsWith(model_name, mfgr_name,
                       base::CompareCase::INSENSITIVE_ASCII)) {
    printer_name = model_name;
  } else {
    printer_name = mfgr_name + " " + model_name;
  }

  std::string device_name =
      base::StringPrintf("ippusb:escl:%s:%04x_%04x/eSCL/", printer_name.c_str(),
                         descriptor.idVendor, descriptor.idProduct);
  LOG(INFO) << "Adding " << device_name << " to possible IPP-USB scanners.";
  ScannerInfo info;
  info.set_name(device_name);
  info.set_manufacturer(mfgr_name);
  info.set_model(model_name);
  info.set_type(kScannerTypeMFP);  // Printer that can scan == MFP.
  return info;
}

// Check if |device| is a printer that supports IPP-USB and return a ScannerInfo
// proto if it is.
base::Optional<ScannerInfo> CheckUsbDevice(libusb_device* device) {
  libusb_device_descriptor descriptor;
  int status = libusb_get_device_descriptor(device, &descriptor);
  if (status < 0) {
    LOG(WARNING) << "Failed to get device descriptor: "
                 << libusb_error_name(status);
    return base::nullopt;
  }
  const std::string vid_pid = VidPid(descriptor);

  // Printers always have a printer class interface defined.  They don't define
  // a top-level device class.
  if (descriptor.bDeviceClass != LIBUSB_CLASS_PER_INTERFACE) {
    return base::nullopt;
  }

  bool isPrinter = false;
  bool isIppUsb = false;
  for (uint8_t c = 0; c < descriptor.bNumConfigurations; c++) {
    libusb_config_descriptor* config;
    status = libusb_get_config_descriptor(device, c, &config);
    if (status < 0) {
      LOG(ERROR) << "Failed to get config descriptor " << c << " for device "
                 << vid_pid << ": " << libusb_error_name(status);
      continue;
    }

    isIppUsb = ContainsIppUsbInterface(config, &isPrinter);

    libusb_free_config_descriptor(config);
    if (isIppUsb) {
      break;
    }
  }
  if (isPrinter && !isIppUsb) {
    LOG(INFO) << "Device " << vid_pid << " is a printer without IPP-USB";
  }
  if (!isIppUsb) {
    return base::nullopt;
  }

  return ScannerInfoForDevice(device, descriptor);
}

}  // namespace

base::Optional<std::string> BackendForDevice(const std::string& device_name) {
  LOG(INFO) << "Finding real backend for device: " << device_name;
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

std::vector<ScannerInfo> FindIppUsbDevices() {
  libusb_context* ctx;
  int status = libusb_init(&ctx);
  if (status != 0) {
    LOG(ERROR) << "Failed to initialize libusb: " << libusb_error_name(status);
    return {};
  }
  auto context =
      std::unique_ptr<libusb_context, decltype(&libusb_exit)>(ctx, libusb_exit);

  libusb_device** dev_list;
  ssize_t num_devices = libusb_get_device_list(context.get(), &dev_list);
  if (num_devices < 0) {
    LOG(ERROR) << "Failed to enumerate USB devices: "
               << libusb_error_name(num_devices);
    return {};
  }

  std::vector<ScannerInfo> scanners;
  for (ssize_t i = 0; i < num_devices; i++) {
    base::Optional<ScannerInfo> info = CheckUsbDevice(dev_list[i]);
    if (info.has_value()) {
      scanners.push_back(info.value());
    }
  }

  libusb_free_device_list(dev_list, 1);
  return scanners;
}

}  // namespace lorgnette
