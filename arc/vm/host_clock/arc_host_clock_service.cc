// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/vm_sockets.h>  // Needs to come after sys/socket.h

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/time/time.h>
#include <brillo/syslog_logging.h>
#include <chromeos/constants/vm_tools.h>

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader |
                  brillo::kLogToStderrIfTty);

  base::ScopedFD listen_fd(socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0));
  PCHECK(listen_fd.is_valid());

  constexpr struct sockaddr_vm addr = {
      .svm_family = AF_VSOCK,
      .svm_port = vm_tools::kArcHostClockServicePort,
      .svm_cid = VMADDR_CID_ANY,
  };
  PCHECK(bind(listen_fd.get(), reinterpret_cast<const struct sockaddr*>(&addr),
              sizeof(addr)) == 0);
  PCHECK(listen(listen_fd.get(), 1) == 0);

  // Keep accepting incoming connection.
  while (true) {
    struct sockaddr_vm addr = {};
    socklen_t addr_size = sizeof(addr);
    base::ScopedFD fd(HANDLE_EINTR(
        accept4(listen_fd.get(), reinterpret_cast<struct sockaddr*>(&addr),
                &addr_size, SOCK_CLOEXEC)));
    PCHECK(fd.is_valid());

    // Keep receiving clockid and returning the corresponding clock value.
    while (true) {
      clockid_t clockid = 0;
      if (!base::ReadFromFD(fd.get(), reinterpret_cast<char*>(&clockid),
                            sizeof(clockid))) {
        PLOG(ERROR) << "ReadFromFD failed.";
        break;
      }
      struct timespec ts = {};
      if (clock_gettime(clockid, &ts) != 0) {
        PLOG(ERROR) << "clock_gettime failed: clock_id = " << clockid;
        break;
      }
      const int64_t result =
          ts.tv_sec * base::Time::kNanosecondsPerSecond + ts.tv_nsec;
      if (!base::WriteFileDescriptor(fd.get(),
                                     reinterpret_cast<const char*>(&result),
                                     sizeof(result))) {
        PLOG(ERROR) << "WriteFileDescriptor failed.";
        break;
      }
    }
  }
}
