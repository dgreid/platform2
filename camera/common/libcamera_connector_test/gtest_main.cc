/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <vector>

#include <base/command_line.h>
#include <base/posix/safe_strerror.h>
#include <base/strings/stringprintf.h>
#include <base/synchronization/waitable_event.h>
#include <brillo/syslog_logging.h>
#include <gtest/gtest.h>
#include <libyuv.h>
#include <linux/videodev2.h>

#include "cros-camera/camera_service_connector.h"
#include "cros-camera/common.h"

namespace cros {
namespace tests {

namespace {

constexpr auto kDefaultTimeout = base::TimeDelta::FromSeconds(5);

// These should be supported on all devices.
constexpr cros_cam_format_info_t kTestFormats[] = {
    {V4L2_PIX_FMT_NV12, 640, 480, 30},
    {V4L2_PIX_FMT_MJPEG, 640, 480, 30},
};

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

bool IsSameFormat(const cros_cam_format_info_t& fmt1,
                  const cros_cam_format_info_t& fmt2) {
  return fmt1.fourcc == fmt2.fourcc && fmt1.width == fmt2.width &&
         fmt1.height == fmt2.height && fmt1.fps == fmt2.fps;
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

class I420Buffer {
 public:
  explicit I420Buffer(int width = 0, int height = 0)
      : width_(width), height_(height), data_(DataSize()) {}

  static I420Buffer Create(const cros_cam_frame_t* frame) {
    const cros_cam_format_info_t& format = frame->format;
    I420Buffer buf(format.width, format.height);

    const cros_cam_plane_t* planes = frame->planes;

    auto expect_empty = [&](const cros_cam_plane_t& plane) {
      EXPECT_EQ(plane.size, 0);
      EXPECT_EQ(plane.stride, 0);
      EXPECT_EQ(plane.data, nullptr);
    };

    switch (format.fourcc) {
      case V4L2_PIX_FMT_NV12: {
        expect_empty(planes[2]);
        expect_empty(planes[3]);
        int ret = libyuv::NV12ToI420(
            planes[0].data, planes[0].stride, planes[1].data, planes[1].stride,
            buf.DataY(), buf.StrideY(), buf.DataU(), buf.StrideU(), buf.DataY(),
            buf.StrideV(), buf.Width(), buf.Height());
        EXPECT_EQ(ret, 0) << "invalid NV12 frame";
        break;
      }
      case V4L2_PIX_FMT_MJPEG: {
        expect_empty(planes[1]);
        expect_empty(planes[2]);
        expect_empty(planes[3]);
        int ret = libyuv::MJPGToI420(
            planes[0].data, planes[0].size, buf.DataY(), buf.StrideY(),
            buf.DataU(), buf.StrideU(), buf.DataV(), buf.StrideV(),
            format.width, format.height, buf.Width(), buf.Height());
        EXPECT_EQ(ret, 0) << "invalid MJPEG frame";
        break;
      }
      default:
        ADD_FAILURE() << "unexpected fourcc: " << FourccToString(format.fourcc);
    }
    return buf;
  }

  int Width() const { return width_; }
  int Height() const { return height_; }

  int StrideY() const { return width_; }
  int StrideU() const { return (width_ + 1) / 2; }
  int StrideV() const { return (width_ + 1) / 2; }

  uint8_t* DataY() { return data_.data(); }
  uint8_t* DataU() { return DataY() + StrideY() * Height(); }
  uint8_t* DataV() { return DataU() + StrideU() * HalfHeight(); }

 private:
  int HalfHeight() const { return (height_ + 1) / 2; }
  int HalfWidth() const { return (width_ + 1) / 2; }
  int DataSize() const {
    return StrideY() * Height() + (StrideU() + StrideV()) * HalfHeight();
  }

  int width_;
  int height_;
  std::vector<uint8_t> data_;
};

class FrameCapturer {
 public:
  FrameCapturer& SetNumFrames(int num_frames) {
    num_frames_ = num_frames;
    return *this;
  }

  FrameCapturer& SetDuration(base::TimeDelta duration) {
    duration_ = duration;
    return *this;
  }

  int Run(int id, cros_cam_format_info_t format) {
    num_frames_captured_ = 0;
    capture_done_.Reset();
    format_ = format;

    if (cros_cam_start_capture(id, &format, &FrameCapturer::CaptureCallback,
                               this) != 0) {
      ADD_FAILURE() << "failed to start capture";
      return 0;
    }

    // Wait until |duration_| passed or |num_frames_| captured.
    capture_done_.TimedWait(duration_);

    // TODO(b/151047930): Check the return value and only call this when
    // TimedWait() return false. There is a bug in libcamera_connector so we
    // cannot do this yet, otherwise the next cros_cam_start_capture() will
    // fail.
    cros_cam_stop_capture(id);
    if (!capture_done_.IsSignaled()) {
      capture_done_.Signal();
    }

    LOGF(INFO) << "Captured " << num_frames_captured_ << " frames";
    return num_frames_captured_;
  }

  I420Buffer LastI420Frame() const { return last_i420_frame_; }

 private:
  // non-zero return value should stop the capture.
  int GotCaptureResult(const cros_cam_capture_result_t* result) {
    if (capture_done_.IsSignaled()) {
      ADD_FAILURE() << "got capture result after capture is done";
      return -1;
    }

    if (result->status != 0) {
      ADD_FAILURE() << "capture result error: "
                    << base::safe_strerror(-result->status);
      return -1;
    }

    const cros_cam_frame_t* frame = result->frame;
    EXPECT_TRUE(IsSameFormat(frame->format, format_));
    last_i420_frame_ = I420Buffer::Create(frame);

    num_frames_captured_++;
    if (num_frames_captured_ == num_frames_) {
      capture_done_.Signal();
      return -1;
    }

    return 0;
  }

  static int CaptureCallback(void* context,
                             const cros_cam_capture_result_t* result) {
    auto* self = reinterpret_cast<FrameCapturer*>(context);
    return self->GotCaptureResult(result);
  }

  int num_frames_ = INT_MAX;
  base::TimeDelta duration_ = kDefaultTimeout;
  cros_cam_format_info_t format_;

  int num_frames_captured_;
  base::WaitableEvent capture_done_;
  I420Buffer last_i420_frame_;
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

  int FindIdForFormat(const cros_cam_format_info_t& format) {
    for (const auto& info : camera_infos_) {
      for (int i = 0; i < info.format_count; i++) {
        if (IsSameFormat(format, info.format_info[i])) {
          return info.id;
        }
      }
    }
    return -1;
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

  static int GetCamInfoCallback(void* context,
                                const cros_cam_info_t* info,
                                unsigned is_removed) {
    auto* self = reinterpret_cast<CameraClient*>(context);
    return self->GotCameraInfo(info, is_removed);
  }
  std::vector<cros_cam_info_t> camera_infos_;
  bool camera_info_frozen_ = false;
};

class CaptureTest
    : public ::testing::Test,
      public ::testing::WithParamInterface<cros_cam_format_info_t> {
 protected:
  void SetUp() override {
    client_.ProbeCameraInfo();
    format_ = GetParam();
    camera_id_ = client_.FindIdForFormat(format_);
    ASSERT_NE(camera_id_, -1);
  }

  CameraClient client_;
  FrameCapturer capturer_;

  int camera_id_;
  cros_cam_format_info_t format_;
};

TEST(ConnectorTest, GetInfo) {
  CameraClient client;
  client.ProbeCameraInfo();
  client.DumpCameraInfo();
}

TEST_P(CaptureTest, OneFrame) {
  int num_frames_captured = capturer_.SetNumFrames(1).Run(camera_id_, format_);
  EXPECT_EQ(num_frames_captured, 1);
}

TEST_P(CaptureTest, ThreeSeconds) {
  const auto kDuration = base::TimeDelta::FromSeconds(3);
  int num_frames_captured =
      capturer_.SetDuration(kDuration).Run(camera_id_, format_);
  // It's expected to get more than 1 frame in 3s.
  EXPECT_GT(num_frames_captured, 1);
}

TEST(ConnectorTest, CompareFrames) {
  CameraClient client;
  client.ProbeCameraInfo();

  int id = client.FindIdForFormat(kTestFormats[0]);
  ASSERT_NE(id, -1);

  FrameCapturer capturer;
  capturer.SetNumFrames(1);

  ASSERT_EQ(capturer.Run(id, kTestFormats[0]), 1);
  I420Buffer frame1 = capturer.LastI420Frame();

  ASSERT_EQ(capturer.Run(id, kTestFormats[1]), 1);
  I420Buffer frame2 = capturer.LastI420Frame();

  double ssim = libyuv::I420Ssim(
      frame1.DataY(), frame1.StrideY(), frame1.DataU(), frame1.StrideU(),
      frame1.DataV(), frame1.StrideV(), frame2.DataY(), frame2.StrideY(),
      frame2.DataU(), frame2.StrideU(), frame2.DataV(), frame2.StrideV(),
      frame1.Width(), frame1.Height());
  LOGF(INFO) << "ssim = " << ssim;

  // It's expected have two similar but not exactly same frames captured in the
  // short period with MJPEG and NV12. The normal values are around 0.7~0.8.
  EXPECT_GE(ssim, 0.4);

  // If the frames are exactly same (ssim = 1.0), the frame is likely broken
  // such as all pixels are black. Set the threshold as 0.99 for potential jpeg
  // artifacts and floating point error.
  EXPECT_LE(ssim, 0.99);
}

INSTANTIATE_TEST_SUITE_P(ConnectorTest,
                         CaptureTest,
                         ::testing::ValuesIn(kTestFormats),
                         [](const auto& info) {
                           const cros_cam_format_info_t& fmt = info.param;
                           return base::StringPrintf(
                               "%s_%ux%u_%ufps",
                               FourccToString(fmt.fourcc).c_str(), fmt.width,
                               fmt.height, fmt.fps);
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
