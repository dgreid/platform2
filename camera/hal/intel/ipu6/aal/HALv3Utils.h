/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <hardware/camera3.h>

#include "Parameters.h"
#include "iutils/CameraLog.h"

namespace camera3 {

#define IS_ZSL_USAGE(usage) (((usage)&GRALLOC_USAGE_HW_CAMERA_ZSL) == GRALLOC_USAGE_HW_CAMERA_ZSL)

namespace HalV3Utils {
const char* getCamera3StreamType(int type);
int HALFormatToV4l2Format(int cameraId, int halFormat, int usage);
int getRotationDegrees(const camera3_stream_t& stream);
bool isSameRatioWithSensor(const icamera::stream_t *stream, int cameraId);
int fillHALStreams(int cameraId, const camera3_stream_t& camera3Stream, icamera::stream_t* stream);
int getCrosConfigCameraNumber();
}  // namespace HalV3Utils

}  // namespace camera3
