// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_MOCK_VPD_PROCESS_H_
#define LOGIN_MANAGER_MOCK_VPD_PROCESS_H_

#include <gmock/gmock.h>

#include "login_manager/vpd_process.h"

namespace login_manager {

class SystemUtils;

class MockVpdProcess : public VpdProcess {
 public:
  MOCK_METHOD2(RunInBackground, bool(SystemUtils* utils, bool block_devmode));
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_MOCK_VPD_PROCESS_H_
