/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>

#include "cros-camera/camera_service_connector.h"

int cros_cam_init() {
  // TODO(b/151047930): Implement the function.
  return -EACCES;
}

void cros_cam_exit() {
  // TODO(b/151047930): Implement the function.
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
