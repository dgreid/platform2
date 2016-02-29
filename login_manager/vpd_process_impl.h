// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_VPD_PROCESS_IMPL_H_
#define LOGIN_MANAGER_VPD_PROCESS_IMPL_H_

#include "login_manager/vpd_process.h"

namespace login_manager {

class VpdProcessImpl : public VpdProcess {
 public:
  virtual bool RunInBackground(SystemUtils* utils, bool block_devmode);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_VPD_PROCESS_IMPL_H_
