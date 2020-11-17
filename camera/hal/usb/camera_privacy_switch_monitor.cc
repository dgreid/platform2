/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb/camera_privacy_switch_monitor.h"

#include <utility>

#include "cros-camera/common.h"

namespace cros {

CameraPrivacySwitchMonitor::CameraPrivacySwitchMonitor()
    : state_(PrivacySwitchState::kUnknown) {
  VLOGF_ENTER();
}

CameraPrivacySwitchMonitor::~CameraPrivacySwitchMonitor() {
  VLOGF_ENTER();
}

void CameraPrivacySwitchMonitor::RegisterCallback(
    PrivacySwitchStateChangeCallback callback) {
  callback_ = std::move(callback);
}

void CameraPrivacySwitchMonitor::OnStatusChanged(PrivacySwitchState state) {
  if (state == state_) {
    return;
  }

  state_ = state;
  if (!callback_.is_null()) {
    callback_.Run(state);
  }
}

}  // namespace cros
