/*
 * Copyright (C) 2014-2017 Intel Corporation
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

#define LOG_TAG "ImgEncoderCore"

#include <cros-camera/jpeg_compressor.h>

#include "Camera3HAL.h"
#include "Camera3V4l2Format.h"
#include "CameraBuffer.h"
#include "ColorConverter.h"
#include "Exif.h"
#include "ImageScalerCore.h"
#include "ImgEncoderCore.h"
#include "LogHelper.h"
#include "PlatformData.h"
#include "Utils.h"


NAMESPACE_DECLARATION {
ImgEncoderCore::ImgEncoderCore() :
    mThumbOutBuf(nullptr),
    mJpegDataBuf(nullptr),
    mMainScaled(nullptr),
    mThumbScaled(nullptr),
    mJpegSetting(nullptr),
    mJpegCompressor(cros::JpegCompressor::GetInstance(g_mojo_manager))
{
    LOG1("@%s", __FUNCTION__);

    mInternalYU12Size =
        RESOLUTION_14MP_WIDTH * RESOLUTION_14MP_HEIGHT * 3 / 2;
    mInternalYU12.reset(new char[mInternalYU12Size]);
}

ImgEncoderCore::~ImgEncoderCore()
{
    LOG1("@%s", __FUNCTION__);
    deInit();
}

status_t ImgEncoderCore::init(void)
{
    LOG1("@%s", __FUNCTION__);

    mJpegSetting = new ExifMetaData::JpegSetting;

    return NO_ERROR;
}

void ImgEncoderCore::deInit(void)
{
    LOG2("@%s", __FUNCTION__);

    if (mJpegSetting) {
        delete mJpegSetting;
        mJpegSetting = nullptr;
    }

    mThumbOutBuf.reset();

    mJpegDataBuf.reset();
}

/**
 * thumbBufferDownScale
 * Downscaling the thumb buffer and allocate the scaled thumb input intermediate
 * buffer if scaling is needed.
 *
 * \param pkg [IN] EncodePackage from the caller
 */
void ImgEncoderCore::thumbBufferDownScale(EncodePackage & pkg)
{
    LOG2("%s", __FUNCTION__);
    int thumbwidth = mJpegSetting->thumbWidth;
    int thumbheight = mJpegSetting->thumbHeight;

    // Allocate thumbnail input buffer if downscaling is needed.
    if (thumbwidth != 0) {
        if (COMPARE_RESOLUTION(pkg.thumb, mThumbOutBuf) != 0) {
            LOG2("%s: Downscaling for thumbnail: %dx%d -> %dx%d", __FUNCTION__,
                pkg.thumb->width(), pkg.thumb->height(),
                mThumbOutBuf->width(), mThumbOutBuf->height());
            if (mThumbScaled
                && (COMPARE_RESOLUTION(mThumbScaled, mThumbOutBuf) != 0
                    || pkg.thumb->v4l2Fmt() != mThumbScaled->v4l2Fmt())) {
                mThumbScaled.reset();
            }
            if (!mThumbScaled) {
                // Using thumbwidth as stride for heap buffer
                mThumbScaled = CameraBuffer::allocateHeapBuffer(
                    thumbwidth, thumbheight, thumbwidth,
                    pkg.thumb->v4l2Fmt(), pkg.thumb->cameraId());
                if (!mThumbScaled) {
                    LOGE("Error in creating shared_ptr mThumbScaled");
                    return;
                }
            }
            ImageScalerCore::scaleFrame(pkg.thumb, mThumbScaled);
            pkg.thumb = mThumbScaled;
        }
    }
}

/**
 * mainBufferDownScale
 * Downscaling the main buffer and allocate the scaled main intermediate
 * buffer if scaling is needed.
 *
 * \param pkg [IN] EncodePackage from the caller
 */
void ImgEncoderCore::mainBufferDownScale(EncodePackage & pkg)
{
    LOG2("%s", __FUNCTION__);

    // Allocate buffer for main picture downscale
    // Compare the resolution, only do downscaling.
    if (COMPARE_RESOLUTION(pkg.main, pkg.jpegOut) == 1) {
        LOG2("%s: Downscaling for main picture: %dx%d -> %dx%d", __FUNCTION__,
            pkg.main->width(), pkg.main->height(),
            pkg.jpegOut->width(), pkg.jpegOut->height());
        if (mMainScaled
            && (COMPARE_RESOLUTION(mMainScaled, pkg.jpegOut) != 0
                || pkg.main->v4l2Fmt() != mMainScaled->v4l2Fmt())) {
            mMainScaled.reset();
        }
        if (!mMainScaled) {
            // Use pkg.jpegOut->width() as stride for the heap buffer
            mMainScaled = CameraBuffer::allocateHeapBuffer(
                pkg.jpegOut->width(), pkg.jpegOut->height(),
                pkg.jpegOut->width(), pkg.main->v4l2Fmt(), pkg.main->cameraId());
            if (!mMainScaled) {
                LOGE("Error in creating shared_ptr mMainScaled");
                return;
            }
        }
        ImageScalerCore::scaleFrame(pkg.main, mMainScaled);
        pkg.main = mMainScaled;
    }
}

