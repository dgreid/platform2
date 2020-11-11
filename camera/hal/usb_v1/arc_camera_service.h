/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_V1_ARC_CAMERA_SERVICE_H_
#define CAMERA_HAL_USB_V1_ARC_CAMERA_SERVICE_H_

#include <memory>
#include <string>

#include <base/threading/thread.h>
#include <base/threading/thread_checker.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/system/isolated_connection.h>

#include "hal/usb_v1/arc_camera.mojom.h"
#include "hal/usb_v1/camera_device_delegate.h"

namespace arc {

class ArcCameraServiceImpl : public ArcCameraService {
 public:
  explicit ArcCameraServiceImpl(base::Closure quit_cb);
  ArcCameraServiceImpl(const ArcCameraServiceImpl&) = delete;
  ArcCameraServiceImpl& operator=(const ArcCameraServiceImpl&) = delete;

  ~ArcCameraServiceImpl();

  // Create a mojo connection to container with a socket FD.
  bool StartWithSocketFD(base::ScopedFD socket_fd);

  // Create a mojo connection to container with a token and a FD.
  bool StartWithTokenAndFD(const std::string& token, base::ScopedFD fd);

 private:
  void OnChannelClosed(const std::string& error_msg);

  // ArcCameraService:
  void Connect(const std::string& device_path,
               ConnectCallback callback) override;
  void Disconnect(DisconnectCallback callback) override;
  void StreamOn(uint32_t width,
                uint32_t height,
                uint32_t pixel_format,
                float frame_rate,
                StreamOnCallback callback) override;
  void StreamOff(StreamOffCallback callback) override;
  void GetNextFrameBuffer(GetNextFrameBufferCallback callback) override;
  void ReuseFrameBuffer(uint32_t buffer_id,
                        ReuseFrameBufferCallback callback) override;
  void GetDeviceSupportedFormats(
      const std::string& device_path,
      GetDeviceSupportedFormatsCallback callback) override;
  void GetCameraDeviceInfos(GetCameraDeviceInfosCallback callback) override;

  // Quit callback to exit daemon.
  base::Closure quit_cb_;

  // Mojo endpoints.
  mojo::Binding<ArcCameraService> binding_;

  // Real camera device.
  std::unique_ptr<CameraDeviceDelegate> camera_device_;

  // Thread used in mojo to send and receive IPC messages.
  base::Thread ipc_thread_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  std::unique_ptr<mojo::IsolatedConnection> isolated_connection_;
};

}  // namespace arc

#endif  // CAMERA_HAL_USB_V1_ARC_CAMERA_SERVICE_H_
