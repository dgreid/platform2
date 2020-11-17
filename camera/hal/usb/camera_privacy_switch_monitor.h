/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_CAMERA_PRIVACY_SWITCH_MONITOR_H_
#define CAMERA_HAL_USB_CAMERA_PRIVACY_SWITCH_MONITOR_H_

#include <memory>
#include <vector>

#include <base/callback.h>

#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/cros_camera_hal.h"

namespace cros {

// CameraPrivacySwitchMonitor is a monitor for the status change of camera
// privacy switch.
class CameraPrivacySwitchMonitor final {
 public:
  CameraPrivacySwitchMonitor();
  CameraPrivacySwitchMonitor(const CameraPrivacySwitchMonitor&) = delete;
  CameraPrivacySwitchMonitor& operator=(const CameraPrivacySwitchMonitor&) =
      delete;
  ~CameraPrivacySwitchMonitor();

  void RegisterCallback(PrivacySwitchStateChangeCallback callback);

  void OnStatusChanged(PrivacySwitchState state);

 private:
  PrivacySwitchState state_;

  PrivacySwitchStateChangeCallback callback_;
};

}  // namespace cros

#endif  // CAMERA_HAL_USB_CAMERA_PRIVACY_SWITCH_MONITOR_H_
