/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_IP_METADATA_HANDLER_H_
#define CAMERA_HAL_IP_METADATA_HANDLER_H_

#include <base/macros.h>
#include <string>

#include <camera/camera_metadata.h>

namespace cros {

class MetadataHandler {
 public:
  MetadataHandler();
  ~MetadataHandler();

  // The caller is responsible for freeing the memory returned
  static android::CameraMetadata CreateStaticMetadata(const std::string& ip,
                                                      const std::string& name,
                                                      int format,
                                                      int width,
                                                      int height,
                                                      double fps);

  static camera_metadata_t* GetDefaultRequestSettings();

 private:
  DISALLOW_COPY_AND_ASSIGN(MetadataHandler);
};

}  // namespace cros

#endif  // CAMERA_HAL_IP_METADATA_HANDLER_H_
