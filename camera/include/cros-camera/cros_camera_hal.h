/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_CROS_CAMERA_HAL_H_
#define CAMERA_INCLUDE_CROS_CAMERA_CROS_CAMERA_HAL_H_

#define CROS_CAMERA_HAL_INFO_SYM CCHI
#define CROS_CAMERA_HAL_INFO_SYM_AS_STR "CCHI"

#include <base/callback.h>

#include "cros-camera/camera_mojo_channel_manager_token.h"

namespace cros {

enum class PrivacySwitchState {
  kUnknown,
  kOn,
  kOff,
};

using PrivacySwitchStateChangeCallback =
    base::RepeatingCallback<void(PrivacySwitchState state)>;

typedef struct cros_camera_hal {
  /**
   * Sets up the camera HAL. The |token| can be used for communication through
   * Mojo.
   */
  void (*set_up)(CameraMojoChannelManagerToken* token);

  /**
   * Tears down the camera HAL.
   */
  void (*tear_down)();

  /**
   * Registers camera privacy switch observer.
   */
  void (*set_privacy_switch_callback)(
      PrivacySwitchStateChangeCallback callback);

  /* reserved for future use */
  void* reserved[4];
} cros_camera_hal_t;

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_CROS_CAMERA_HAL_H_
