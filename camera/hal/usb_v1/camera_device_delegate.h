/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_V1_CAMERA_DEVICE_DELEGATE_H_
#define CAMERA_HAL_USB_V1_CAMERA_DEVICE_DELEGATE_H_

#include <string>
#include <vector>

#include <base/macros.h>

#include "hal/usb_v1/common_types.h"

namespace arc {

// CameraDeviceDelegate should match the one in Android camera HAL.
class CameraDeviceDelegate {
 public:
  CameraDeviceDelegate() {}
  CameraDeviceDelegate(const CameraDeviceDelegate&) = delete;
  CameraDeviceDelegate& operator=(const CameraDeviceDelegate&) = delete;

  virtual ~CameraDeviceDelegate() {}

  // Connect camera device with |device_path|. Return 0 if device is opened
  // successfully. Otherwise, return -|errno|.
  virtual int Connect(const std::string& device_path) = 0;

  // Disconnect camera device. This function is a no-op if the camera device
  // is not connected. If the stream is on, this function will also stop the
  // stream.
  virtual void Disconnect() = 0;

  // Enable camera device stream. Setup captured frame with |width|x|height|
  // resolution, |pixel_format|, and |frame_rate|. Get frame buffer file
  // descriptors |fds| and |buffer_size|. |buffer_size| is the size allocated
  // for each buffer. The ownership of |fds| are transferred to the caller and
  // |fds| should be closed when done. Caller can memory map |fds| and should
  // unmap when done. Return 0 if device supports the format. Otherwise, return
  // -|errno|. This function should be called after Connect().
  virtual int StreamOn(uint32_t width,
                       uint32_t height,
                       uint32_t pixel_format,
                       float frame_rate,
                       std::vector<int>* fds,
                       uint32_t* buffer_size) = 0;

  // Disable camera device stream. Return 0 if device disables stream
  // successfully. Otherwise, return -|errno|. This function is a no-op if the
  // stream is already stopped.
  virtual int StreamOff() = 0;

  // Get next frame buffer from device. Device returns the corresponding
  // buffer with |buffer_id| and |data_size| bytes. |data_size| is how many
  // bytes used in the buffer for this frame. Return 0 if device gets the
  // buffer successfully. Otherwise, return -|errno|. Return -EAGAIN immediately
  // if next frame buffer is not ready. This function should be called after
  // StreamOn().
  virtual int GetNextFrameBuffer(uint32_t* buffer_id, uint32_t* data_size) = 0;

  // Return |buffer_id| buffer to device. Return 0 if the buffer is returned
  // successfully. Otherwise, return -|errno|. This function should be called
  // after StreamOn().
  virtual int ReuseFrameBuffer(uint32_t buffer_id) = 0;

  // Get all supported formats of device by |device_path|. This function can be
  // called without calling Connect().
  virtual const SupportedFormats GetDeviceSupportedFormats(
      const std::string& device_path) = 0;

  // Get all camera devices information. This function can be called without
  // calling Connect().
  virtual const DeviceInfos GetCameraDeviceInfos() = 0;
};

}  // namespace arc

#endif  // CAMERA_HAL_USB_V1_CAMERA_DEVICE_DELEGATE_H_
