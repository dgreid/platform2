// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/libvda/gbm_util.h"

#include <fcntl.h>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

namespace arc {

uint32_t ConvertPixelFormatToGbmFormat(video_pixel_format_t format) {
  switch (format) {
    case YV12:
      return GBM_FORMAT_YVU420;
    case NV12:
      return GBM_FORMAT_NV12;
    default:
      return 0;
  }
}

std::vector<video_pixel_format_t> GetSupportedRawFormats(
    GbmUsageType usage_type) {
  // TODO(alexlau): don't hardcode this path, see
  // http://cs/chromeos_public/src/platform/minigbm/cros_gralloc/cros_gralloc_driver.cc?l=29&rcl=cc35e699f36cce0f0b3a130b0d6ce4e2a393b373
  base::ScopedFD fd(HANDLE_EINTR(open("/dev/dri/renderD128", O_RDWR)));
  if (!fd.is_valid()) {
    LOG(ERROR) << "Could not open vgem.";
    return {};
  }

  arc::ScopedGbmDevicePtr device(gbm_create_device(fd.get()));
  if (!device.get()) {
    LOG(ERROR) << "Could not create gbm device.";
    return {};
  }

  uint32_t usage_flags = GBM_BO_USE_TEXTURING;
  switch (usage_type) {
    case ENCODE:
      usage_flags |= GBM_BO_USE_HW_VIDEO_ENCODER;
      break;
    case DECODE:
      usage_flags |= GBM_BO_USE_HW_VIDEO_DECODER;
      break;
    default:
      NOTREACHED();
  }

  std::vector<video_pixel_format_t> formats;
  constexpr video_pixel_format_t pixel_formats[] = {NV12, YV12};
  for (video_pixel_format_t pixel_format : pixel_formats) {
    // VEA has NV12 hardcoded as the only allowed format on many devices,
    // only check NV12 for now.
    if (usage_type == ENCODE && pixel_format != NV12)
      continue;

    uint32_t gbm_format = ConvertPixelFormatToGbmFormat(pixel_format);
    if (gbm_format == 0u)
      continue;
    if (!gbm_device_is_format_supported(device.get(), gbm_format,
                                        usage_flags)) {
      DLOG(INFO) << "Not supported: " << pixel_format;
      continue;
    }
    formats.push_back(pixel_format);
  }
  return formats;
}

}  // namespace arc
