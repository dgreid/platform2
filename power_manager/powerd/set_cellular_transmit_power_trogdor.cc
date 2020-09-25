// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Helper program for setting radio transmit power of a cellular modem on
// trogdor.

#include <stdlib.h>

#include <string>

#include <base/at_exit.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

int main(int argc, char* argv[]) {
  // The dynamic power reduction (DPR) pin on a M.2 modem module is an active
  // low signal that controls the reduction of radio transmit power. It's
  // typically mapped to a GPIO on the AP, which can be controlled over sysfs.
  DEFINE_int32(level, 0, "Power level for the modem dynamic power reduction");

  brillo::FlagHelper::Init(argc, argv,
                           "Set cellular transmit power mode on trogdor");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  const std::string command = base::StringPrintf(
      "/usr/bin/qmicli -p -d qrtr://0 --sar-rf-set-state=%d", FLAGS_level);
  LOG(INFO) << "Executing command = " << command;
  int return_value = ::system(command.c_str());
  if (return_value == -1) {
    PLOG(ERROR) << "fork() failed";
  } else if (return_value) {
    return_value = WEXITSTATUS(return_value);
    LOG(ERROR) << "Command failed with exit status " << return_value;
  }
  return return_value;
}
