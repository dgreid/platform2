/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <unistd.h>

#include "cros-camera/camera_service_connector.h"

int main() {
  int res = cros_cam_init();
  if (res != 0) {
    return res;
  }
  cros_cam_exit();
  return 0;
}
