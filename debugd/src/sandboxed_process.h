// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_SANDBOXED_PROCESS_H_
#define DEBUGD_SRC_SANDBOXED_PROCESS_H_

#include <string>
#include <vector>

#include <brillo/process.h>

namespace debugd {

class SandboxedProcess : public brillo::ProcessImpl {
 public:
  SandboxedProcess();
  ~SandboxedProcess() override;

  // Get the full path of a helper executable located at the |relative_path|
  // relative to the debugd helpers directory. Return false if the full path
  // is too long.
  static bool GetHelperPath(const std::string& relative_path,
                            std::string* full_path);

  virtual bool Init();

  void BindFd(int parent_fd, int child_fd) override;

  // Disable the default sandboxing for this process.
  virtual void DisableSandbox();

  // Change the default sandboxing for this process.
  virtual void SandboxAs(const std::string& user, const std::string& group);

  // Allow this process to access the root mount namespace.
  virtual void AllowAccessRootMountNamespace();

  // Kill the sandboxed process' process group.
  virtual bool KillProcessGroup();

  static const char *kDefaultUser;
  static const char *kDefaultGroup;

 private:
  bool sandboxing_;
  bool access_root_mount_ns_;
  std::string user_;
  std::string group_;
  std::vector<int> bound_fds_;
};

}  // namespace debugd

#endif  // DEBUGD_SRC_SANDBOXED_PROCESS_H_
