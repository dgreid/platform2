// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This tool is used for getting dmesg information through debugd.

#include "debugd/src/dmesg_tool.h"

#include "debugd/src/error_utils.h"
#include "debugd/src/process_with_output.h"
#include "debugd/src/variant_utils.h"
#include "linux/capability.h"

namespace {

constexpr char kDmesgPath[] = "/bin/dmesg";

}  // namespace

namespace debugd {

bool DmesgTool::CallDmesg(const brillo::VariantDictionary& options,
                          brillo::ErrorPtr* error,
                          std::string* output) {
  ProcessWithOutput process;

  process.SetCapabilities(CAP_TO_MASK(CAP_SYSLOG));
  if (!process.Init()) {
    *output = "<process init failed>";
    return false;
  }

  process.AddArg(kDmesgPath);

  if (!AddIntOption(&process, options, "show-delta", "-d", error) ||
      !AddIntOption(&process, options, "human", "-H", error) ||
      !AddIntOption(&process, options, "kernel", "-k", error) ||
      !AddIntOption(&process, options, "force-prefix", "-p", error) ||
      !AddIntOption(&process, options, "raw", "-r", error) ||
      !AddIntOption(&process, options, "ctime", "-T", error) ||
      !AddIntOption(&process, options, "notime", "-t", error) ||
      !AddIntOption(&process, options, "userspace", "-u", error) ||
      !AddIntOption(&process, options, "decode", "-x", error)) {
    *output = "<invalid option>";
    return false;
  }

  if (process.Run() != 0) {
    *output = "<process exited with nonzero status>";
    return false;
  }

  process.GetOutput(output);
  return true;
}

}  // namespace debugd
