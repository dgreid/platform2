/*
 * Copyright (C) 2012-2017 Intel Corporation
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
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

#define LOG_TAG "ImageScalerCore"

#include "LogHelper.h"
#include "ImageScalerCore.h"
#include "CameraBuffer.h"
#include <libyuv.h>

NAMESPACE_DECLARATION {

status_t ImageScalerCore::scaleFrame(std::shared_ptr<CameraBuffer> input,
                                     std::shared_ptr<CameraBuffer> output)
{
    if (input->v4l2Fmt() != V4L2_PIX_FMT_NV12 &&
        input->v4l2Fmt() != V4L2_PIX_FMT_NV12M &&
        input->v4l2Fmt() != V4L2_PIX_FMT_NV21 &&
        input->v4l2Fmt() != V4L2_PIX_FMT_NV21M) {
        LOGE("%s Unsupported format %d", __FUNCTION__, input->v4l2Fmt());
        return BAD_VALUE;
    }

    // Y plane
    libyuv::ScalePlane((uint8_t*)input->data(),
                input->stride(),
                input->width(),
                input->height(),
                (uint8_t*)output->data(),
                output->stride(),
                output->width(),
                output->height(),
                libyuv::kFilterNone);

    // UV plane
    int inUVOffsetByte = input->stride() * input->height();
    const uint16_t* inBufferUV = (uint16_t*)input->data() + inUVOffsetByte / 2;
    if (input->nonContiguousYandUV()) {
        inBufferUV = (uint16_t*)input->dataUV();
    }

    int outUVOffsetByte = output->stride() * output->height();
    uint16_t* outBufferUV = (uint16_t*)output->data() + outUVOffsetByte / 2;
    if (output->nonContiguousYandUV()) {
        outBufferUV = (uint16_t*)output->dataUV();
    }

    libyuv::ScalePlane_16(inBufferUV,
                input->stride() / 2,
                input->width() / 2,
                input->height() / 2,
                outBufferUV,
                output->stride() / 2,
                output->width() / 2,
                output->height() / 2,
                libyuv::kFilterNone);
    return OK;
}

status_t ImageScalerCore::cropRotateScaleFrame(std::shared_ptr<CameraBuffer> input,
                                               std::shared_ptr<CameraBuffer> output,
                                               int angle,
                                               std::vector<uint8_t>& tempRotationBuffer,
                                               std::vector<uint8_t>& tempScaleBuffer)

{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);

    // Check the output buffer resolution with device config resolution
    CheckAndLogError(
        (output->width() != input->width() ||
         output->height() != input->height()),
        UNKNOWN_ERROR, "output resolution mis-match [%d x %d] -> [%d x %d]",
        input->width(), input->height(), output->width(), output->height());

    if (input->v4l2Fmt() != V4L2_PIX_FMT_NV12 &&
        input->v4l2Fmt() != V4L2_PIX_FMT_NV12M &&
        input->v4l2Fmt() != V4L2_PIX_FMT_NV21 &&
        input->v4l2Fmt() != V4L2_PIX_FMT_NV21M) {
        LOGE("%s Unsupported format %d", __FUNCTION__, input->v4l2Fmt());
        return BAD_VALUE;
    }

    int width = input->width();
    int height = input->height();
    int inStride = input->stride();
    int cropped_width = height * height / width;
    if (cropped_width % 2 == 1) {
      // Make cropped_width to the closest even number.
      cropped_width++;
    }
    int cropped_height = height;
    int crop_x = (width - cropped_width) / 2;
    int crop_y = 0;

    int rotated_height = cropped_width;
    int rotated_width = cropped_height;

    libyuv::RotationMode rotation_mode = libyuv::RotationMode::kRotate90;
    switch (angle) {
      case 90:
        rotation_mode = libyuv::RotationMode::kRotate90;
        break;
      case 270:
        rotation_mode = libyuv::RotationMode::kRotate270;
        break;
      default:
        LOGE("error rotation degree %d", angle);
        return -EINVAL;
    }
    // This libyuv method first crops the frame and then rotates it 90 degrees
    // clockwise or counterclockwise.
    if (tempRotationBuffer.size() < input->size()) {
        tempRotationBuffer.resize(input->size());
    }

    uint8_t* I420RotateBuffer = tempRotationBuffer.data();

    const uint8_t* inBufferY = (uint8_t*)(input->data());
    const uint8_t* inBufferUV = inBufferY + input->stride() * input->height();

    if (input->nonContiguousYandUV()) {
        inBufferUV = (uint8_t*)(input->dataUV());
    }

    const uint8_t* src = inBufferY + (inStride * crop_y + crop_x);
    const uint8_t* src_uv = inBufferUV + ((crop_y / 2) * inStride) + ((crop_x / 2) * 2);

    int ret = NV12ToI420Rotate(
            src, inStride, src_uv, inStride,
            I420RotateBuffer, rotated_width,
            I420RotateBuffer + rotated_width * rotated_height, rotated_width / 2,
            I420RotateBuffer + rotated_width * rotated_height * 5 / 4, rotated_width / 2,
            cropped_width, cropped_height, rotation_mode);

    if (ret) {
      LOGE("ConvertToI420 failed: %d", ret);
      return ret;
    }

    if (tempScaleBuffer.size() < input->size()) {
        tempScaleBuffer.resize(input->size());
    }

    uint8_t* I420ScaleBuffer = tempScaleBuffer.data();
    ret = libyuv::I420Scale(
        I420RotateBuffer, rotated_width,
        I420RotateBuffer + rotated_width * rotated_height, rotated_width / 2,
        I420RotateBuffer + rotated_width * rotated_height * 5 / 4, rotated_width / 2,
        rotated_width, rotated_height,
        I420ScaleBuffer, width,
        I420ScaleBuffer + width * height, width / 2,
        I420ScaleBuffer + width * height * 5 / 4, width / 2,
        width, height,
        libyuv::FilterMode::kFilterNone);
    if (ret) {
      LOGE("I420Scale failed: %d", ret);
    }

    int outStride = output->stride();
    uint8_t* outBuffer = (uint8_t*)(output->data());
    uint8_t* outBufferUV = outBuffer + outStride * height;
    if (output->nonContiguousYandUV()) {
        outBufferUV = (uint8_t*)(output->dataUV());
    }
    ret = libyuv::I420ToNV12(I420ScaleBuffer, width,
                             I420ScaleBuffer + width * height, width / 2,
                             I420ScaleBuffer + width * height * 5 / 4, width / 2,
                             outBuffer, outStride,
                             outBufferUV, outStride,
                             width, height);
    return ret;
}


} NAMESPACE_DECLARATION_END

