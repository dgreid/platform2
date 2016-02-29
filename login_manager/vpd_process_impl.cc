// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/vpd_process_impl.h"

#include <string>
#include <vector>

#include "login_manager/child_job.h"

namespace login_manager {

bool VpdProcessImpl::RunInBackground(SystemUtils* utils, bool block_devmode) {
  ChildJobInterface::Subprocess subprocess(0, utils);
  std::vector<std::string> argv;
  argv.push_back("/usr/sbin/block_devmode_vpd");
  if (block_devmode) {
    argv.push_back("--enable");
  } else {
    argv.push_back("--disable");
  }
  return subprocess.ForkAndExec(argv, std::vector<std::string>());
}

}  // namespace login_manager
