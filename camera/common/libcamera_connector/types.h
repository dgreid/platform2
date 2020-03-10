/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_LIBCAMERA_CONNECTOR_TYPES_H_
#define CAMERA_COMMON_LIBCAMERA_CONNECTOR_TYPES_H_

#include <base/callback.h>

namespace cros {

using IntOnceCallback = base::OnceCallback<void(int)>;

}  // namespace cros

#endif  // CAMERA_COMMON_LIBCAMERA_CONNECTOR_TYPES_H_
