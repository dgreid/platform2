// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera3_test/camera3_preview_fixture.h"

#include <unistd.h>

#include <base/bind.h>

namespace camera3_test {

void Camera3PreviewFixture::SetUp() {
  ASSERT_EQ(0, cam_service_.Initialize(
                   Camera3Service::ProcessStillCaptureResultCallback(),
                   Camera3Service::ProcessRecordingResultCallback()))
      << "Failed to initialize camera service";
}

void Camera3PreviewFixture::TearDown() {
  cam_service_.Destroy();
}

// Test parameters:
// - Camera ID
class Camera3SinglePreviewTest : public Camera3PreviewFixture,
                                 public ::testing::WithParamInterface<int32_t> {
 public:
  const uint32_t kNumPreviewFrames = 10;
  const uint32_t kTimeoutMsPerFrame = 1000;

  Camera3SinglePreviewTest()
      : Camera3PreviewFixture(std::vector<int>(1, GetParam())),
        cam_id_(GetParam()) {}

 protected:
  int cam_id_;
};

TEST_P(Camera3SinglePreviewTest, Camera3BasicPreviewTest) {
  auto resolutions =
      cam_service_.GetStaticInfo(cam_id_)->GetSortedOutputResolutions(
          HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED);
  for (const auto& resolution : resolutions) {
    ResolutionInfo jpeg_resolution(0, 0), recording_resolution(0, 0);
    ASSERT_EQ(0, cam_service_.StartPreview(cam_id_, resolution, jpeg_resolution,
                                           recording_resolution))
        << "Starting preview fails";
    ASSERT_EQ(0, cam_service_.WaitForPreviewFrames(cam_id_, kNumPreviewFrames,
                                                   kTimeoutMsPerFrame));
    cam_service_.StopPreview(cam_id_);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Camera3PreviewTest,
    Camera3SinglePreviewTest,
    ::testing::ValuesIn(Camera3Module().GetTestCameraIds()));

}  // namespace camera3_test
