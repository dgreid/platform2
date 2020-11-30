/*
 * Copyright (C) 2019-2020 Intel Corporation
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

#define LOG_TAG "HalV3Utils"
#include <linux/videodev2.h>
#include <math.h>

#include <memory>
#include <string>

#include <base/strings/stringprintf.h>
#include <chromeos-config/libcros_config/cros_config.h>

#include "Errors.h"
#include "HALv3Utils.h"
#include "PlatformData.h"
#include "Utils.h"
#include "MetadataConvert.h"

namespace camera3 {
namespace HalV3Utils {

static const char* Camera3StreamTypes[] = {"OUTPUT",         // CAMERA3_STREAM_OUTPUT
                                           "INPUT",          // CAMERA3_STREAM_INPUT
                                           "BIDIRECTIONAL",  // CAMERA3_STREAM_BIDIRECTIONAL
                                           "INVALID"};

const char* getCamera3StreamType(int type) {
    int num = sizeof(Camera3StreamTypes) / sizeof(Camera3StreamTypes[0]);
    return (type >= 0 && type < num) ? Camera3StreamTypes[type] : Camera3StreamTypes[num - 1];
}

int HALFormatToV4l2Format(int cameraId, int halFormat, int usage) {
    LOG1("@%s", __func__);

    int format = -1;
    switch (halFormat) {
        case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
            if (IS_ZSL_USAGE(usage)) {
                format = icamera::PlatformData::getISysRawFormat(cameraId);
            } else {
                format = V4L2_PIX_FMT_NV12;
            }
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
        case HAL_PIXEL_FORMAT_BLOB:
            format = V4L2_PIX_FMT_NV12;
            break;
        case HAL_PIXEL_FORMAT_RAW_OPAQUE:
            format = icamera::PlatformData::getISysRawFormat(cameraId);
            break;
        default:
            LOGW("unsupport format %d", halFormat);
            break;
    }

    return format;
}

int getRotationDegrees(const camera3_stream_t& stream) {
    LOG1("@%s", __func__);
    if (stream.stream_type != CAMERA3_STREAM_OUTPUT) {
        LOG2("%s, no need rotation for stream type %d", __func__, stream.stream_type);
        return 0;
    }
    switch (stream.crop_rotate_scale_degrees) {
        case CAMERA3_STREAM_ROTATION_0:
            return 0;
        case CAMERA3_STREAM_ROTATION_90:
            return 90;
        case CAMERA3_STREAM_ROTATION_270:
            return 270;
        default:
            LOGE("unsupport rotate degree: %d, the value must be (0,1,3)",
                 stream.crop_rotate_scale_degrees);
            return -1;
    }
}

bool isSameRatioWithSensor(const icamera::stream_t *stream, int cameraId) {
    const icamera::CameraMetadata* meta = StaticCapability::getInstance(cameraId)->getCapability();

    float sensorRatio = 0.0f;
    icamera_metadata_ro_entry entry = meta->find(CAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE);
    if (entry.count == 2) {
        sensorRatio = static_cast<float>(entry.data.i32[0]) / entry.data.i32[1];
    }

    LOG2("%s, the sensor output sensorRatio: %f", __func__, sensorRatio);
    // invalid sensor output ratio, ignore this condition
    if (sensorRatio == 0.0) return true;

    // The pixel array size may be larger than biggest output size, set the
    // default tolerance to 0.1
    const float RATIO_TOLERANCE = 0.1f;
    const float streamRatio = static_cast<float>(stream->width) / stream->height;
    if (fabs(sensorRatio - streamRatio) < RATIO_TOLERANCE) return true;

    return false;
}

int fillHALStreams(int cameraId, const camera3_stream_t& camera3Stream, icamera::stream_t* stream) {
    LOG1("@%s, cameraId:%d", __func__, cameraId);

    stream->format = HALFormatToV4l2Format(cameraId, camera3Stream.format, camera3Stream.usage);
    CheckError(stream->format == -1, icamera::BAD_VALUE, "unsupported format %x",
               camera3Stream.format);

    // For rotation cases, aal needs to get the psl output mapping to user requirement.
    if (getRotationDegrees(camera3Stream) > 0) {
        icamera::camera_resolution_t* psl = icamera::PlatformData::getPslOutputForRotation(
            camera3Stream.width, camera3Stream.height, cameraId);

        stream->width = psl ? psl->width : camera3Stream.height;
        stream->height = psl ? psl->height : camera3Stream.width;
        LOG1("%s, Use the psl output %dx%d to map user requirement: %dx%d", __func__, stream->width,
             stream->height, camera3Stream.width, camera3Stream.height);
    } else {
        stream->width = camera3Stream.width;
        stream->height = camera3Stream.height;
    }

    stream->field = 0;
    stream->stride = icamera::CameraUtils::getStride(stream->format, stream->width);
    stream->size =
        icamera::CameraUtils::getFrameSize(stream->format, stream->width, stream->height);
    stream->memType = V4L2_MEMORY_USERPTR;
    stream->streamType = icamera::CAMERA_STREAM_OUTPUT;
    // CAMERA_STREAM_PREVIEW is for user preview stream
    // CAMERA_STREAM_VIDEO_CAPTURE is for other yuv stream
    if (camera3Stream.usage & (GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_COMPOSER)) {
        stream->usage = icamera::CAMERA_STREAM_PREVIEW;
    } else if (IS_ZSL_USAGE(camera3Stream.usage)) {
        stream->usage = icamera::CAMERA_STREAM_OPAQUE_RAW;
    } else {
        int size = stream->width * stream->height;
        if (camera3Stream.format == HAL_PIXEL_FORMAT_BLOB) {
            // When enable GPU tnr, use video pipe to output BLOB stream for small resolutions
            if (size <= RESOLUTION_1080P_WIDTH * RESOLUTION_1080P_HEIGHT &&
                icamera::PlatformData::isGpuTnrEnabled()) {
                stream->usage = icamera::CAMERA_STREAM_VIDEO_CAPTURE;
            } else {
                stream->usage = icamera::CAMERA_STREAM_STILL_CAPTURE;
            }
        } else if (camera3Stream.format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
            // Check if it is YUV capture
            stream->usage = (size > RESOLUTION_1080P_WIDTH * RESOLUTION_1080P_HEIGHT)
                                ? icamera::CAMERA_STREAM_STILL_CAPTURE
                                : icamera::CAMERA_STREAM_VIDEO_CAPTURE;
        } else {
            stream->usage = icamera::CAMERA_STREAM_VIDEO_CAPTURE;
        }
    }

    LOG2("@%s, stream: width:%d, height:%d, usage %d", __func__, stream->width, stream->height,
         stream->usage);
    return icamera::OK;
}

// Only when /camera/devices exists in CrOS config on the board, it returns the
// real camera number, otherwise it returns 0.
int getCrosConfigCameraNumber() {
    int cameraNumber = 0;
    brillo::CrosConfig crosConfig;
    bool status = crosConfig.Init();
    CheckWarning(!status, -1, "@%s, Failed to initialize CrOS config", __func__);

    // Get MIPI camera count from "devices" array in Chrome OS config. The structure looks like:
    //     camera - devices + 0 + interface (mipi, usb)
    //                      |   + facing (front, back)
    //                      |   + orientation (0, 90, 180, 270)
    //                      |   ...
    //                      + 1 + interface
    //                          ...
    for (int i = 0;; ++i) {
        std::string interface;
        if (!crosConfig.GetString(base::StringPrintf("/camera/devices/%i", i), "interface",
                                  &interface)) {
            break;
        }
        if (interface == "mipi") {
            ++cameraNumber;
        }
    }
    return cameraNumber;
}
}  // namespace HalV3Utils
}  // namespace camera3
