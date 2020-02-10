// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera3_test/camera3_recording_fixture.h"

namespace camera3_test {

void Camera3RecordingFixture::SetUp() {
  ASSERT_EQ(0, cam_service_.Initialize(
                   Camera3Service::ProcessStillCaptureResultCallback(),
                   base::Bind(&Camera3RecordingFixture::ProcessRecordingResult,
                              base::Unretained(this))))
      << "Failed to initialize camera service";
}

void Camera3RecordingFixture::ProcessRecordingResult(
    int cam_id, uint32_t /*frame_number*/, ScopedCameraMetadata metadata) {
  VLOGF_ENTER();
  camera_metadata_ro_entry_t entry;
  ASSERT_EQ(0, find_camera_metadata_ro_entry(metadata.get(),
                                             ANDROID_SENSOR_TIMESTAMP, &entry))
      << "Failed to get sensor timestamp in recording result";
  sensor_timestamp_map_[cam_id].push_back(entry.data.i64[0]);
}

// Test parameters:
// - Camera ID, width, height, frame rate
class Camera3BasicRecordingTest
    : public Camera3RecordingFixture,
      public ::testing::WithParamInterface<
          std::tuple<int32_t, int32_t, int32_t, float>> {
 public:
  const int32_t kRecordingDurationMs = 3000;
  // Margin of frame duration in percetange. The value is adopted from
  // android.hardware.camera2.cts.RecordingTest#testBasicRecording.
  const float kFrameDurationMargin = 20.0;
  // Tolerance of frame drop rate in percetange
  const float kFrameDropRateTolerance = 5.0;

  Camera3BasicRecordingTest()
      : Camera3RecordingFixture(std::vector<int>(1, std::get<0>(GetParam()))),
        cam_id_(std::get<0>(GetParam())),
        recording_resolution_(std::get<1>(GetParam()), std::get<2>(GetParam())),
        recording_frame_rate_(std::get<3>(GetParam())) {}

 protected:
  // |duration_ms|: total duration of recording in milliseconds
  // |frame_duration_ms|: duration of each frame in milliseconds
  void ValidateRecordingFrameRate(float duration_ms, float frame_duration_ms);

  int cam_id_;

  ResolutionInfo recording_resolution_;

  float recording_frame_rate_;
};

void Camera3BasicRecordingTest::ValidateRecordingFrameRate(
    float duration_ms, float frame_duration_ms) {
  ASSERT_NE(0, duration_ms);
  ASSERT_NE(0, frame_duration_ms);
  float max_frame_duration_ms =
      frame_duration_ms * (1.0 + kFrameDurationMargin / 100.0);
  float min_frame_duration_ms =
      frame_duration_ms * (1.0 - kFrameDurationMargin / 100.0);
  uint32_t frame_drop_count = 0;
  int64_t prev_timestamp = sensor_timestamp_map_[cam_id_].front();
  sensor_timestamp_map_[cam_id_].pop_front();
  while (!sensor_timestamp_map_[cam_id_].empty()) {
    int64_t cur_timestamp = sensor_timestamp_map_[cam_id_].front();
    sensor_timestamp_map_[cam_id_].pop_front();
    if (static_cast<float>(cur_timestamp - prev_timestamp) / 1000000 >
            max_frame_duration_ms ||
        static_cast<float>(cur_timestamp - prev_timestamp) / 1000000 <
            min_frame_duration_ms) {
      VLOGF(1) << "Frame drop at "
               << (prev_timestamp / 1000000 +
                   static_cast<int64_t>(frame_duration_ms))
               << " ms, actual " << cur_timestamp / 1000000 << " ms";
      ++frame_drop_count;
    }
    prev_timestamp = cur_timestamp;
  }
  float frame_drop_rate =
      100.0 * frame_drop_count * frame_duration_ms / duration_ms;
  ASSERT_LT(frame_drop_rate, kFrameDropRateTolerance)
      << "Camera " << cam_id_
      << " Video frame drop rate too high: " << frame_drop_rate
      << ", tolerance " << kFrameDropRateTolerance;
}

TEST_P(Camera3BasicRecordingTest, BasicRecording) {
#define ARRAY_SIZE(A) (sizeof(A) / sizeof(*(A)))
  // Choose a preview resolution that is equal to or smaller than full HD so as
  // to avoid the full-sized one used for the ZSL opaque stream. Ideally we
  // should check it against the display resolution, but this should do for now.
  // 1920x1088 is used here since on certain hardware alignment to 16 or higher
  // is required.
  const ResolutionInfo full_hd_alt(1920, 1088);
  auto preview_resolutions =
      cam_service_.GetStaticInfo(cam_id_)->GetSortedOutputResolutions(
          HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED);
  ResolutionInfo preview_resolution(0, 0);
  for (auto it = preview_resolutions.rbegin(); it != preview_resolutions.rend();
       ++it) {
    // Both width and height should be equal to or smaller than the bound
    // according to getSupportedPreviewSizes() of cts/CameraTestUtils.java.
    if (it->Width() <= full_hd_alt.Width() &&
        it->Height() <= full_hd_alt.Height()) {
      preview_resolution = *it;
      break;
    }
  }
  ResolutionInfo jpeg_resolution(0, 0);
  cam_service_.StartPreview(cam_id_, preview_resolution, jpeg_resolution,
                            recording_resolution_);

  ScopedCameraMetadata recording_metadata(
      clone_camera_metadata(cam_service_.ConstructDefaultRequestSettings(
          cam_id_, CAMERA3_TEMPLATE_VIDEO_RECORD)));
  ASSERT_NE(nullptr, recording_metadata.get());
  int32_t fps_range[] = {static_cast<int32_t>(recording_frame_rate_),
                         static_cast<int32_t>(recording_frame_rate_)};
  EXPECT_EQ(0, UpdateMetadata(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, fps_range,
                              ARRAY_SIZE(fps_range), &recording_metadata));
  cam_service_.StartRecording(cam_id_, recording_metadata.get());
  usleep(kRecordingDurationMs * 1000);
  cam_service_.StopRecording(cam_id_);
  float frame_duration_ms = 1000.0 / recording_frame_rate_;
  float duration_ms = sensor_timestamp_map_[cam_id_].size() * frame_duration_ms;
  ValidateRecordingFrameRate(duration_ms, frame_duration_ms);

  cam_service_.StopPreview(cam_id_);
}

INSTANTIATE_TEST_CASE_P(Camera3RecordingFixture,
                        Camera3BasicRecordingTest,
                        ::testing::ValuesIn(ParseRecordingParams()));

}  // namespace camera3_test
