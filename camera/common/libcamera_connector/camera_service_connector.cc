/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>

#include "common/libcamera_connector/camera_service_connector_impl.h"
#include "cros-camera/camera_service_connector.h"
#include "cros-camera/future.h"

int cros_cam_init() {
  auto* connector = cros::CameraServiceConnector::GetInstance();
  auto future = cros::Future<int>::Create(nullptr);
  connector->Init(cros::GetFutureCallback(future));
  return future->Get();
}

void cros_cam_exit() {
  auto* connector = cros::CameraServiceConnector::GetInstance();
  connector->Exit();
}

int cros_cam_get_cam_info(cros_cam_get_cam_info_cb_t callback, void* context) {
  // TODO(b/151047930): Implement the function.
  return -ENODEV;
}

int cros_cam_start_capture(cros_cam_device_t id,
                           const cros_cam_format_info_t* format,
                           cros_cam_capture_cb_t callback,
                           void* context) {
  // TODO(b/151047930): Implement the function.
  return -ENODEV;
}

void cros_cam_stop_capture(cros_cam_device_t id) {
  // TODO(b/151047930): Implement the function.
}
