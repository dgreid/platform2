/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_V1_V4L2_CAMERA_DEVICE_H_
#define CAMERA_HAL_USB_V1_V4L2_CAMERA_DEVICE_H_

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>

#include "cros-camera/timezone.h"
#include "hal/usb_v1/camera_device_delegate.h"

namespace arc {

class V4L2CameraDevice : public CameraDeviceDelegate {
 public:
  V4L2CameraDevice();
  V4L2CameraDevice(const V4L2CameraDevice&) = delete;
  V4L2CameraDevice& operator=(const V4L2CameraDevice&) = delete;

  ~V4L2CameraDevice() override;

  int Connect(const std::string& device_path) override;
  void Disconnect() override;
  int StreamOn(uint32_t width,
               uint32_t height,
               uint32_t pixel_format,
               float frame_rate,
               std::vector<int>* fds,
               uint32_t* buffer_size) override;
  int StreamOff() override;
  int GetNextFrameBuffer(uint32_t* buffer_id, uint32_t* data_size) override;
  int ReuseFrameBuffer(uint32_t buffer_id) override;
  const SupportedFormats GetDeviceSupportedFormats(
      const std::string& device_path) override;
  const DeviceInfos GetCameraDeviceInfos() override;

 private:
  static bool IsCameraDevice(const std::string& device_path);

  std::vector<float> GetFrameRateList(int fd,
                                      uint32_t fourcc,
                                      uint32_t width,
                                      uint32_t height);

  const std::unordered_map<std::string, std::string> GetCameraDevicesByPattern(
      std::string pattern);

  int RetryDeviceOpen(const std::string& device_path, int flags);

  // Get power frequency supported from device.
  cros::PowerLineFrequency GetPowerLineFrequency(
      const std::string& device_path);

  // Set power frequency supported from device.
  int SetPowerLineFrequency(cros::PowerLineFrequency setting);

  // The number of video buffers we want to request in kernel.
  const int kNumVideoBuffers = 4;

  // The opened device fd.
  base::ScopedFD device_fd_;

  // StreamOn state
  bool stream_on_;

  // True if the buffer is used by client after GetNextFrameBuffer().
  std::vector<bool> buffers_at_client_;
};

}  // namespace arc

#endif  // CAMERA_HAL_USB_V1_V4L2_CAMERA_DEVICE_H_
