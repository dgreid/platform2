/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_TOOLS_CONNECTOR_CLIENT_CROS_CAMERA_CONNECTOR_CLIENT_H_
#define CAMERA_TOOLS_CONNECTOR_CLIENT_CROS_CAMERA_CONNECTOR_CLIENT_H_

#include <brillo/daemons/daemon.h>

#include "cros-camera/camera_service_connector.h"

namespace cros {

int OnGotCameraInfo(void* context,
                    const cros_cam_info_t* info,
                    unsigned is_removed);

class CrosCameraConnectorClient : public brillo::Daemon {
 public:
  CrosCameraConnectorClient() = default;

  int OnInit() override;

  void OnShutdown(int* exit_code) override;
};

}  // namespace cros

#endif  // CAMERA_TOOLS_CONNECTOR_CLIENT_CROS_CAMERA_CONNECTOR_CLIENT_H_
