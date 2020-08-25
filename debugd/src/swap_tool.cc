// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/swap_tool.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>

#include "debugd/src/error_utils.h"
#include "debugd/src/process_with_output.h"

namespace debugd {

namespace {

// This script holds the bulk of the real logic.
const char kSwapHelperScript[] = "/usr/share/cros/init/swap.sh";
// The path of the kstaled ratio file.
const char kKstaledRatioPath[] = "/sys/kernel/mm/kstaled/ratio";
const char kSwapToolErrorString[] = "org.chromium.debugd.error.Swap";

std::string RunSwapHelper(const ProcessWithOutput::ArgList& arguments,
                          int* result) {
  std::string stdout, stderr;
  *result = ProcessWithOutput::RunProcessFromHelper(
      kSwapHelperScript, arguments, nullptr, &stdout, &stderr);
  return *result ? stderr : stdout;
}

}  // namespace

std::string SwapTool::SwapEnable(int32_t size, bool change_now) const {
  int result;
  std::string output, buf;

  buf = base::StringPrintf("%d", size);
  output = RunSwapHelper({"enable", buf}, &result);
  if (result != EXIT_SUCCESS)
    return output;

  if (change_now)
    output = SwapStartStop(true);

  return output;
}

std::string SwapTool::SwapDisable(bool change_now) const {
  int result;
  std::string output;

  output = RunSwapHelper({"disable"}, &result);
  if (result != EXIT_SUCCESS)
    return output;

  if (change_now)
    output = SwapStartStop(false);

  return output;
}

std::string SwapTool::SwapStartStop(bool on) const {
  int result;
  std::string output;

  // We always turn off swap because the config might have changed.
  // Also because the code doesn't like to turn on twice.
  output = RunSwapHelper({"stop"}, &result);
  if (result != EXIT_SUCCESS)
    return output;

  if (on)
    output = RunSwapHelper({"start"}, &result);

  return output;
}

std::string SwapTool::SwapStatus() const {
  int result;
  return RunSwapHelper({"status"}, &result);
}

std::string SwapTool::SwapSetParameter(const std::string& parameter_name,
                                       int32_t parameter_value) const {
  int result;
  std::string buf;

  buf = base::StringPrintf("%d", parameter_value);
  return RunSwapHelper({"set_parameter", parameter_name, buf}, &result);
}

bool SwapTool::KstaledSetRatio(brillo::ErrorPtr* error,
                               uint8_t kstaled_ratio) const {
  std::string buf = std::to_string(kstaled_ratio);

  errno = 0;
  size_t res = base::WriteFile(base::FilePath(kKstaledRatioPath), buf.c_str(),
                               buf.size());
  if (res != buf.size()) {
    DEBUGD_ADD_ERROR(error, kSwapToolErrorString, strerror(errno));
    return false;
  }

  return true;
}

}  // namespace debugd