/**
 * allocateBufferAndDownScale
 * This method downscales the main image and thumbnail if necesary. In case it
 * needs to scale, it allocates the intermediate buffers where the scaled version
 * is stored before it is given to the encoders. jpeg.thumbnailSize (0,0) means
 * JPEG EXIF will not contain a thumbnail. We use thumbwidth to determine if
 * Thumbnail size is > 0. In case thumbsize is not 0 we create the thumb output
 * buffer with size provided in the settings. If no thumb input buffer is
 * provided with the package the main buffer is assinged to the thumb input.
 * If thumb input buffer is provided in the package then only down scaling is needed
 *
 * \param pkg [IN] EncodePackage from the caller
 */
status_t ImgEncoderCore::allocateBufferAndDownScale(EncodePackage & pkg)
{
    LOG2("%s", __FUNCTION__);

    int thumbwidth = mJpegSetting->thumbWidth;
    int thumbheight = mJpegSetting->thumbHeight;

    // Check if client provided the encoded data buffer
    if (pkg.encodedData)
        mJpegDataBuf = pkg.encodedData;

    // Allocate buffer for main image jpeg output if in first time or resolution changed
    if (pkg.encodeAll && (!mJpegDataBuf ||
                COMPARE_RESOLUTION(mJpegDataBuf, pkg.jpegOut))) {
        if (mJpegDataBuf)
            mJpegDataBuf.reset();

        if (!mJpegDataBuf) {
            LOG1("Allocating jpeg data buffer with %dx%d, stride:%d", pkg.jpegOut->width(),
                    pkg.jpegOut->height(), pkg.jpegOut->stride());

            mJpegDataBuf = CameraBuffer::allocateHeapBuffer(
                pkg.jpegOut->width(), pkg.jpegOut->height(),
                pkg.jpegOut->width(), pkg.jpegOut->v4l2Fmt(),
                pkg.jpegOut->cameraId(), pkg.jpegOut->size());
            if (!mJpegDataBuf) {
                LOGE("Error in creating shared_ptr mJpegDataBuf");
                return NO_MEMORY;
            }
        }
    }

    // Check if client provided the thumb out data buffer
    if (pkg.thumbOut)
        mThumbOutBuf = pkg.thumbOut;

    // Allocate buffer for thumbnail output
    if (thumbwidth != 0) {
        if (!pkg.thumb)
            pkg.thumb = pkg.main;

        // Minimum buffer size required for high compression quality
        int minThumbBufSize = thumbwidth * thumbheight * 2;
        if (mThumbOutBuf && (mThumbOutBuf->width() != thumbwidth ||
                    mThumbOutBuf->height() != thumbheight ||
                    mThumbOutBuf->size() < minThumbBufSize))
            mThumbOutBuf.reset();

        if (!mThumbOutBuf) {
            LOG1("Allocating thumb data buffer with %dx%d", thumbwidth, thumbheight);

            if (!pkg.thumb) {
                LOGE("No source for thumb");
                return UNKNOWN_ERROR;
            }

            mThumbOutBuf = CameraBuffer::allocateHeapBuffer(
                thumbwidth, thumbheight,
                thumbwidth, pkg.thumb->v4l2Fmt(),
                pkg.thumb->cameraId(), minThumbBufSize);

            if (!mThumbOutBuf) {
                LOGE("Error in creating shared_ptr mThumbOutBuf");
                return NO_MEMORY;
            }
        }
    }

    thumbBufferDownScale(pkg);
    if (pkg.encodeAll)
        mainBufferDownScale(pkg);

    return NO_ERROR;
}


/**
 * getJpegSettings
 *
 * Get the JPEG settings needed for image encoding from the Exif
 * metadata and store to internal struct
 * \param settings [IN] EncodePackage from the caller
 * \param metaData [IN] exif metadata
 */
status_t ImgEncoderCore::getJpegSettings(EncodePackage & pkg, ExifMetaData& metaData)
{
    LOG2("@%s:", __FUNCTION__);
    UNUSED(pkg);
    status_t status = NO_ERROR;

    *mJpegSetting = metaData.mJpegSetting;
    LOG1("jpegQuality=%d,thumbQuality=%d,thumbW=%d,thumbH=%d,orientation=%d",
              mJpegSetting->jpegQuality,
              mJpegSetting->jpegThumbnailQuality,
              mJpegSetting->thumbWidth,
              mJpegSetting->thumbHeight,
              mJpegSetting->orientation);

    return status;
}

