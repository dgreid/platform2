// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_VPD_PROCESS_H_
#define LOGIN_MANAGER_VPD_PROCESS_H_

namespace login_manager {

class SystemUtils;

class VpdProcess {
 public:
  virtual ~VpdProcess() {}

  // Run VPD setter script as a separate process.  Returns whether fork() was
  // successful.
  virtual bool RunInBackground(SystemUtils* utils, bool block_devmode) = 0;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_VPD_PROCESS_H_
