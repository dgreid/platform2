// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_CROS_EC_IOCTL_H_
#define POWER_MANAGER_POWERD_SYSTEM_CROS_EC_IOCTL_H_

#include <fcntl.h>
#include <sys/ioctl.h>

#include <base/logging.h>
#include <base/macros.h>
#include <base/strings/stringprintf.h>
#include <chromeos/ec/cros_ec_dev.h>
#include <chromeos/ec/ec_commands.h>

namespace power_manager {
namespace system {
namespace cros_ec_ioctl {

// Character device exposing the EC command interface.
constexpr char kCrosEcDevNodePath[] = "/dev/cros_ec";

// Empty request or response for the IoctlCommand template below.
struct EmptyParam {};
// Empty struct is one byte in C++, get the size we want instead.
template <typename T>
constexpr size_t realsizeof() {
  return std::is_empty<T>::value ? 0 : sizeof(T);
}

// Helper to build and send the command structures for cros_ec.
template <typename Request, typename Response>
class IoctlCommand {
 public:
  explicit IoctlCommand(uint32_t cmd, uint32_t ver = 0, const Request& req = {})
      : data_({
            {ver, cmd, realsizeof<Request>(), realsizeof<Response>(), 0xff},
            {req},
        }) {}

  virtual ~IoctlCommand() = default;

  void SetReq(const Request& req) { data_.req = req; }

  // Runs an EC command.
  // @param ec_fd file descriptor for the EC device.
  // @return true if command runs successfully and response size is same as
  // expected, false otherwise.
  bool Run(int ec_fd) {
    data_.cmd.result = 0xff;
    int ret = ioctl(ec_fd, CROS_EC_DEV_IOCXCMD_V2, &data_);
    if (ret >= 0) {
      VLOG(1) << base::StringPrintf("CROS EC ioctl command 0x%x succeeded",
                                    data_.cmd.command);
      return static_cast<uint32_t>(ret) == data_.cmd.insize;
    }
    PLOG(ERROR) << base::StringPrintf("CROS EC ioctl command 0x%x failed",
                                      data_.cmd.command);
    return false;
  }

  Response* Resp() { return &data_.resp; }
  Request* Req() { return &data_.req; }
  uint16_t Result() { return data_.cmd.result; }

  struct Data {
    struct cros_ec_command_v2 cmd;
    union {
      Request req;
      Response resp;
    };
  };

 private:
  virtual int ioctl(int fd, uint32_t request, Data* data) {
    return ::ioctl(fd, request, data);
  }

  Data data_;

  DISALLOW_COPY_AND_ASSIGN(IoctlCommand);
};

}  // namespace cros_ec_ioctl
}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_CROS_EC_IOCTL_H_
