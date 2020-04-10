/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <vector>

#include <base/command_line.h>
#include <base/strings/stringprintf.h>
#include <brillo/syslog_logging.h>
#include <gtest/gtest.h>

#include "cros-camera/camera_service_connector.h"
#include "cros-camera/common.h"

namespace cros {
namespace tests {

namespace {

std::string FourccToString(uint32_t fourcc) {
  std::string result = "0000";
  for (int i = 0; i < 4; i++) {
    auto c = static_cast<char>(fourcc & 0xFF);
    if (c <= 0x1f || c >= 0x7f) {
      return base::StringPrintf("%#x", fourcc);
    }
    result[i] = c;
    fourcc >>= 8;
  }
  return result;
}

std::string CameraFormatInfoToString(const cros_cam_format_info_t& info) {
  return base::StringPrintf("%s %4ux%4u %3ufps",
                            FourccToString(info.fourcc).c_str(), info.width,
                            info.height, info.fps);
}

void DumpCameraInfo(const cros_cam_info_t& info) {
  LOGF(INFO) << "id: " << info.id;
  LOGF(INFO) << "name: " << info.name;
  LOGF(INFO) << "format_count: " << info.format_count;
  for (int i = 0; i < info.format_count; i++) {
    LOGF(INFO) << base::StringPrintf(
        "Format %2d: %s", i,
        CameraFormatInfoToString(info.format_info[i]).c_str());
  }
}

}  // namespace

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

class CameraClient {
 public:
  size_t GetCameraCount() { return camera_infos_.size(); }

  int GetCameraInfo() {
    int ret = cros_cam_get_cam_info(&CameraClient::GetCamInfoCallback, this);
    // All connected cameras should be already reported by the callback
    // function, set the frozen flag to capture unexpected hotplug events
    // during test. Please see the comment of cros_cam_get_cam_info() for more
    // details.
    camera_info_frozen_ = true;
    return ret;
  }

 private:
  int GotCameraInfo(const cros_cam_info_t* info, unsigned is_removed) {
    EXPECT_FALSE(camera_info_frozen_) << "unexpected hotplug events";
    EXPECT_EQ(is_removed, 0) << "unexpected removing events";
    EXPECT_GT(info->format_count, 0) << "no available formats";
    camera_infos_.push_back(*info);
    LOGF(INFO) << "Got camera info";
    DumpCameraInfo(*info);
    return 0;
  }

  static int GetCamInfoCallback(void* context,
                                const cros_cam_info_t* info,
                                unsigned is_removed) {
    auto* self = reinterpret_cast<CameraClient*>(context);
    return self->GotCameraInfo(info, is_removed);
  }

  std::vector<cros_cam_info_t> camera_infos_;
  bool camera_info_frozen_ = false;
};

TEST(ConnectorTest, GetInfo) {
  CameraClient client;
  ASSERT_EQ(client.GetCameraInfo(), 0);
  EXPECT_GT(client.GetCameraCount(), 0) << "no camera found";
}

TEST(ConnectorTest, Capture) {
  // TODO(b/151047930): Implement the test.
}

}  // namespace tests
}  // namespace cros

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToStderr);

  ::testing::AddGlobalTestEnvironment(new cros::tests::ConnectorEnvironment());
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
