/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_TOOLS_CONNECTOR_CLIENT_CROS_CAMERA_CONNECTOR_CLIENT_H_
#define CAMERA_TOOLS_CONNECTOR_CLIENT_CROS_CAMERA_CONNECTOR_CLIENT_H_

#include <list>
#include <map>
#include <vector>

#include <base/threading/thread.h>
#include <brillo/daemons/daemon.h>

#include "cros-camera/camera_service_connector.h"

namespace cros {

int OnGotCameraInfo(void* context,
                    const cros_cam_info_t* info,
                    unsigned is_removed);

int OnFrameAvailable(void* context, const cros_cam_frame_t* frame);

class CrosCameraConnectorClient : public brillo::Daemon {
 public:
  CrosCameraConnectorClient();

  int OnInit() override;

  void OnShutdown(int* exit_code) override;

  void SetCamInfo(const cros_cam_info_t* info);

  void ProcessFrame(const cros_cam_frame_t* frame);

  void RestartCapture();

 private:
  void StartCaptureOnThread();

  void StopCaptureOnThread();

  void RestartCaptureOnThread();

  std::list<cros_cam_device_t> camera_device_list_;
  std::map<cros_cam_device_t, std::vector<cros_cam_format_info_t>>
      format_info_map_;

  std::list<cros_cam_device_t>::iterator request_device_iter_;
  std::vector<cros_cam_format_info_t>::iterator request_format_iter_;

  base::Thread capture_thread_;
  int num_restarts_;
};

}  // namespace cros

#endif  // CAMERA_TOOLS_CONNECTOR_CLIENT_CROS_CAMERA_CONNECTOR_CLIENT_H_
