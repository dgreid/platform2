// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/ec_typec_tool.h"

#include <vector>

#include <base/files/file_path.h>
#include <base/strings/stringprintf.h>
#include <brillo/errors/error.h>

#include "debugd/src/ectool_util.h"

namespace {

constexpr char kSandboxDirPath[] = "/usr/share/policy/";
constexpr char kRunAs[] = "typecd_ec";

// Returns the ectool policy file corresponding to the provided
// |ectool_command|.
std::string GetEctoolPolicyFile(const std::string& ectool_command) {
  return base::StringPrintf("ectool_%s-seccomp.policy", ectool_command.c_str());
}

}  // namespace

namespace debugd {

std::string EcTypeCTool::GetInventory() {
  std::string output;
  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath).Append(GetEctoolPolicyFile("typec"));
  std::vector<std::string> ectool_args = {"inventory"};

  brillo::ErrorPtr error;
  if (!RunEctoolWithArgs(&error, seccomp_policy_path, ectool_args, kRunAs,
                         &output))
    output.clear();

  return output;
}

}  // namespace debugd
