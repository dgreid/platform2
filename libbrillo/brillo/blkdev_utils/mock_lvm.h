// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_BLKDEV_UTILS_MOCK_LVM_H_
#define LIBBRILLO_BRILLO_BLKDEV_UTILS_MOCK_LVM_H_

#include <brillo/process/process_mock.h>
#include <gmock/gmock.h>

#include "brillo/blkdev_utils/lvm_device.h"

#include <string>
#include <vector>

using testing::_;
using testing::Return;
using testing::SetArgPointee;
using testing::WithArg;

namespace brillo {

class MockLvmCommandRunner : public LvmCommandRunner {
 public:
  MockLvmCommandRunner() : LvmCommandRunner() {
    ON_CALL(*this, RunCommand(_)).WillByDefault(Return(true));
    ON_CALL(*this, RunProcess(_, _)).WillByDefault(Return(true));
  }

  virtual ~MockLvmCommandRunner() {}

  MOCK_METHOD(bool, RunCommand, (const std::vector<std::string>&), (override));
  MOCK_METHOD(bool,
              RunProcess,
              (const std::vector<std::string>&, std::string*),
              (override));
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_BLKDEV_UTILS_MOCK_LVM_H_
