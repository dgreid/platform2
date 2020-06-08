/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_V4L2_CAMERA_DEVICE_H_
#define CAMERA_HAL_USB_V4L2_CAMERA_DEVICE_H_

#include <time.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/synchronization/lock.h>

#include "cros-camera/timezone.h"
#include "hal/usb/common_types.h"

namespace cros {

struct ControlRange {
  int32_t minimum;
  int32_t maximum;
  int32_t step;
  int32_t default_value;
};

enum ControlType {
  kControlAutoWhiteBalance,
  kControlBrightness,
  kControlContrast,
  kControlExposureAutoPriority,  // 0 for constant frame rate
  kControlPan,
  kControlSaturation,
  kControlSharpness,
  kControlTilt,
  kControlZoom,
  kControlWhiteBalanceTemperature,
};

constexpr uint32_t kColorTemperatureAuto = 0;

// The class is thread-safe.
class V4L2CameraDevice {
 public:
  V4L2CameraDevice();
  explicit V4L2CameraDevice(const DeviceInfo& device_info);
  virtual ~V4L2CameraDevice();

  // Connect camera device with |device_path|. Return 0 if device is opened
  // successfully. Otherwise, return -|errno|.
  int Connect(const std::string& device_path);

  // Disconnect camera device. This function is a no-op if the camera device
  // is not connected. If the stream is on, this function will also stop the
  // stream.
  void Disconnect();

  // Enable camera device stream. Setup captured frame with |width|x|height|
  // resolution, |pixel_format|, |frame_rate|. Get frame buffer file descriptors
  // |fds| and |buffer_sizes|. |buffer_sizes| are the sizes allocated for each
  // buffer. The ownership of |fds| are transferred to the caller and |fds|
  // should be closed when done. Caller can memory map |fds| and should unmap
  // when done. Return 0 if device supports the format.  Otherwise, return
  // -|errno|. This function should be called after Connect().
  int StreamOn(uint32_t width,
               uint32_t height,
               uint32_t pixel_format,
               float frame_rate,
               std::vector<base::ScopedFD>* fds,
               std::vector<uint32_t>* buffer_sizes);

  // Disable camera device stream. Return 0 if device disables stream
  // successfully. Otherwise, return -|errno|. This function is a no-op if the
  // stream is already stopped.
  int StreamOff();

  // Get next frame buffer from device. Device returns the corresponding buffer
  // with |buffer_id|, |data_size| bytes and its v4l2 timestamp |v4l2_ts| and
  // userspace timestamp |user_ts| in nanoseconds.
  // |data_size| is how many bytes used in the buffer for this frame. Return 0
  // if device gets the buffer successfully. Otherwise, return -|errno|. Return
  // -EAGAIN immediately if next frame buffer is not ready. This function should
  // be called after StreamOn().
  int GetNextFrameBuffer(uint32_t* buffer_id,
                         uint32_t* data_size,
                         uint64_t* v4l2_ts,
                         uint64_t* user_ts);

  // Return |buffer_id| buffer to device. Return 0 if the buffer is returned
  // successfully. Otherwise, return -|errno|. This function should be called
  // after StreamOn().
  int ReuseFrameBuffer(uint32_t buffer_id);

  // Return true if buffer specified by |buffer_id| is filled and moved to
  // outgoing queue.
  bool IsBufferFilled(uint32_t buffer_id);

  // Return 0 if device set auto focus mode successfully. Otherwise, return
  // |-errno|.
  int SetAutoFocus(bool enable);

  // Return 0 if device sets color tepmerature successfully. Otherwise, return
  // |-errno|. Set |color_temperature| to |kColorTemperatureAuto| means auto
  // white balance mode.
  int SetColorTemperature(uint32_t color_temperature);

  // TODO(shik): Change the type of |device_path| to base::FilePath.

  // Whether the device supports updating frame rate.
  bool CanUpdateFrameRate();

  // Gets the frame rate which is set previously.
  float GetFrameRate();

  // Sets the frame rate to |frame_rate| for current device.
  int SetFrameRate(float frame_rate);

  // Return true if control |type| is supported otherwise return false.
  bool IsControlSupported(ControlType type);

  // Sets the |type|'s value to |value| for current device.
  // Return 0 if set successfully. Otherwise, return |-errno|.
  int SetControlValue(ControlType type, int32_t value);

  // Gets the |type|'s current value for current device.
  // To prevent ioctl overhead, this API only returned cached value if there is
  // one. The cached current value is updated in SetControlValue.
  // Return 0 if get successfully. Otherwise, return |-errno|.
  int GetControlValue(ControlType type, int32_t* value);

  // Get all supported formats of device by |device_path|. This function can be
  // called without calling Connect().
  static const SupportedFormats GetDeviceSupportedFormats(
      const std::string& device_path);

  // Get power frequency supported from device.
  static PowerLineFrequency GetPowerLineFrequency(
      const std::string& device_path);

  static bool IsAutoFocusSupported(const std::string& device_path);

  static bool IsConstantFrameRateSupported(const std::string& device_path);

  static bool IsCameraDevice(const std::string& device_path);

  // Get clock type in UVC driver to report the same time base in user space.
  static clockid_t GetUvcClock();

  // Get the model name from |device_path|.
  static std::string GetModelName(const std::string& device_path);

  // Return true if control |type| is supported otherwise return false.
  static bool IsControlSupported(const std::string& device_path,
                                 ControlType type);

  // Query control.
  // Return 0 if operation successfully. Otherwise, return |-errno|.
  // The control range value is stored in |range|.
  static int QueryControl(const std::string& device_path,
                          ControlType type,
                          ControlRange* range);

 private:
  static std::vector<float> GetFrameRateList(int fd,
                                             uint32_t fourcc,
                                             uint32_t width,
                                             uint32_t height);

  // Query the control of |type|.
  // Return 0 if operation successfully. Otherwise, return |-errno|.
  // The control range value is stored in |range|.
  static int QueryControl(int fd, ControlType type, ControlRange* range);

  // Return 0 if set control successfully. Otherwise, return |-errno|.
  static int SetControlValue(int fd, ControlType type, int32_t value);

  // Return 0 if get control successfully. Otherwise, return |-errno|.
  // The returned value is stored in |value|.
  static int GetControlValue(int fd, ControlType type, int32_t* value);

  // This is for suspend/resume feature. USB camera will be enumerated after
  // device resumed. But camera device may not be ready immediately.
  static int RetryDeviceOpen(const std::string& device_path, int flags);

  int QueryControl(ControlType type, ControlRange* range);

  // Set power frequency supported from device.
  int SetPowerLineFrequency(PowerLineFrequency setting);

  // Returns true if the current connected device is an external camera.
  bool IsExternalCamera();

  // The number of video buffers we want to request in kernel.
  const int kNumVideoBuffers = 4;

  // The opened device fd.
  base::ScopedFD device_fd_;

  // StreamOn state
  bool stream_on_;

  // AF state
  bool autofocus_on_;
  bool autofocus_supported_;

  bool white_balance_control_supported_;

  bool can_update_frame_rate_;
  float frame_rate_;

  // True if the buffer is used by client after GetNextFrameBuffer().
  std::vector<bool> buffers_at_client_;

  const DeviceInfo device_info_;

  // Current control values.
  std::map<ControlType, int32_t> control_values_;

  // Since V4L2CameraDevice may be called on different threads, this is used to
  // guard all variables.
  base::Lock lock_;

  DISALLOW_COPY_AND_ASSIGN(V4L2CameraDevice);
};

}  // namespace cros

#endif  // CAMERA_HAL_USB_V4L2_CAMERA_DEVICE_H_