int ImgEncoderCore::doSwEncode(std::shared_ptr<CameraBuffer> srcBuf,
                               int quality,
                               std::shared_ptr<CameraBuffer> destBuf,
                               unsigned int destOffset)
{
    LOG2("@%s", __FUNCTION__);
    if (srcBuf->v4l2Fmt() != V4L2_PIX_FMT_NV12 &&
        srcBuf->v4l2Fmt() != V4L2_PIX_FMT_NV12M &&
        srcBuf->v4l2Fmt() != V4L2_PIX_FMT_NV21 &&
        srcBuf->v4l2Fmt() != V4L2_PIX_FMT_NV21M) {
        LOGE("%s Unsupported format %d", __FUNCTION__, srcBuf->v4l2Fmt());
        return 0;
    }

    if (NO_ERROR != srcBuf->lock()) {
        LOGE("%s Failed to lock buffer %d", __FUNCTION__, srcBuf->v4l2Fmt());
        return 0;
    }

    int width = srcBuf->width();
    int height = srcBuf->height();
    int stride = srcBuf->stride();
    void* srcY = srcBuf->data();
    void* srcUV = static_cast<unsigned char*>(srcBuf->data()) + stride * height;
    if (srcBuf->nonContiguousYandUV()) {
        srcUV = static_cast<unsigned char*>(srcBuf->dataUV());
    }

    if (width * height * 3 / 2 > mInternalYU12Size) {
        mInternalYU12Size = width * height * 3 / 2;
        mInternalYU12.reset(new char[mInternalYU12Size]);
    }
    void* tempBuf = mInternalYU12.get();

    if (srcBuf->v4l2Fmt() == V4L2_PIX_FMT_NV12 ||
        srcBuf->v4l2Fmt() == V4L2_PIX_FMT_NV12M) {
        NV12ToP411Separate(width, height, stride, srcY, srcUV, tempBuf);
    } else {
        // NV21 and NV21M
        NV21ToP411Separate(width, height, stride, srcY, srcUV, tempBuf);
    }

    uint32_t outSize = 0;
    nsecs_t startTime = systemTime();
    void* pDst = static_cast<unsigned char*>(destBuf->data()) + destOffset;
    // The quality HW JPEG encoder image is not good, so use SW encoder instead.
    // (b/129519949).
    bool ret = mJpegCompressor->CompressImage(tempBuf,
                                             width, height, quality,
                                             nullptr, 0,
                                             destBuf->size(), pDst,
                                             &outSize,
                                             cros::JpegCompressor::Mode::kSwOnly);
    LOG1("%s: encoding ret:%d, %dx%d need %" PRId64 "ms, jpeg size %u, quality %d)",
         __FUNCTION__, ret, destBuf->width(), destBuf->height(),
         (systemTime() - startTime) / 1000000, outSize, quality);
    CheckError(ret == false, 0, "@%s, mJpegCompressor->CompressImage() fails",
               __FUNCTION__);

    srcBuf->unlock();
    return outSize;
}

/**
 * encodeSync
 *
 * Do HW or SW encoding of the main buffer of the package
 * Also do SW encoding of the thumb buffer
 *
 * \param srcBuf [IN] The input buffer to encode
 * \param metaData [IN] exif metadata
 *
 */
status_t ImgEncoderCore::encodeSync(EncodePackage & package, ExifMetaData& metaData)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    status_t status = NO_ERROR;
    int mainSize = 0;
    int thumbSize = 0;

    std::lock_guard<std::mutex> l(mEncodeLock);

    if (package.encodeAll) {
        if (!package.main) {
            LOGE("Main buffer for JPEG encoding is nullptr");
            return UNKNOWN_ERROR;
        }

        if (!package.jpegOut) {
            LOGE("JPEG output buffer is nullptr");
            return UNKNOWN_ERROR;
        }
    }

    getJpegSettings(package, metaData);
    // Allocate buffers for thumbnail if not present and also
    // downscale the buffer for main if scaling is needed
    allocateBufferAndDownScale(package);

    // Encode thumbnail as JPEG in parallel with the HW encoding started earlier
    if (package.thumb && mThumbOutBuf) {
        do {
            LOG2("Encoding thumbnail with quality %d",
                 mJpegSetting->jpegThumbnailQuality);
            thumbSize = doSwEncode(package.thumb,
                                   mJpegSetting->jpegThumbnailQuality,
                                   mThumbOutBuf);
            mJpegSetting->jpegThumbnailQuality -= 5;
        } while (thumbSize > 0 && mJpegSetting->jpegThumbnailQuality > 0 &&
                thumbSize > THUMBNAIL_SIZE_LIMITATION);

        if (thumbSize > 0) {
            package.thumbOut = mThumbOutBuf;
            package.thumbSize = thumbSize;
        } else {
            // This is not critical, we can continue with main picture image
            LOGW("Could not encode thumbnail stream!");
        }
    } else {
        // No thumb is not critical, we can continue with main picture image
        LOG1("Exif created without thumbnail stream!");
    }

    if (package.encodeAll) {
        status = NO_ERROR;
        // Encode main picture with SW encoder
        mainSize = doSwEncode(package.main,
                              mJpegSetting->jpegQuality,
                              mJpegDataBuf);
        if (mainSize <= 0) {
           LOGE("Error while SW encoding JPEG");
           status = INVALID_OPERATION;
        }

        if (mainSize > 0) {
            package.encodedData = mJpegDataBuf;
            package.encodedDataSize = mainSize;
        }
    }

    return status;
}

} NAMESPACE_DECLARATION_END
