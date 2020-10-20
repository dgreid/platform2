/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_UTILS_COMMON_TYPES_H_
#define CAMERA_COMMON_UTILS_COMMON_TYPES_H_

#include <map>
#include <memory>

#include <hardware/camera3.h>

#include "common/camera_buffer_handle.h"
#include "mojo/cros_camera_service.mojom.h"

namespace cros {
namespace internal {

// Common data types for hal_adapter internal use.

struct CameraMetadataDeleter {
  inline void operator()(camera_metadata_t* metadata) const {
    free_camera_metadata(metadata);
  }
};

using ScopedCameraMetadata =
    std::unique_ptr<camera_metadata_t, CameraMetadataDeleter>;

using ScopedStreams = std::map<uint64_t, std::unique_ptr<camera3_stream_t>>;

}  // namespace internal
}  // namespace cros

#endif  // CAMERA_COMMON_UTILS_COMMON_TYPES_H_
