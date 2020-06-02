/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_CROS_CAMERA_HAL_H_
#define CAMERA_INCLUDE_CROS_CAMERA_CROS_CAMERA_HAL_H_

#define CROS_CAMERA_HAL_INFO_SYM CCHI
#define CROS_CAMERA_HAL_INFO_SYM_AS_STR "CCHI"

#include "cros-camera/camera_mojo_channel_manager.h"

namespace cros {

typedef struct cros_camera_hal {
  /**
   * Sets up the camera HAL. The |mojo_manager| can be used for communication
   * through Mojo.
   */
  void (*set_up)(CameraMojoChannelManager* mojo_manager);

  /**
   * Tears down the camera HAL.
   */
  void (*tear_down)();

  /* reserved for future use */
  void* reserved[5];
} cros_camera_hal_t;

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_CROS_CAMERA_HAL_H_
