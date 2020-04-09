/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <base/command_line.h>
#include <brillo/syslog_logging.h>
#include <gtest/gtest.h>

#include "cros-camera/camera_service_connector.h"
#include "cros-camera/common.h"

namespace cros {
namespace tests {

class ConnectorEnvironment : public ::testing::Environment {
 public:
  void SetUp() override {
    ASSERT_EQ(cros_cam_init(), 0);
    LOGF(INFO) << "Camera connector initialized";
  }

  void TearDown() override {
    cros_cam_exit();
    LOGF(INFO) << "Camera connector exited";
  }
};

TEST(ConnectorTest, GetInfo) {
  // TODO(b/151047930): Implement the test.
}

}  // namespace tests
}  // namespace cros

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  ::testing::AddGlobalTestEnvironment(new cros::tests::ConnectorEnvironment());
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
