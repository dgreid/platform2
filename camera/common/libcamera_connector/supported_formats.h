/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_LIBCAMERA_CONNECTOR_SUPPORTED_FORMATS_H_
#define CAMERA_COMMON_LIBCAMERA_CONNECTOR_SUPPORTED_FORMATS_H_

#include <utility>
#include <vector>

#include <drm_fourcc.h>
#include <hardware/gralloc.h>

namespace cros {

constexpr std::pair<int, uint32_t> kSupportedFormats[] = {
    {HAL_PIXEL_FORMAT_BLOB, DRM_FORMAT_R8}};

// Resolves the given HAL pixel format to its corresponding DRM format. Returns
// 0 if a HAL Pixel format cannot be resolved.
uint32_t ResolveDrmFormat(int hal_pixel_format);

// Gets the corresponding HAL pixel format with the given DRM format. Returns 0
// if no format is found.
int GetHalPixelFormat(uint32_t drm_format);

}  // namespace cros

#endif  // CAMERA_COMMON_LIBCAMERA_CONNECTOR_SUPPORTED_FORMATS_H_
