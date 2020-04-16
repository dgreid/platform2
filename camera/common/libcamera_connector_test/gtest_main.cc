/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <vector>

#include <base/command_line.h>
#include <base/synchronization/waitable_event.h>
#include <base/strings/stringprintf.h>
#include <brillo/syslog_logging.h>
#include <gtest/gtest.h>
#include <linux/videodev2.h>

#include "cros-camera/camera_service_connector.h"
#include "cros-camera/common.h"

namespace cros {
namespace tests {

namespace {

constexpr auto kDefaultTimeout = base::TimeDelta::FromSeconds(5);

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
  void ProbeCameraInfo() {
    ASSERT_EQ(cros_cam_get_cam_info(&CameraClient::GetCamInfoCallback, this),
              0);
    EXPECT_GT(camera_infos_.size(), 0) << "no camera found";
    // All connected cameras should be already reported by the callback
    // function, set the frozen flag to capture unexpected hotplug events
    // during test. Please see the comment of cros_cam_get_cam_info() for more
    // details.
    camera_info_frozen_ = true;
  }

  void DumpCameraInfo() {
    for (const auto& info : camera_infos_) {
      LOGF(INFO) << "id: " << info.id;
      LOGF(INFO) << "name: " << info.name;
      LOGF(INFO) << "format_count: " << info.format_count;
      for (int i = 0; i < info.format_count; i++) {
        LOGF(INFO) << base::StringPrintf(
            "Format %2d: %s", i,
            CameraFormatInfoToString(info.format_info[i]).c_str());
      }
    }
  }

  void Capture(uint32_t fourcc, int width, int height, int fps) {
    LOGF(INFO) << base::StringPrintf("Capture one %s frame with %dx%d %dfps",
                                     FormatToString(fourcc).c_str(), width,
                                     height, fps);

    cros_cam_device_t id;
    const cros_cam_format_info_t* format;
    ASSERT_TRUE(ResolveCaptureParams(fourcc, width, height, fps, &id, &format));

    frame_captured_.Reset();
    ASSERT_EQ(cros_cam_start_capture(id, format, &CameraClient::CaptureCallback,
                                     this),
              0);
    EXPECT_TRUE(frame_captured_.TimedWait(kDefaultTimeout));
    cros_cam_stop_capture(id);
  }

 private:
  int GotCameraInfo(const cros_cam_info_t* info, unsigned is_removed) {
    EXPECT_FALSE(camera_info_frozen_) << "unexpected hotplug events";
    EXPECT_EQ(is_removed, 0) << "unexpected removing events";
    EXPECT_GT(info->format_count, 0) << "no available formats";
    camera_infos_.push_back(*info);
    LOGF(INFO) << "Got camera info for id: " << info->id;
    return 0;
  }

  int GotFrame(const cros_cam_frame_t* frame) {
    // TODO(b/151047930): Verify the content of frame.
    EXPECT_FALSE(frame_captured_.IsSignaled()) << "got too many frames";
    frame_captured_.Signal();

    // non-zero return value should stop the capture.
    return -1;
  }

  bool ResolveCaptureParams(uint32_t fourcc,
                            int width,
                            int height,
                            int fps,
                            cros_cam_device_t* id,
                            const cros_cam_format_info_t** format) {
    for (const auto& info : camera_infos_) {
      for (int i = 0; i < info.format_count; i++) {
        const auto& fmt = info.format_info[i];
        if (fmt.fourcc == fourcc && fmt.width == width &&
            fmt.height == height && fmt.fps == fps) {
          *id = info.id;
          *format = &fmt;
          return true;
        }
      }
    }
    return false;
  }

  static int GetCamInfoCallback(void* context,
                                const cros_cam_info_t* info,
                                unsigned is_removed) {
    auto* self = reinterpret_cast<CameraClient*>(context);
    return self->GotCameraInfo(info, is_removed);
  }

  static int CaptureCallback(void* context, const cros_cam_frame_t* frame) {
    auto* self = reinterpret_cast<CameraClient*>(context);
    return self->GotFrame(frame);
  }

  std::vector<cros_cam_info_t> camera_infos_;
  bool camera_info_frozen_ = false;

  base::WaitableEvent frame_captured_;
};

class CaptureTest : public ::testing::Test,
                    public ::testing::WithParamInterface<uint32_t> {
 protected:
  void SetUp() override { client_.ProbeCameraInfo(); }

  CameraClient client_;
};

TEST(ConnectorTest, GetInfo) {
  CameraClient client;
  client.ProbeCameraInfo();
  client.DumpCameraInfo();
}

TEST_P(CaptureTest, OneFrame) {
  uint32_t fourcc = GetParam();
  // This should be supported on all devices.
  client_.Capture(fourcc, 640, 480, 30);
}

INSTANTIATE_TEST_SUITE_P(ConnectorTest,
                         CaptureTest,
                         ::testing::Values(V4L2_PIX_FMT_NV12,
                                           V4L2_PIX_FMT_MJPEG),
                         [](const auto& info) {
                           return FourccToString(info.param);
                         });

}  // namespace tests
}  // namespace cros

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToStderr);
  logging::SetLogItems(/*enable_process_id=*/true, /*enable_thread_id=*/true,
                       /*enable_timestamp=*/true, /*enable_tickcount=*/false);

  ::testing::AddGlobalTestEnvironment(new cros::tests::ConnectorEnvironment());
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
