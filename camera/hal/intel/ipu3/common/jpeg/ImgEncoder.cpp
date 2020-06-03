/*
 * Copyright (C) 2014-2020 Intel Corporation
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

#define LOG_TAG "ImgEncoder"

#include <linux/videodev2.h>
#include <mutex>

#include "Camera3HAL.h"
#include "ImgEncoder.h"
#include "LogHelper.h"
#include "Utils.h"

namespace cros {
namespace intel {

ImgEncoder::ImgEncoder(int cameraid) :
    mJpegCompressor(cros::JpegCompressor::GetInstance(g_mojo_manager))
{
    LOG1("@%s", __FUNCTION__);
}

ImgEncoder::~ImgEncoder()
{
    LOG1("@%s", __FUNCTION__);
}

/**
 * encodeSync
 *
 * Do HW / SW JPEG encoding for the main buffer
 * Do SW JPEG encoding for the thumbnail buffer
 *
 * \param package [IN] Information that should be encoded
 */
bool ImgEncoder::encodeSync(EncodePackage& package)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

    bool sameSize = package.input->width() == package.output->width() &&
                    package.input->height() == package.output->height();
    bool sameSizeButRotate = package.input->width() == package.output->height() &&
                             package.input->height() == package.output->width();
    CheckError(!sameSize && !sameSizeButRotate,
               0, "@%s, ImgEncoder::encodeSync failed: input size != output size, %d, %d, %d, %d",
               __FUNCTION__, package.input->width(), package.input->height(),
               package.output->width(), package.output->height());

    std::lock_guard<std::mutex> l(mEncodeLock);
    auto srcBuf = package.input;
    auto dstBuf = package.output;
    nsecs_t startTime = systemTime();

    bool ret;
    if (srcBuf->type() == CameraBuffer::BufferType::BUF_TYPE_HANDLE &&
        dstBuf->type() == CameraBuffer::BufferType::BUF_TYPE_HANDLE) {
        ret = mJpegCompressor->CompressImageFromHandle(
            srcBuf->getBufferHandle(), dstBuf->getBufferHandle(), srcBuf->width(), srcBuf->height(),
            package.quality, package.exifData, package.exifDataSize, &package.encodedDataSize);
    } else if (srcBuf->type() == CameraBuffer::BufferType::BUF_TYPE_MALLOC &&
               dstBuf->type() == CameraBuffer::BufferType::BUF_TYPE_MALLOC) {
        ret = mJpegCompressor->CompressImageFromMemory(
            srcBuf->data(), V4L2_PIX_FMT_NV12, dstBuf->data(), dstBuf->size(), srcBuf->width(),
            srcBuf->height(), package.quality, package.exifData, package.exifDataSize,
            &package.encodedDataSize);
    } else {
      LOGE("%s: encode input type and output type does not match", __FUNCTION__);
      return false;
    }
    LOGE("%s: encoding ret:%d, %dx%d need %" PRId64 "ms, jpeg size %u, quality %d)", __FUNCTION__,
         ret, dstBuf->width(), dstBuf->height(), (systemTime() - startTime) / 1000000,
         package.encodedDataSize, package.quality);
    return ret && package.encodedDataSize > 0;
}

} /* namespace intel */
} /* namespace cros */
