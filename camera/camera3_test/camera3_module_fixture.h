// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_CAMERA3_TEST_CAMERA3_MODULE_FIXTURE_H_
#define CAMERA_CAMERA3_TEST_CAMERA3_MODULE_FIXTURE_H_

#include <dlfcn.h>

#include <map>
#include <tuple>
#include <vector>

#include <base/logging.h>
#include <base/synchronization/lock.h>
#include <cros-camera/camera_thread.h>
#include <gtest/gtest.h>
#include <hardware/camera3.h>

namespace camera3_test {

const int kMaxNumCameras = 2;
const size_t kNumOfElementsInStreamConfigEntry = 4;
enum {
  STREAM_CONFIG_FORMAT_INDEX,
  STREAM_CONFIG_WIDTH_INDEX,
  STREAM_CONFIG_HEIGHT_INDEX,
  STREAM_CONFIG_DIRECTION_INDEX,
  STREAM_CONFIG_STALL_DURATION_INDEX = STREAM_CONFIG_DIRECTION_INDEX,
  STREAM_CONFIG_MIN_DURATION_INDEX = STREAM_CONFIG_DIRECTION_INDEX
};

// Returns true if the |actual_level| is equal to or beyond the
// |required_level|.
bool isHardwareLevelSupported(int actual_level, int required_level);

// Get recording parameter list of camera id, width, height and frame rate
std::vector<std::tuple<int, int32_t, int32_t, float>> ParseRecordingParams();

class ResolutionInfo {
 public:
  ResolutionInfo(int32_t width, int32_t height)
      : width_(width), height_(height) {}

  ResolutionInfo() : width_(0), height_(0) {}

  int32_t Width() const;

  int32_t Height() const;

  int32_t Area() const;

  bool operator==(const ResolutionInfo& resolution) const;

  bool operator<(const ResolutionInfo& resolution) const;

  friend std::ostream& operator<<(std::ostream& out,
                                  const ResolutionInfo& info);

 private:
  int32_t width_, height_;
};

class CameraModuleCallbacksHandler {
 public:
  static void camera_device_status_change(
      const camera_module_callbacks_t* callbacks,
      int camera_id,
      int new_status);

  static void torch_mode_status_change(
      const camera_module_callbacks_t* callbacks,
      const char* camera_id,
      int new_status);

  static CameraModuleCallbacksHandler* GetInstance();

  bool IsExternalCameraPresent(int camera_id);

 private:
  void CameraDeviceStatusChange(int camera_id,
                                camera_device_status_t new_status);

  void TorchModeStatusChange(int camera_id, torch_mode_status_t new_status);

  // All operations are sequenced by |lock_|.
  base::Lock lock_;

  std::map<int, camera_device_status_t> device_status_;
};

struct CameraModuleCallbacksAux : camera_module_callbacks_t {
  CameraModuleCallbacksHandler* handler;
};

class Camera3Module {
 public:
  Camera3Module();

  // Initialize
  int Initialize();

  // Get number of cameras
  int GetNumberOfCameras();

  // Get list of camera IDs
  std::vector<int> GetCameraIds();

  // Get list of test camera IDs if specify in cmdline args, or default use
  // |GetCameraIds|
  std::vector<int> GetTestCameraIds();

  // Open camera device
  camera3_device* OpenDevice(int cam_id);

  // Close camera device
  int CloseDevice(camera3_device* cam_device);

  // Check if a stream format is supported
  bool IsFormatAvailable(int cam_id, int format);

  // Get camera information
  int GetCameraInfo(int cam_id, camera_info* info);

  // Get the image output formats in this stream configuration
  std::vector<int32_t> GetOutputFormats(int cam_id);

  // Get the image output resolutions in this stream configuration
  std::vector<ResolutionInfo> GetSortedOutputResolutions(int cam_id,
                                                         int32_t format);

  // Get the stall duration for the format/size combination (in nanoseconds)
  int64_t GetOutputStallDuration(int cam_id,
                                 int32_t format,
                                 const ResolutionInfo& resolution);

  //  Get the minimum frame duration
  int64_t GetOutputMinFrameDuration(int cam_id,
                                    int32_t format,
                                    const ResolutionInfo& resolution);

 private:
  void GetNumberOfCamerasOnHalThread(int* result);

  void GetCameraInfoOnHalThread(int cam_id, camera_info* info, int* result);

  void OpenDeviceOnHalThread(int cam_id, camera3_device_t** cam_device);

  void CloseDeviceOnDevThread(camera3_device_t* cam_device, int* result);

  void GetStreamConfigEntry(int cam_id,
                            int32_t key,
                            camera_metadata_ro_entry_t* entry);

  int64_t GetOutputKeyParameterI64(int cam_id,
                                   int32_t format,
                                   const ResolutionInfo& resolution,
                                   int32_t key,
                                   int32_t index);

  const camera_module_t* cam_module_;

  // Id of cameras to be tested exclusively. Empty vector for test all available
  // cameras.
  std::vector<int> test_camera_ids_;

  // This thread is needed because of the Chrome OS camera HAL adapter
  // assumption that all the camera_module functions should be called on the
  // same Chromium thread. It is expected to start this thread before gtest
  // initialization in main() because test case instantiation needs it running
  // to get the camera ID list.
  cros::CameraThread* hal_thread_;

  // Use a separate thread from |hal_thread_| to close camera device to
  // simulate hal_adapter behavior.
  cros::CameraThread dev_thread_;

  DISALLOW_COPY_AND_ASSIGN(Camera3Module);
};

class Camera3ModuleFixture : public testing::Test {
 public:
  Camera3ModuleFixture() {}

  void SetUp() override;

 protected:
  Camera3Module cam_module_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Camera3ModuleFixture);
};

}  // namespace camera3_test

#endif  // CAMERA_CAMERA3_TEST_CAMERA3_MODULE_FIXTURE_H_
