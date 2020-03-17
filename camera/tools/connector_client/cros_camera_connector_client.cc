/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <cstring>
#include <string>
#include <sysexits.h>

#include <base/strings/stringprintf.h>

#include "cros-camera/common.h"
#include "tools/connector_client/cros_camera_connector_client.h"

namespace cros {

int OnGotCameraInfo(void* context,
                    const cros_cam_info_t* info,
                    unsigned is_removed) {
  auto GetDrmFormatName = [](uint32_t fourcc) {
    std::string result = "0000";
    for (size_t i = 0; i < 4; ++i, fourcc >>= 8) {
      const char c = static_cast<char>(fourcc & 0xFF);
      if (c <= 0x1f || c >= 0x7f) {
        return base::StringPrintf("0x%x", fourcc);
      }
      result[i] = c;
    }
    return result;
  };

  int32_t camera_id = *reinterpret_cast<int32_t*>(info->id);
  LOGF(INFO) << "Gotten camera info of " << camera_id
             << " (name = " << info->name
             << ", format_count = " << info->format_count << ")";
  for (unsigned i = 0; i < info->format_count; ++i) {
    LOGF(INFO) << "format = " << GetDrmFormatName(info->format_info[i].fourcc)
               << ", width = " << info->format_info[i].width
               << ", height = " << info->format_info[i].height
               << ", fps = " << info->format_info[i].fps;
  }
  return 0;
}

int CrosCameraConnectorClient::OnInit() {
  int res = brillo::Daemon::OnInit();
  if (res != EX_OK) {
    return res;
  }

  res = cros_cam_init();
  if (res != 0) {
    return EX_UNAVAILABLE;
  }

  res = cros_cam_get_cam_info(&OnGotCameraInfo, nullptr);
  if (res != 0) {
    return EX_UNAVAILABLE;
  }

  return EX_OK;
}

void CrosCameraConnectorClient::OnShutdown(int* exit_code) {
  cros_cam_exit();
}

}  // namespace cros

int main() {
  cros::CrosCameraConnectorClient connector_client;
  return connector_client.Run();
}
