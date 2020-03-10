/*
 * Copyright (C) 2019 MediaTek Inc.
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

#define LOG_TAG "mtkcam-CaptureStreamUpdaterPolicy"

#include <mtkcam/pipeline/policy/ICaptureStreamUpdaterPolicy.h>

#include <algorithm>
#include <cmath>

#include <mtkcam/pipeline/utils/streaminfo/ImageStreamInfo.h>
#include <mtkcam/utils/metastore/IMetadataProvider.h>

#include <property_lib.h>
#include <compiler.h>
#include <memory>

#include "MyUtils.h"

/******************************************************************************
 *
 ******************************************************************************/
#define ASPECT_TOLERANCE 0.03

/******************************************************************************
 *
 ******************************************************************************/
namespace NSCam {
namespace v3 {
namespace pipeline {
namespace policy {

static auto createImageStreamInfo(char const* streamName,
                                  StreamId_T streamId,
                                  MUINT32 streamType,
                                  size_t maxBufNum,
                                  size_t minInitBufNum,
                                  MUINT usageForAllocator,
                                  MINT imgFormat,
                                  MSize const& imgSize,
                                  MUINT32 transform)
    -> std::shared_ptr<IImageStreamInfo> {
  IImageStreamInfo::BufPlanes_t bufPlanes;
#define addBufPlane(planes, height, stride)                             \
  do {                                                                  \
    size_t _height = (size_t)(height);                                  \
    size_t _stride = (size_t)(stride);                                  \
    IImageStreamInfo::BufPlane bufPlane = {_height * _stride, _stride}; \
    planes.push_back(bufPlane);                                         \
  } while (0)
  switch (imgFormat) {
    case eImgFmt_YV12:
      addBufPlane(bufPlanes, imgSize.h, imgSize.w);
      addBufPlane(bufPlanes, imgSize.h >> 1, imgSize.w >> 1);
      addBufPlane(bufPlanes, imgSize.h >> 1, imgSize.w >> 1);
      break;
    case eImgFmt_NV21:
    case eImgFmt_NV12:
      addBufPlane(bufPlanes, imgSize.h, imgSize.w);
      addBufPlane(bufPlanes, imgSize.h >> 1, imgSize.w);
      break;
    case eImgFmt_YUY2:
      addBufPlane(bufPlanes, imgSize.h, imgSize.w << 1);
      break;
    default:
      MY_LOGE("format not support yet %d", imgFormat);
      break;
  }
#undef addBufPlane

  auto pStreamInfo = std::make_shared<NSCam::v3::Utils::ImageStreamInfo>(
      streamName, streamId, streamType, maxBufNum, minInitBufNum,
      usageForAllocator, imgFormat, imgSize, bufPlanes, transform);

  if (pStreamInfo == nullptr) {
    MY_LOGE("create ImageStream failed, %s, %#" PRIx64, streamName, streamId);
  }

  return pStreamInfo;
}

static uint32_t getJpegRotation(IMetadata const* pMetadata) {
  IMetadata::IEntry const& entryJpegOrientation =
      pMetadata->entryFor(MTK_JPEG_ORIENTATION);
  if (entryJpegOrientation.isEmpty()) {
    MY_LOGW("No tag: MTK_JPEG_ORIENTATION");
    return -EINVAL;
  }

  int32_t jpegFlip = 0;
  IMetadata::IEntry const& entryJpegFlip =
      pMetadata->entryFor(MTK_CONTROL_CAPTURE_JPEG_FLIP_MODE);
  if (entryJpegFlip.isEmpty()) {
    MY_LOGD("No tag: MTK_CONTROL_CAPTURE_JPEG_FLIP_MODE");
  } else {
    jpegFlip = entryJpegFlip.itemAt(0, Type2Type<MINT32>());
  }

  int32_t jpegFlipProp =
      ::property_get_int32("vendor.debug.camera.Jpeg.flip", 0);

  int32_t const jpegOrientation =
      entryJpegOrientation.itemAt(0, Type2Type<MINT32>());
  uint32_t reqTransform = 0;

  if (jpegFlip || jpegFlipProp) {
    if (0 == jpegOrientation) {
      reqTransform = eTransform_FLIP_H;
    } else if (90 == jpegOrientation) {
      reqTransform = eTransform_ROT_90 | eTransform_FLIP_V;
    } else if (180 == jpegOrientation) {
      reqTransform = eTransform_FLIP_V;
    } else if (270 == jpegOrientation) {
      reqTransform = eTransform_ROT_90 | eTransform_FLIP_H;
    } else {
      MY_LOGW("Invalid Jpeg Orientation value: %d", jpegOrientation);
    }
  } else {
    if (0 == jpegOrientation) {
      reqTransform = 0;
    } else if (90 == jpegOrientation) {
      reqTransform = eTransform_ROT_90;
    } else if (180 == jpegOrientation) {
      reqTransform = eTransform_ROT_180;
    } else if (270 == jpegOrientation) {
      reqTransform = eTransform_ROT_270;
    } else {
      MY_LOGW("Invalid Jpeg Orientation value: %d", jpegOrientation);
    }
  }
  MY_LOGD_IF(
      1,
      "Jpeg orientation metadata: %d degrees; transform request(%d) & flip(%d)",
      jpegOrientation, reqTransform, jpegFlip);
  return reqTransform;
}

static auto createStreamInfoLocked_Thumbnail_YUV(
    NSCam::v3::pipeline::policy::capturestreamupdater::RequestOutputParams* out,
    NSCam::v3::pipeline::policy::capturestreamupdater::RequestInputParams const&
        in) -> int {
  auto const pMetadata = in.pRequest_AppControl;
  auto const pCfgThumbYUV = *(in.pConfiguration_HalImage_Thumbnail_YUV);
  auto const pCfgJpegYUV = *(in.pConfiguration_HalImage_Jpeg_YUV);
  if (CC_UNLIKELY(!pMetadata || !pCfgThumbYUV.get())) {
    MY_LOGE("invlaid input params");
    return NO_INIT;
  }

  IMetadata::IEntry const& entryThumbnailSize =
      pMetadata->entryFor(MTK_JPEG_THUMBNAIL_SIZE);
  MSize thumbnailSize;
  if (entryThumbnailSize.isEmpty()) {
    int sensorID = in.sensorID;
    std::shared_ptr<IMetadataProvider> pMetadataProvider =
        NSMetadataProviderManager::valueFor(sensorID);
    IMetadata static_meta = pMetadataProvider->getMtkStaticCharacteristics();
    IMetadata::IEntry const& entryAvaliableSize =
        static_meta.entryFor(MTK_JPEG_AVAILABLE_THUMBNAIL_SIZES);
    if (entryAvaliableSize.count() == 0) {
      MY_LOGW("No tag: MTK_JPEG_AVAILABLE_THUMBNAIL_SIZES");
      return OK;
    }
    MSize jpegSize = pCfgJpegYUV->getImgSize();
    MY_LOGI(
        "Select thumbnail size from MTK_JPEG_AVAILABLE_THUMBNAIL_SIZES, "
        "sersorID:%d, JpegSize size: w x h: %dx%d",
        sensorID, jpegSize.w, jpegSize.h);
    MUINT32 thumbnailWidth = 0;
    MUINT32 thumbnailHeight = 0;
    if (jpegSize.w != 0 && jpegSize.h != 0) {
      double picAspectRatio =
          static_cast<double>(jpegSize.w) / static_cast<double>(jpegSize.h);
      for (int index = 0; index < entryAvaliableSize.count(); index++) {
        MSize size = entryAvaliableSize.itemAt(index, Type2Type<MSize>());
        MY_LOGD("thumbnail index: %d, size: wxh: %dx%d", index, size.w, size.h);
        if (size.h == 0) {
          continue;
        }
        double thumbnailAspectRatio =
            static_cast<double>(size.w) / static_cast<double>(size.h);
        double dis = std::abs(picAspectRatio - thumbnailAspectRatio);
        if (dis > ASPECT_TOLERANCE) {
          continue;
        }
        if (size.w > thumbnailWidth) {
          thumbnailWidth = size.w;
          thumbnailHeight = size.h;
        }
      }
      if ((0 != thumbnailWidth) && (0 != thumbnailHeight)) {
        thumbnailSize.w = thumbnailWidth;
        thumbnailSize.h = thumbnailHeight;
      } else {
        MY_LOGW("Bad thumbnail size: %dx%d", thumbnailSize.w, thumbnailSize.h);
        return OK;
      }
    } else {
      MY_LOGW("Bad jpegSize: %dx%d", jpegSize.w, jpegSize.h);
      return OK;
    }
  } else {
    thumbnailSize = entryThumbnailSize.itemAt(0, Type2Type<MSize>());
  }
  MY_LOGD_IF(1, "thumbnail size from metadata: %dx%d", thumbnailSize.w,
             thumbnailSize.h);
  //
  IMetadata::IEntry const& entryJpegOrientation =
      pMetadata->entryFor(MTK_JPEG_ORIENTATION);
  if (entryJpegOrientation.isEmpty()) {
    MY_LOGW("No tag: MTK_JPEG_ORIENTATION");
    return OK;
  }
  //
  MSize const yuvthumbnailsize = thumbnailSize;
  //
  MUINT32 jpegReqTransform = getJpegRotation(pMetadata);
  MUINT32 jpegCfgTransform = pCfgJpegYUV->getTransform();
  // using portait rotation, ignoring normal rotation
  if (jpegCfgTransform != 0) {
    jpegReqTransform = jpegCfgTransform;
    MY_LOGW("modify thumb image req transform to portait transform");
  }
  MSize thunmbSize;
  if (jpegReqTransform & eTransform_ROT_90) {
    thunmbSize.w = std::min(yuvthumbnailsize.w, yuvthumbnailsize.h);
    thunmbSize.h = std::max(yuvthumbnailsize.w, yuvthumbnailsize.h);
  } else {
    thunmbSize = yuvthumbnailsize;
  }

  //
  MINT const format = pCfgThumbYUV->getImgFormat();
  IImageStreamInfo::BufPlanes_t bufPlanes;
  switch (format) {
    case eImgFmt_YUY2: {
      IImageStreamInfo::BufPlane bufPlane;
      bufPlane.rowStrideInBytes = (thunmbSize.w << 1);
      bufPlane.sizeInBytes = bufPlane.rowStrideInBytes * thunmbSize.h;
      bufPlanes.push_back(bufPlane);
    } break;
    default:
      MY_LOGE("not supported format: %#x", format);
      break;
  }
  //
  auto pStreamInfo = std::make_shared<NSCam::v3::Utils::ImageStreamInfo>(
      pCfgThumbYUV->getStreamName(), pCfgThumbYUV->getStreamId(),
      pCfgThumbYUV->getStreamType(), pCfgThumbYUV->getMaxBufNum(),
      pCfgThumbYUV->getMinInitBufNum(), pCfgThumbYUV->getUsageForAllocator(),
      format, thunmbSize, bufPlanes, jpegReqTransform);
  if (CC_UNLIKELY(!pStreamInfo.get())) {
    MY_LOGE("fail to new thumbnail ImageStreamInfo: %s %#" PRIx64,
            pCfgThumbYUV->getStreamName(), pCfgThumbYUV->getStreamId());
    return NO_MEMORY;
  }

  *out->pHalImage_Thumbnail_YUV = pStreamInfo;

  MY_LOGD_IF(
      1,
      "streamId:%#" PRIx64
      " name(%s) req(%p) cfg(%p) yuvthumbnailsize:%dx%d jpegTransform:%d",
      pStreamInfo->getStreamId(), pStreamInfo->getStreamName(),
      pStreamInfo.get(), pCfgThumbYUV.get(), thunmbSize.w, thunmbSize.h,
      jpegReqTransform);

  return OK;
}

static auto createRotationStreamInfoLocked_Main_YUV(
    NSCam::v3::pipeline::policy::capturestreamupdater::RequestOutputParams& out
        __unused,
    NSCam::v3::pipeline::policy::capturestreamupdater::RequestInputParams const&
        in __unused) -> int {
  auto const pMetadata = in.pRequest_AppControl;
  auto const pCfgMainYUV = *(in.pConfiguration_HalImage_Jpeg_YUV);
  if (CC_UNLIKELY(!pMetadata || !pCfgMainYUV.get())) {
    MY_LOGE("invlaid input params");
    return NO_INIT;
  }
  uint32_t reqTransform = getJpegRotation(pMetadata);
  uint32_t const cfgTransform = pCfgMainYUV->getTransform();
  MY_LOGD_IF(1, "Jpeg rotation: transform request(%d) & config(%d)",
             reqTransform, cfgTransform);
  if (cfgTransform != 0) {  // using portait rotation, ignoring normal rotation
    reqTransform = cfgTransform;
    MY_LOGW("modify jpeg image req transform to portait transform");
  }
  if (reqTransform == cfgTransform) {
    *out.pHalImage_Jpeg_YUV = nullptr;
    return OK;
  }

  MSize size, cfgSize(pCfgMainYUV->getImgSize().w, pCfgMainYUV->getImgSize().h);
  if (reqTransform & eTransform_ROT_90) {
    size.w = std::min(cfgSize.w, cfgSize.h);
    size.h = std::max(cfgSize.w, cfgSize.h);
  } else {
    size = cfgSize;
  }

  auto pStreamInfo = createImageStreamInfo(
      pCfgMainYUV->getStreamName(), pCfgMainYUV->getStreamId(),
      pCfgMainYUV->getStreamType(), pCfgMainYUV->getMaxBufNum(),
      pCfgMainYUV->getMinInitBufNum(), pCfgMainYUV->getUsageForAllocator(),
      pCfgMainYUV->getImgFormat(), size, reqTransform);
  if (CC_UNLIKELY(!pStreamInfo.get())) {
    MY_LOGE("fail to new ImageStreamInfo: %s %#" PRIx64,
            pCfgMainYUV->getStreamName(), pCfgMainYUV->getStreamId());
    return NO_MEMORY;
  }

  *out.pHalImage_Jpeg_YUV = pStreamInfo;

  MY_LOGD_IF(1,
             "streamId:%#" PRIx64
             " name(%s) req(%p) cfg(%p) yuvsize(%dx%d) reqTransform(%d)",
             pStreamInfo->getStreamId(), pStreamInfo->getStreamName(),
             pStreamInfo.get(), pCfgMainYUV.get(), pStreamInfo->getImgSize().w,
             pStreamInfo->getImgSize().h, reqTransform);

  return OK;
}

/**
 * Make a function target as a policy - default version
 */
FunctionType_CaptureStreamUpdaterPolicy
makePolicy_CaptureStreamUpdater_Default() {
  return
      [](NSCam::v3::pipeline::policy::capturestreamupdater::RequestOutputParams&
             out __unused,
         NSCam::v3::pipeline::policy::capturestreamupdater::
             RequestInputParams const& in __unused) -> int {
        int err = createStreamInfoLocked_Thumbnail_YUV(&out, in);
        if (CC_UNLIKELY(OK != err)) {
          MY_LOGW("err:%d(%s)", err, ::strerror(-err));
        }

        if (!in.isJpegRotationSupported) {
          *out.pHalImage_Jpeg_YUV = nullptr;
        } else {
          err = createRotationStreamInfoLocked_Main_YUV(out, in);
        }

        return err;
      };
}

};  // namespace policy
};  // namespace pipeline
};  // namespace v3
};  // namespace NSCam
