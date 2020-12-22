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

  process.SetCapabilities(CAP_TO_MASK(CAP_SYS_ADMIN));
  if (!process.Init()) {
    *output = "<process init failed>";
    return false;
  }

  process.AddArg(kDmesgPath);

  if (!AddBoolOption(&process, options, "show-delta", "-d", error) ||
      !AddBoolOption(&process, options, "human", "--human", error) ||
      !AddBoolOption(&process, options, "kernel", "-k", error) ||
      !AddBoolOption(&process, options, "color", "--color=always", error) ||
      !AddBoolOption(&process, options, "force-prefix", "-p", error) ||
      !AddBoolOption(&process, options, "raw", "-r", error) ||
      !AddBoolOption(&process, options, "ctime", "-T", error) ||
      !AddBoolOption(&process, options, "notime", "-t", error) ||
      !AddBoolOption(&process, options, "userspace", "-u", error) ||
      !AddBoolOption(&process, options, "decode", "-x", error)) {
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
