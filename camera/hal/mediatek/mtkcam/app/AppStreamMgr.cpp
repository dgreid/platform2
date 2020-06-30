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

#undef LOG_TAG
#define LOG_TAG "MtkCam/AppStreamMgr"
//
#include "camera/hal/mediatek/mtkcam/app/AppStreamMgr.h"
#include <cros-camera/camera_buffer_manager.h>
#include <mtkcam/ipc/client/Mediatek3AClient.h>
#include <mtkcam/utils/gralloc/IGrallocHelper.h>
#include <mtkcam/utils/metadata/client/TagMap.h>
#include <mtkcam/utils/metadata/mtk_metadata_types.h>
#include <mtkcam/utils/std/Profile.h>
#include <mtkcam/utils/std/Trace.h>
#include "MyUtils.h"
//
#include <property_service/property.h>
#include <property_service/property_lib.h>
#include <sys/prctl.h>

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<NSCam::v3::IAppStreamManager>
NSCam::v3::IAppStreamManager::create(
    MINT32 openId,
    camera3_callback_ops const* callback_ops,
    std::shared_ptr<IMetadataProvider> pMetadataProvider,
    MBOOL isDumpOutputInfo) {
  return std::make_shared<NSCam::v3::Imp::AppStreamMgr>(
      openId, callback_ops, pMetadataProvider, isDumpOutputInfo);
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
NSCam::v3::Imp::AppStreamMgr::destroy() {
  requestExit();
  if (mThread.joinable()) {
    mThread.join();
  }
  //
  if (mMetadata) {
    ::free_camera_metadata(mMetadata);
    mMetadata = nullptr;
  }
  //
  MY_LOGD("-");
}

/******************************************************************************
 *
 ******************************************************************************/
// Good place to do one-time initializations
MERROR
NSCam::v3::Imp::AppStreamMgr::readyToRun() {
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
void NSCam::v3::Imp::AppStreamMgr::requestExit() {
  MY_LOGD("+");
  //
  {
    std::lock_guard<std::mutex> _l(mResultQueueLock);
    mExitPending = true;
    mResultQueueCond.notify_all();
  }
  //
  MY_LOGD("-");
}

/******************************************************************************
 *
 ******************************************************************************/
bool NSCam::v3::Imp::AppStreamMgr::threadLoop() {
  while (this->_threadLoop()) {
    std::lock_guard<std::mutex> _l(mResultQueueLock);
    if (mExitPending)
      break;
  }
  MY_LOGI("threadLoop exit");
  return true;
}

bool NSCam::v3::Imp::AppStreamMgr::_threadLoop() {
  ResultQueueT vResult;
  MERROR err = dequeResult(&vResult);
  if (OK == err && !vResult.empty()) {
    handleResult(vResult);
  }
  //
  return true;
}

/******************************************************************************
 *
 ******************************************************************************/
NSCam::v3::Imp::AppStreamMgr::AppStreamMgr(
    MINT32 openId,
    camera3_callback_ops const* callback_ops,
    std::shared_ptr<IMetadataProvider> pMetadataProvider,
    MBOOL isExternal)
    : mOpenId(openId),
      mpCallbackOps(callback_ops)
      //
      ,
      mpMetadataProvider(pMetadataProvider),
      mpMetadataConverter(IMetadataConverter::createInstance(
          IDefaultMetadataTagSet::singleton()->getTagSet())),
      mMetadata(nullptr),
      mExitPending(false)
      //
      ,
      mFrameHandler(
          std::make_shared<FrameHandler>(pMetadataProvider, isExternal)),
      mStreamIdToConfig(eSTREAMID_BEGIN_OF_APP)
      //
      ,
      mAvgTimestampDuration(0),
      mAvgCallbackDuration(0),
      mAvgTimestampFps(0),
      mAvgCallbackFps(0),
      mFrameCounter(0),
      mMaxFrameCount(33),
      mTimestamp(0),
      mCallbackTime(0),
      mInputType(TYPE_NONE),
      mHasImplemt(false),
      mHasVideoEnc(false) {
  IMetadata::IEntry const& entry =
      mpMetadataProvider->getMtkStaticCharacteristics().entryFor(
          MTK_REQUEST_PARTIAL_RESULT_COUNT);
  if (entry.isEmpty()) {
    MY_LOGE("no static REQUEST_PARTIAL_RESULT_COUNT");
    mAtMostMetaStreamCount = 1;
  } else {
    mAtMostMetaStreamCount = entry.itemAt(0, Type2Type<MINT32>());
  }

  if (Mediatek3AClient::getInstance())
    Mediatek3AClient::getInstance()->registerErrorCallback(this);
  //
  char cLogLevel[PROPERTY_VALUE_MAX];
  property_get("debug.camera.log", cLogLevel, "0");
  mLogLevel = atoi(cLogLevel);
  if (mLogLevel == 0) {
    property_get("debug.camera.log.AppStreamMgr", cLogLevel, "0");
    mLogLevel = atoi(cLogLevel);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
NSCam::v3::Imp::AppStreamMgr::waitUntilDrained(int64_t const timeout) {
  {
    //
    int64_t const startTime = NSCam::Utils::getTimeInNs();
    std::unique_lock<std::mutex> l(mFrameHandlerLock);
    //
    std::cv_status err;
    int64_t const elapsedInterval = (NSCam::Utils::getTimeInNs() - startTime);
    int64_t const timeWait =
        (timeout > elapsedInterval) ? (timeout - elapsedInterval) : 0;
    while (!mFrameHandler->isEmptyFrameQueue()) {
      err = mFrameHandlerCond.wait_for(l, std::chrono::nanoseconds(timeWait));
      if (err == std::cv_status::timeout) {
        MY_LOGW("FrameQueue#:%zu timeout(ns):%" PRId64 " elapsed(ns):%" PRId64
                ".",
                mFrameHandler->getFrameQueueSize(), timeout,
                (NSCam::Utils::getTimeInNs() - startTime));
        mFrameHandler->dump();
        return TIMED_OUT;
      }
    }
  }

  MY_LOGI("wait mFrameHandlerCond done");
  requestExit();
  if (mThread.joinable()) {
    mThread.join();
  }

  mInputType.reset();
  mHasImplemt = false;
  mHasVideoEnc = false;

  MY_LOGI("wait mResultQueueCond done");
  {
    std::lock_guard<std::mutex> _l(mResultQueueLock);
    mExitPending = false;
    mThread =
        std::thread(std::bind(&NSCam::v3::Imp::AppStreamMgr::threadLoop, this));
  }
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
NSCam::v3::Imp::AppStreamMgr::queryOldestRequestNumber(MUINT32* ReqNo) {
  return mFrameHandler->queryOldestRequestNumber(ReqNo);
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
NSCam::v3::Imp::AppStreamMgr::checkStream(camera3_stream* stream) const {
  if (!stream) {
    MY_LOGE("NULL stream");
    return -EINVAL;
  }
  //
  /**
   * Return values:
   *
   *  0:      On successful stream configuration
   *
   * -EINVAL: If the requested stream configuration is invalid. Some examples
   *          of invalid stream configurations include:
   *
   *          - Including streams with unsupported formats, or an unsupported
   *            size for that format.
   *
   *          ....
   *
   *          - Unsupported rotation configuration (only applies to
   *            devices with version >= CAMERA_DEVICE_API_VERSION_3_3)
   */
  if (stream->crop_rotate_scale_degrees != CAMERA3_STREAM_ROTATION_0 &&
      stream->crop_rotate_scale_degrees != CAMERA3_STREAM_ROTATION_90 &&
      stream->crop_rotate_scale_degrees != CAMERA3_STREAM_ROTATION_270) {
    MY_LOGE("Invalid rotation value %d", stream->crop_rotate_scale_degrees);
    return -EINVAL;
  }
  if (stream->rotation == CAMERA3_STREAM_ROTATION_0 &&
      stream->crop_rotate_scale_degrees != CAMERA3_STREAM_ROTATION_0) {
    stream->rotation = stream->crop_rotate_scale_degrees;
  }
  switch (stream->data_space) {
    case HAL_DATASPACE_BT601_525:
    case HAL_DATASPACE_BT601_625:
    case HAL_DATASPACE_BT709:
    case HAL_DATASPACE_JFIF:
    case HAL_DATASPACE_UNKNOWN:
      MY_LOGI("framework stream dataspace:0x%x", stream->data_space);
      break;
    case HAL_DATASPACE_DEPTH:
      MY_LOGE("Not support depth dataspace:0x%x!", stream->data_space);
      return -EINVAL;
    default:
      MY_LOGW("framework stream dataspace:0x%x", stream->data_space);
  }
  //
  if (CAMERA3_STREAM_ROTATION_0 != stream->rotation) {
    MY_LOGI("stream format:0x%x w/ rotation:%d", stream->format,
            stream->rotation);
    if (CAMERA3_STREAM_INPUT == stream->stream_type) {
      MY_LOGE("input stream cannot support rotation");
      return -EINVAL;
    }
  }
  //
  IMetadata::IEntry const& entryScaler =
      mpMetadataProvider->getMtkStaticCharacteristics().entryFor(
          MTK_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);

  if (entryScaler.isEmpty()) {
    MY_LOGE("no static MTK_SCALER_AVAILABLE_STREAM_CONFIGURATIONS");
    return -EINVAL;
  }

  if (HAL_PIXEL_FORMAT_RAW16 == stream->format ||
      HAL_PIXEL_FORMAT_RAW_OPAQUE == stream->format) {
    if (CAMERA3_STREAM_ROTATION_0 != stream->rotation) {
      MY_LOGE("raw stream cannot support rotation");
      return -EINVAL;
    }
  }

  // android.scaler.availableStreamConfigurations
  // int32 x n x 4
  MUINT i;
  for (i = 0; i < entryScaler.count(); i += 4) {
    if (entryScaler.itemAt(i, Type2Type<MINT32>()) != stream->format) {
      continue;
    }
    MUINT32 scalerWidth = entryScaler.itemAt(i + 1, Type2Type<MINT32>());
    MUINT32 scalerHeight = entryScaler.itemAt(i + 2, Type2Type<MINT32>());

    if ((stream->width == scalerWidth && stream->height == scalerHeight) ||
        (stream->rotation & CAMERA3_STREAM_ROTATION_90 &&
         stream->width == scalerHeight && stream->height == scalerWidth)) {
      return OK;
    }
  }

  MY_LOGE("unsupported size w:%d h:%d for format %d", stream->width,
          stream->height, stream->format);
  return -EINVAL;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
NSCam::v3::Imp::AppStreamMgr::checkStreams(
    camera3_stream_configuration_t* stream_list) const {
  if (!stream_list) {
    MY_LOGE("NULL stream_list");
    return -EINVAL;
  }
  //
  if (!stream_list->streams) {
    MY_LOGE("NULL stream_list->streams");
    return -EINVAL;
  }
  //
  if (0 == stream_list->num_streams) {
    MY_LOGE("stream_list->num_streams = 0");
    return -EINVAL;
  }
  //
  //
  std::map<int, size_t> typeNum;
  typeNum.emplace(CAMERA3_STREAM_OUTPUT, 0);
  typeNum.emplace(CAMERA3_STREAM_INPUT, 0);
  typeNum.emplace(CAMERA3_STREAM_BIDIRECTIONAL, 0);
  //
  std::map<int, size_t> outRotationNum;
  outRotationNum.emplace(CAMERA3_STREAM_ROTATION_0, 0);
  outRotationNum.emplace(CAMERA3_STREAM_ROTATION_90, 0);
  outRotationNum.emplace(CAMERA3_STREAM_ROTATION_180, 0);
  outRotationNum.emplace(CAMERA3_STREAM_ROTATION_270, 0);
  //
  for (size_t i = 0; i < stream_list->num_streams; i++) {
    camera3_stream* stream = stream_list->streams[i];
    //
    MERROR err = checkStream(stream);
    if (OK != err) {
      MY_LOGE("streams[%zu] has a bad status: %d(%s)", i, err,
              ::strerror(-err));
      return err;
    }
    //
    typeNum.at(stream->stream_type)++;
    if (CAMERA3_STREAM_INPUT != stream->stream_type) {
      outRotationNum.at(stream->rotation)++;
    }
  }

  /**
   * At most one input-capable stream may be defined (INPUT or BIDIRECTIONAL)
   * in a single configuration.
   *
   * At least one output-capable stream must be defined (OUTPUT or
   * BIDIRECTIONAL).
   */
  /*
   *
   * Return values:
   *
   *  0:      On successful stream configuration
   *
   * -EINVAL: If the requested stream configuration is invalid. Some examples
   *          of invalid stream configurations include:
   *
   *          - Including more than 1 input-capable stream (INPUT or
   *            BIDIRECTIONAL)
   *
   *          - Not including any output-capable streams (OUTPUT or
   *            BIDIRECTIONAL)
   *
   *          - Including too many output streams of a certain format.
   *
   *          - Unsupported rotation configuration (only applies to
   *            devices with version >= CAMERA_DEVICE_API_VERSION_3_3)
   */
  size_t const num_stream_O = typeNum[CAMERA3_STREAM_OUTPUT];
  size_t const num_stream_I = typeNum[CAMERA3_STREAM_INPUT];
  size_t const num_stream_IO = typeNum[CAMERA3_STREAM_BIDIRECTIONAL];

  if (num_stream_O + num_stream_IO == 0) {
    MY_LOGE("bad stream count: (out, in, in-out)=(%zu, %zu, %zu)", num_stream_O,
            num_stream_I, num_stream_IO);
    return -EINVAL;
  }
  //
  size_t const num_rotation_not0 = outRotationNum[CAMERA3_STREAM_ROTATION_90] +
                                   outRotationNum[CAMERA3_STREAM_ROTATION_180] +
                                   outRotationNum[CAMERA3_STREAM_ROTATION_270];
  if (num_rotation_not0 > 1) {
    // we cannot decide whether it's resonalbe for isp to generate more than 1
    // oupout stream w/ rotation value here it should be determined by
    // pipelinemodel configuration step to predict pass2 worst case scenario
    MY_LOGW("more than one output streams need to rotation");
    return -EINVAL;
  }

  if ((num_rotation_not0 > 0) &&
      (outRotationNum[CAMERA3_STREAM_ROTATION_0] > 0)) {
    // for Camera3MultiStreamTest, DifferentRotation
    MY_LOGW("more than one output streams need to rotation");
    return -EINVAL;
  }
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
NSCam::v3::Imp::AppStreamMgr::configureStreams(
    camera3_stream_configuration_t* stream_list) {
  MERROR err = OK;
  //
  err = checkStreams(stream_list);
  if (OK != err) {
    return err;
  }
  //
  std::lock_guard<std::mutex> _l(mFrameHandlerLock);
  //
  {
    StreamId_T streamId = mStreamIdToConfig++;
    mFrameHandler->addConfigStream(createMetaStreamInfo(streamId));
  }
  //
  for (size_t i = 0; i < stream_list->num_streams; i++) {
    if (stream_list->streams[i]->stream_type == CAMERA3_STREAM_OUTPUT &&
        stream_list->streams[i]->format ==
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
      mHasImplemt = true;
    }
    if (stream_list->streams[i]->stream_type == CAMERA3_STREAM_BIDIRECTIONAL ||
        stream_list->streams[i]->stream_type == CAMERA3_STREAM_INPUT) {
      if (stream_list->streams[i]->format ==
          HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
        mInputType.set(TYPE_IMPLEMENTATION_DEFINED);
      } else if (stream_list->streams[i]->format ==
                 HAL_PIXEL_FORMAT_YCbCr_420_888) {
        mInputType.set(TYPE_YUV);
      }
    }
  }

  for (size_t i = 0; i < stream_list->num_streams; i++) {
    StreamId_T streamId = mStreamIdToConfig++;
    mFrameHandler->addConfigStream(
        createImageStreamInfo(streamId, stream_list->streams[i]));
  }
  //
  //  An emtpy settings buffer cannot be used as the first submitted request
  //  after a configure_streams() call.
  mLatestSettings.clear();
  //
  {
    std::lock_guard<std::mutex> _l(mResultQueueLock);
    mExitPending = false;
    mThread =
        std::thread(std::bind(&NSCam::v3::Imp::AppStreamMgr::threadLoop, this));
  }
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<NSCam::v3::Imp::AppStreamMgr::AppImageStreamInfo>
NSCam::v3::Imp::AppStreamMgr::createImageStreamInfo(
    StreamId_T suggestedStreamId, camera3_stream* stream) {
  MERROR err = OK;
  //
  MINT formatToAllocate = stream->format;
  MUINT usageForConsumer = stream->usage;

  MUINT usageForAllocator = usageForConsumer;
  if (stream->stream_type == CAMERA3_STREAM_OUTPUT) {
    usageForAllocator |= GRALLOC_USAGE_HW_CAMERA_WRITE;
  } else if (stream->stream_type == CAMERA3_STREAM_INPUT) {
    usageForAllocator |= GRALLOC_USAGE_HW_CAMERA_READ;
  } else if (stream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL &&
             formatToAllocate == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
    usageForAllocator |= GRALLOC_USAGE_HW_CAMERA_ZSL;
  } else {
    usageForAllocator |= GRALLOC_USAGE_HW_CAMERA_WRITE;
  }

  if (formatToAllocate == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
    if (stream->stream_type == CAMERA3_STREAM_OUTPUT) {
      bool isPreviewSurface = usageForConsumer & (GRALLOC_USAGE_HW_COMPOSER |
                                                  GRALLOC_USAGE_HW_TEXTURE);
      if (!isPreviewSurface && mInputType.test(TYPE_IMPLEMENTATION_DEFINED)) {
        usageForAllocator |= GRALLOC_USAGE_HW_CAMERA_ZSL;
        usageForConsumer |= GRALLOC_USAGE_HW_CAMERA_ZSL;
      } else {
        usageForAllocator |= GRALLOC_USAGE_HW_COMPOSER;
        usageForConsumer |= GRALLOC_USAGE_HW_COMPOSER;
      }
    } else if (stream->stream_type == CAMERA3_STREAM_INPUT) {
      usageForAllocator |= GRALLOC_USAGE_HW_CAMERA_ZSL;
      usageForConsumer |= GRALLOC_USAGE_HW_CAMERA_ZSL;
    }
  } else if (formatToAllocate == HAL_PIXEL_FORMAT_RAW_OPAQUE) {
    usageForAllocator |= GRALLOC_USAGE_HW_CAMERA_ZSL;
  } else if (formatToAllocate == HAL_PIXEL_FORMAT_YCbCr_420_888) {
    if (mHasImplemt && !mHasVideoEnc) {
      if ((stream->width <= 1920 && stream->height <= 1080) ||
          // testPreviewFpsRange will check this size
          (!mInputType.test(TYPE_IMPLEMENTATION_DEFINED) &&
           !mInputType.test(TYPE_YUV))) {
        usageForConsumer |= GRALLOC_USAGE_HW_VIDEO_ENCODER;
        mHasVideoEnc = true;
      }
    } else if (!mHasImplemt && !mInputType.test(TYPE_YUV)) {
      usageForConsumer |= GRALLOC_USAGE_HW_COMPOSER;
      mHasImplemt = true;
    }
  }

  //
  IGrallocHelper* pGrallocHelper = IGrallocHelper::singleton();
  //
  GrallocStaticInfo grallocStaticInfo;
  GrallocRequest grallocRequest;
  grallocRequest.usage = usageForAllocator;
  grallocRequest.format = formatToAllocate;
  if (HAL_PIXEL_FORMAT_BLOB == formatToAllocate) {
    IMetadata::IEntry const& entry =
        mpMetadataProvider->getMtkStaticCharacteristics().entryFor(
            MTK_JPEG_MAX_SIZE);
    if (entry.isEmpty()) {
      MY_LOGE("no static JPEG_MAX_SIZE");
      grallocRequest.widthInPixels = stream->width * stream->height * 2;
    } else {
      grallocRequest.widthInPixels = entry.itemAt(0, Type2Type<MINT32>());
    }
    grallocRequest.heightInPixels = 1;
  } else {
    grallocRequest.widthInPixels = stream->width;
    grallocRequest.heightInPixels = stream->height;
  }
  //
  err = pGrallocHelper->query(&grallocRequest, &grallocStaticInfo);
  if (OK != err) {
    MY_LOGE("IGrallocHelper::query - err:%d(%s)", err, ::strerror(-err));
    return NULL;
  }
  //
  std::string s8FormatToAllocate =
      pGrallocHelper->queryPixelFormatName(formatToAllocate);
  std::string s8FormatAllocated =
      pGrallocHelper->queryPixelFormatName(grallocStaticInfo.format);
  std::string s8UsageForConsumer =
      pGrallocHelper->queryGrallocUsageName(usageForConsumer);
  std::string s8UsageForAllocator =
      pGrallocHelper->queryGrallocUsageName(usageForAllocator);
  //
  StreamId_T streamId = suggestedStreamId;
  std::string s8StreamName("Image:App:");
  //
  if ((usageForConsumer & GRALLOC_USAGE_HW_VIDEO_ENCODER)) {
    std::string sGrallocUsageName =
        pGrallocHelper->queryGrallocUsageName(GRALLOC_USAGE_HW_VIDEO_ENCODER);
    s8StreamName.append(sGrallocUsageName.c_str());
  } else {
    switch (grallocStaticInfo.format) {
      case HAL_PIXEL_FORMAT_BLOB:
        s8StreamName += "JPEG-BLOB";
        break;
      case eImgFmt_NV12:
      case HAL_PIXEL_FORMAT_YV12:
      case HAL_PIXEL_FORMAT_YCrCb_420_SP:
      case HAL_PIXEL_FORMAT_YCbCr_422_I:
      case HAL_PIXEL_FORMAT_RAW16:
      case HAL_PIXEL_FORMAT_RAW_OPAQUE:
        s8StreamName.append(s8FormatAllocated.c_str());
        break;
      case HAL_PIXEL_FORMAT_Y16:
      default:
        MY_LOGE("Unsupported format:0x%x(%s)", grallocStaticInfo.format,
                s8FormatAllocated.c_str());
        return NULL;
    }
  }
  //
  std::string s8Planes;
  IImageStreamInfo::BufPlanes_t bufPlanes;
  bufPlanes.resize(grallocStaticInfo.planes.size());
  for (size_t i = 0; i < bufPlanes.size(); i++) {
    IImageStreamInfo::BufPlane& plane = bufPlanes[i];
    plane.sizeInBytes = grallocStaticInfo.planes[i].sizeInBytes;
    plane.rowStrideInBytes = grallocStaticInfo.planes[i].rowStrideInBytes;
    //
    s8Planes += base::StringPrintf(" %zu/%zu", plane.rowStrideInBytes,
                                   plane.sizeInBytes);
  }
  //
  MUINT32 transform =
      (stream->rotation == CAMERA3_STREAM_ROTATION_90)
          ? HAL_TRANSFORM_ROT_270
          : (stream->rotation == CAMERA3_STREAM_ROTATION_180)
                ? HAL_TRANSFORM_ROT_180
                : (stream->rotation == CAMERA3_STREAM_ROTATION_270)
                      ? HAL_TRANSFORM_ROT_90
                      : 0;
  if (stream->crop_rotate_scale_degrees != 0) {
    transform =
        (stream->crop_rotate_scale_degrees == CAMERA3_STREAM_ROTATION_90)
            ? HAL_TRANSFORM_ROT_90
            : (stream->crop_rotate_scale_degrees == CAMERA3_STREAM_ROTATION_270)
                  ? HAL_TRANSFORM_ROT_270
                  : 0;
    MY_LOGD("PortraitRotation rotation %d, transform %d",
            stream->crop_rotate_scale_degrees, transform);
  }

  auto pStream = std::make_shared<AppImageStreamInfo>(
      stream, s8StreamName.c_str(), streamId, usageForConsumer,
      usageForAllocator, formatToAllocate, grallocStaticInfo.format, bufPlanes,
      0,
      transform,  // transform
      stream->data_space);

  MY_LOGI("[%" PRId64
          " %s] stream:%p->%p %dx%d type:%d"
          "rotation(%d)->transform(%d) dataspace(%d) "
          "formatToAllocate:%#x(%s) formatAllocated:%#x(%s) "
          "Consumer-usage:%#x(%s) Allocator-usage:%#x(%s) "
          "rowStrideInBytes/sizeInBytes:%s",
          pStream->getStreamId(), pStream->getStreamName(), stream,
          pStream.get(), pStream->getImgSize().w, pStream->getImgSize().h,
          stream->stream_type, stream->rotation, transform, stream->data_space,
          formatToAllocate, s8FormatToAllocate.c_str(),
          grallocStaticInfo.format, s8FormatAllocated.c_str(), usageForConsumer,
          s8UsageForConsumer.c_str(), usageForAllocator,
          s8UsageForAllocator.c_str(), s8Planes.c_str());

  return pStream;
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<NSCam::v3::Imp::AppStreamMgr::AppMetaStreamInfo>
NSCam::v3::Imp::AppStreamMgr::createMetaStreamInfo(
    StreamId_T suggestedStreamId) const {
  auto pStream = std::make_shared<AppMetaStreamInfo>(
      "Meta:App:Control", suggestedStreamId, eSTREAMTYPE_META_IN, 0);
  return pStream;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
NSCam::v3::Imp::AppStreamMgr::queryConfiguredStreams(
    ConfigAppStreams* rStreams) const {
  std::lock_guard<std::mutex> _l(mFrameHandlerLock);
  return mFrameHandler->getConfigStreams(rStreams);
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
NSCam::v3::Imp::AppStreamMgr::checkRequestLocked(
    camera3_capture_request_t const* request) const {
  if (NULL == request) {
    MY_LOGE("NULL request");
    return -EINVAL;
  }
  //
  //  there are 0 output buffers
  if (NULL == request->output_buffers || 0 == request->num_output_buffers) {
    MY_LOGE("[frameNo:%u] output_buffers:%p num_output_buffers:%d",
            request->frame_number, request->output_buffers,
            request->num_output_buffers);
    return -EINVAL;
  }
  //
  //  the settings are NULL when not allowed
  if (NULL == request->settings && mLatestSettings.isEmpty()) {
    MY_LOGE(
        "[frameNo:%u] NULL request settings; however most-recently submitted "
        "request is also NULL after configure_stream",
        request->frame_number);
    return -EINVAL;
  }
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
NSCam::v3::Imp::AppStreamMgr::createRequest(camera3_capture_request_t* request,
                                            Request* rRequest) {
  std::lock_guard<std::mutex> _l(mFrameHandlerLock);
  //
  MERROR err = checkRequestLocked(request);
  if (OK != err) {
    return err;
  }
  //
  rRequest->frameNo = request->frame_number;
  //
  //  vInputImageBuffers <- camera3_capture_request_t::input_buffer
  if (camera3_stream_buffer const* p_stream_buffer = request->input_buffer) {
    auto pStreamBuffer = createImageStreamBuffer(p_stream_buffer);
    //
    if (pStreamBuffer == nullptr) {
      MY_LOGE("NULL AppImageStreamBuffer of request->input_buffer");
      return -EINVAL;
    }
    rRequest->vInputImageBuffers.emplace(
        pStreamBuffer->getStreamInfo()->getStreamId(), pStreamBuffer);
  }
  //
  //  vOutputImageBuffers <- camera3_capture_request_t::output_buffers
  for (size_t i = 0; i < request->num_output_buffers; i++) {
    camera3_stream_buffer const* p_stream_buffer = &request->output_buffers[i];
    //
    auto pStreamBuffer = createImageStreamBuffer(p_stream_buffer);
    //
    if (pStreamBuffer == nullptr) {
      MY_LOGE("NULL AppImageStreamBuffer of request->output_buffers[%d]", i);
      return -EINVAL;
    }
    rRequest->vOutputImageBuffers.emplace(
        pStreamBuffer->getStreamInfo()->getStreamId(), pStreamBuffer);
  }
  //
  //  vInputMetaBuffers <- camera3_capture_request_t::settings
  {
    std::shared_ptr<IMetaStreamInfo> pStreamInfo =
        mFrameHandler->getConfigMetaStream(0);
    MBOOL isRepeating = MFALSE;
    //
    if (request->settings) {
      isRepeating = MFALSE;
      mLatestSettings.clear();
      if (!mpMetadataConverter->convert(request->settings, &mLatestSettings)) {
        MY_LOGE("frameNo:%u IMetadataConverter->convert",
                request->frame_number);
        return -ENODEV;
      }
      // to debug
      {
        if (mLogLevel >= 2) {
          mpMetadataConverter->dumpAll(mLatestSettings, request->frame_number);
        } else if (mLogLevel >= 1) {
          mpMetadataConverter->dump(mLatestSettings, request->frame_number);
        }
      }
    } else {
      /**
       * As a special case, a NULL settings buffer indicates that the
       * settings are identical to the most-recently submitted capture request.
       * A NULL buffer cannot be used as the first submitted request after a
       * configure_streams() call.
       */
      isRepeating = MTRUE;
      MY_LOGD_IF(
          mLogLevel >= 1,
          "frameNo:%u NULL settings -> most-recently submitted capture request",
          request->frame_number);
    }
    //
    auto pStreamBuffer =
        createMetaStreamBuffer(pStreamInfo, mLatestSettings, isRepeating);
    //
    rRequest->vInputMetaBuffers.emplace(
        pStreamBuffer->getStreamInfo()->getStreamId(), pStreamBuffer);

    if (!isRepeating) {
      camera_metadata_ro_entry e1;
      if (OK == find_camera_metadata_ro_entry(
                    request->settings, ANDROID_CONTROL_AF_TRIGGER, &e1) &&
          *e1.data.u8 == ANDROID_CONTROL_AF_TRIGGER_START) {
        CAM_TRACE_FMT_BEGIN("AF_state: %d", *e1.data.u8);
        MY_LOGD_IF(mLogLevel >= 1, "AF_state: %d", *e1.data.u8);
        CAM_TRACE_END();
      }

      camera_metadata_ro_entry e2;
      if (OK == find_camera_metadata_ro_entry(
                    request->settings, ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
                    &e2) &&
          *e2.data.u8 == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START) {
        CAM_TRACE_FMT_BEGIN("ae precap: %d", *e2.data.u8);
        MY_LOGD_IF(mLogLevel >= 1, "ae precapture trigger: %d", *e2.data.u8);
        CAM_TRACE_END();
      }

      camera_metadata_ro_entry e4;
      if (OK == find_camera_metadata_ro_entry(
                    request->settings, ANDROID_CONTROL_CAPTURE_INTENT, &e4)) {
        CAM_TRACE_FMT_BEGIN("capture intent: %d", *e4.data.u8);
        MY_LOGD_IF(mLogLevel >= 1, "capture intent: %d", *e4.data.u8);
        CAM_TRACE_END();
      }
    }
  }
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<NSCam::v3::Imp::AppStreamMgr::AppImageStreamBuffer>
NSCam::v3::Imp::AppStreamMgr::createImageStreamBuffer(
    camera3_stream_buffer const* buffer) const {
  MY_LOGI(
      "stream:%p buffer:%p status:%d acquire_fence:%d release_fence:%d type "
      "%d, width %d, height %d, format %d rotation %d",
      buffer->stream, buffer->buffer, buffer->status, buffer->acquire_fence,
      buffer->release_fence, buffer->stream->stream_type, buffer->stream->width,
      buffer->stream->height, buffer->stream->format, buffer->stream->rotation);

  cros::CameraBufferManager* cbm = cros::CameraBufferManager::GetInstance();
  MERROR status = cbm->Register(*buffer->buffer);
  if (OK != status) {
    MY_LOGE("cannot Register from buffer_handle_t - status:%d[%s]", status,
            ::strerror(status));
    return nullptr;
  }
  std::shared_ptr<IImageStreamInfo> pStreamInfo =
      AppImageStreamInfo::cast(buffer->stream);
  //
  std::shared_ptr<IGraphicImageBufferHeap> pImageBufferHeap =
      IGraphicImageBufferHeap::create(pStreamInfo->getStreamName(), buffer);
  //
  auto pStreamBuffer =
      AppImageStreamBuffer::Allocator(pStreamInfo)(pImageBufferHeap);
  //
  return pStreamBuffer;
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<NSCam::v3::Imp::AppStreamMgr::AppMetaStreamBuffer>
NSCam::v3::Imp::AppStreamMgr::createMetaStreamBuffer(
    std::shared_ptr<IMetaStreamInfo> pStreamInfo,
    IMetadata const& rSettings,
    MBOOL const repeating) const {
  auto pStreamBuffer = AppMetaStreamBuffer::Allocator(pStreamInfo)(rSettings);
  //
  pStreamBuffer->setRepeating(repeating);
  //
  return pStreamBuffer;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
NSCam::v3::Imp::AppStreamMgr::registerRequest(Request const& rRequest) {
  std::lock_guard<std::mutex> _l(mFrameHandlerLock);
  return mFrameHandler->registerFrame(rRequest);
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
NSCam::v3::Imp::AppStreamMgr::updateResult(
    MUINT32 const frameNo,
    MINTPTR const userId,
    std::vector<std::shared_ptr<IMetaStreamBuffer> > resultMeta,
    bool hasLastPartial) {
  enqueResult(frameNo, userId, resultMeta, hasLastPartial);
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
NSCam::v3::Imp::AppStreamMgr::enqueResult(
    MUINT32 const frameNo,
    MINTPTR const userId,
    std::vector<std::shared_ptr<IMetaStreamBuffer> > resultMeta,
    bool hasLastPartial) {
  NSCam::Utils::CamProfile profile(__FUNCTION__, "AppStreamManager");
  std::lock_guard<std::mutex> _l(mResultQueueLock);
  //
  if (exitPending()) {
    MY_LOGW("Dead ResultQueue");
    return DEAD_OBJECT;
  }
  profile.print_overtime(1, "std::mutex: frameNo:%u userId:%#" PRIxPTR, frameNo,
                         userId);
  //
  auto iter = mResultQueue.find(frameNo);
  profile.print_overtime(
      1, "indexOf ResultQueue#:%zu frameNo:%u userId:%#" PRIxPTR,
      mResultQueue.size(), frameNo, userId);
  if (iter != mResultQueue.end()) {
    MY_LOGD("frameNo:%u existed", frameNo);
    //
    std::shared_ptr<ResultItem> pItem = iter->second;
    pItem->lastPartial = hasLastPartial;
    pItem->buffer.insert(pItem->buffer.end(), resultMeta.begin(),
                         resultMeta.end());
    mResultQueueCond.notify_all();
  } else {
    std::shared_ptr<ResultItem> pItem = std::make_shared<ResultItem>();
    pItem->frameNo = frameNo;
    pItem->buffer = resultMeta;
    pItem->lastPartial = hasLastPartial;
    //
    mResultQueue.emplace(frameNo, pItem);
    mResultQueueCond.notify_all();
  }
  //
  profile.print_overtime(1, "- frameNo:%u userId:%#" PRIxPTR, frameNo, userId);
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
NSCam::v3::Imp::AppStreamMgr::dequeResult(ResultQueueT* rvResult) {
  MERROR err = OK;
  //
  std::unique_lock<std::mutex> lck(mResultQueueLock);
  //
  while (!exitPending() && mResultQueue.empty()) {
    mResultQueueCond.wait(lck);
  }
  //
  if (mResultQueue.empty()) {
    MY_LOGD_IF(mLogLevel >= 1, "empty queue");
    rvResult->clear();
    err = NOT_ENOUGH_DATA;
  } else {
    //  If the queue is not empty, deque all items from the queue.
    *rvResult = mResultQueue;
    mResultQueue.clear();
    err = OK;
  }
  //
  return err;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
NSCam::v3::Imp::AppStreamMgr::handleResult(ResultQueueT const& rvResult) {
  std::list<CallbackParcel> cbList;
  //
  {
    std::lock_guard<std::mutex> _l(mFrameHandlerLock);
    mFrameHandler->update(rvResult, &cbList);
  }
  //
  {
    std::list<CallbackParcel>::iterator it = cbList.begin();
    while (!cbList.empty()) {
      performCallback(*it);
      it = cbList.erase(it);
    }
  }
  //
  {
    std::lock_guard<std::mutex> _l(mFrameHandlerLock);
    if (mFrameHandler->isEmptyFrameQueue()) {
      mFrameHandlerCond.notify_all();
    }
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
NSCam::v3::Imp::AppStreamMgr::performCallback(CallbackParcel const& cbParcel) {
  uint32_t const frame_number = cbParcel.frameNo;
  //
  std::string str = base::StringPrintf("frameNo:%u", frame_number);
  {
    if (cbParcel.shutter != 0) {
      str +=
          base::StringPrintf(" shutter:%" PRId64, cbParcel.shutter->timestamp);
    }
    if (!cbParcel.vError.empty()) {
      str += base::StringPrintf(" Error#:%zu", cbParcel.vError.size());
    }
    if (!cbParcel.vOutputMetaItem.empty()) {
      str +=
          base::StringPrintf(" O:Meta#:%zu", cbParcel.vOutputMetaItem.size());
    }
    if (!cbParcel.vOutputImageItem.empty()) {
      str +=
          base::StringPrintf(" O:Image#:%zu", cbParcel.vOutputImageItem.size());
    }
    if (!cbParcel.vInputImageItem.empty()) {
      str +=
          base::StringPrintf(" I:Image#:%zu", cbParcel.vInputImageItem.size());
    }
    MY_LOGD_IF(mLogLevel >= 1, "+ %s", str.c_str());
  }
  //
  // CallbackParcel::shutter
  if (cbParcel.shutter != 0) {
    // to debug
    {
      if (cbParcel.shutter->timestamp < mTimestamp) {
        MY_LOGI(" #(%d), now shutter:%" PRId64 " last shutter:%" PRId64,
                frame_number, cbParcel.shutter->timestamp, mTimestamp);
      }
      mAvgTimestampDuration += cbParcel.shutter->timestamp - mTimestamp;
      mTimestamp = cbParcel.shutter->timestamp;
      if (mAvgTimestampFps == 0) {
        mAvgTimestampFps = cbParcel.shutter->timestamp;
      }
      if (mFrameCounter >= mMaxFrameCount) {
        mAvgTimestampFps = cbParcel.shutter->timestamp - mAvgTimestampFps;
      }
    }
    //
    camera3_notify_msg msg;
    msg.type = CAMERA3_MSG_SHUTTER;
    msg.message.shutter.frame_number = frame_number;
    msg.message.shutter.timestamp = cbParcel.shutter->timestamp;
    mpCallbackOps->notify(mpCallbackOps, &msg);
  }
  //
  // CallbackParcel::vOutputMetaItem
  for (size_t i = 0; i < cbParcel.vOutputMetaItem.size(); i++) {
    CallbackParcel::MetaItem const& rCbMetaItem = cbParcel.vOutputMetaItem[i];
    //
    IMetadata* pMetadata = rCbMetaItem.buffer->tryReadLock(LOG_TAG);
    {
      MBOOL ret = MTRUE;
      ret = mpMetadataConverter->convertWithoutAllocate(*pMetadata, &mMetadata);

      // to debug
      {
        if (mLogLevel >= 3) {
          mpMetadataConverter->dumpAll(*pMetadata, frame_number);
        } else if (mLogLevel >= 2) {
          mpMetadataConverter->dump(*pMetadata, frame_number);
        }
      }

      MY_LOGF_IF(!ret || !mMetadata, "fail to convert metadata:%p ret:%d",
                 mMetadata, ret);
    }
    rCbMetaItem.buffer->unlock(LOG_TAG, pMetadata);
    //
    camera3_capture_result const result = camera3_capture_result{
        .frame_number = frame_number,
        .result = mMetadata,
        .num_output_buffers = 0,
        .output_buffers = NULL,
        .input_buffer = NULL,
        .partial_result = rCbMetaItem.bufferNo,
    };
    // to debug
    {
      if (rCbMetaItem.bufferNo == mAtMostMetaStreamCount) {
        MUINT64 ms64 = NSCam::Utils::getTimeInMs();
        //
        mAvgCallbackDuration += ms64 - mCallbackTime;
        mCallbackTime = ms64;
        //
        if (mFrameCounter == 0) {
          mAvgCallbackFps = ms64;
        }
        //
        mFrameCounter++;
        //
        if (mFrameCounter > mMaxFrameCount) {
          mAvgCallbackFps = ms64 - mAvgCallbackFps;
        }
      }
    }
    str += base::StringPrintf(" %s(partial#:%d)", rCbMetaItem.buffer->getName(),
                              result.partial_result);
    mpCallbackOps->process_capture_result(mpCallbackOps, &result);
  }
  //
  // CallbackParcel::vError
  for (size_t i = 0; i < cbParcel.vError.size(); i++) {
    CallbackParcel::Error const& rError = cbParcel.vError[i];
    camera3_notify_msg msg;
    msg.type = CAMERA3_MSG_ERROR;
    msg.message.error.frame_number = frame_number;
    msg.message.error.error_stream =
        (rError.stream != 0) ? rError.stream->get_camera3_stream() : NULL;
    msg.message.error.error_code = rError.errorCode;
    str += base::StringPrintf(" error_code:%d", msg.message.error.error_code);
    mpCallbackOps->notify(mpCallbackOps, &msg);
  }
  //
  // CallbackParcel::vOutputImageItem
  // CallbackParcel::vInputImageItem
  if (!cbParcel.vOutputImageItem.empty() || !cbParcel.vInputImageItem.empty()) {
    std::string s8Trace_HwComposer, s8Trace_HwTexture, s8Trace_HwVideoEncoder;
    // Output
    std::vector<camera3_stream_buffer_t> vOutBuffers;
    vOutBuffers.resize(cbParcel.vOutputImageItem.size());
    for (size_t i = 0; i < cbParcel.vOutputImageItem.size(); i++) {
      CallbackParcel::ImageItem const& rCbImageItem =
          cbParcel.vOutputImageItem[i];
      std::shared_ptr<IGraphicImageBufferHeap> pImageBufferHeap =
          rCbImageItem.buffer->getImageBufferHeap();
      //
      camera3_stream_buffer_t& rDst = vOutBuffers[i];
      rDst.stream = rCbImageItem.stream->get_camera3_stream();
      rDst.buffer = pImageBufferHeap->getBufferHandlePtr();
      rDst.status = rCbImageItem.buffer->hasStatus(STREAM_BUFFER_STATUS::ERROR)
                        ? CAMERA3_BUFFER_STATUS_ERROR
                        : CAMERA3_BUFFER_STATUS_OK;
      rDst.acquire_fence = rCbImageItem.buffer->getAcquireFence();
      rDst.release_fence = rCbImageItem.buffer->getReleaseFence();
      str += base::StringPrintf(" %s", rCbImageItem.buffer->getName());

      if (HAL_PIXEL_FORMAT_BLOB == rDst.stream->format &&
          CAMERA3_BUFFER_STATUS_OK == rDst.status) {
        MERROR err = OK;
        GrallocStaticInfo staticInfo;
        err = IGrallocHelper::singleton()->query(
            *rDst.buffer, rDst.stream->usage, &staticInfo);
        if (OK != err) {
          MY_LOGE("IGrallocHelper::query - err:%d(%s)", err, ::strerror(-err));
          return;
        }
        //
        if (pImageBufferHeap->lockBuf(
                LOG_TAG,
                GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_SW_READ_OFTEN)) {
          MINTPTR jpegBuf = pImageBufferHeap->getBufVA(0);
          size_t jpegDataSize = pImageBufferHeap->getBitstreamSize();
          size_t jpegBufSize = staticInfo.widthInPixels;

          camera3_jpeg_blob* pTransport = reinterpret_cast<camera3_jpeg_blob*>(
              jpegBuf + jpegBufSize - sizeof(camera3_jpeg_blob));
          pTransport->jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
          pTransport->jpeg_size = jpegDataSize;

          pImageBufferHeap->unlockBuf(LOG_TAG);
          MY_LOGD("jpegBuf:%#" PRIxPTR " bufsize:%d datasize:%d", jpegBuf,
                  staticInfo.widthInPixels, jpegDataSize);
        } else {
          MY_LOGE("Fail to lock jpeg");
        }
      }
      //
      cros::CameraBufferManager* cbm = cros::CameraBufferManager::GetInstance();
      MERROR status = cbm->Deregister(*pImageBufferHeap->getBufferHandlePtr());
      if (OK != status) {
        MY_LOGE("cannot Deregister from buffer_handle_t - status:%d[%s]",
                status, ::strerror(status));
        return;
      }
    }
    //
    // Input
    std::vector<camera3_stream_buffer_t> vInpBuffers;
    vInpBuffers.resize(cbParcel.vInputImageItem.size());
    for (size_t i = 0; i < cbParcel.vInputImageItem.size(); i++) {
      CallbackParcel::ImageItem const& rCbImageItem =
          cbParcel.vInputImageItem[i];
      std::shared_ptr<IGraphicImageBufferHeap> pImageBufferHeap =
          rCbImageItem.buffer->getImageBufferHeap();
      //
      camera3_stream_buffer_t& rDst = vInpBuffers[i];
      rDst.stream = rCbImageItem.stream->get_camera3_stream();
      rDst.buffer = pImageBufferHeap->getBufferHandlePtr();
      rDst.status = CAMERA3_BUFFER_STATUS_OK;
      rDst.acquire_fence = rCbImageItem.buffer->getAcquireFence();
      rDst.release_fence = rCbImageItem.buffer->getReleaseFence();
      str += base::StringPrintf(" %s", rCbImageItem.buffer->getName());
      //
      cros::CameraBufferManager* cbm = cros::CameraBufferManager::GetInstance();
      MERROR status = cbm->Deregister(*pImageBufferHeap->getBufferHandlePtr());
      if (OK != status) {
        MY_LOGE("cannot Deregister from buffer_handle_t - status:%d[%s]",
                status, ::strerror(status));
        return;
      }
    }
    //
    camera3_capture_result const result = camera3_capture_result{
        .frame_number = frame_number,
        .result = NULL,
        .num_output_buffers = (uint32_t)vOutBuffers.size(),
        .output_buffers = vOutBuffers.data(),
        .input_buffer = vInpBuffers.size() ? vInpBuffers.data() : NULL,
        .partial_result = 0,
    };
    if (!s8Trace_HwComposer.empty()) {
      CAM_TRACE_BEGIN(s8Trace_HwComposer.c_str());
    } else if (!s8Trace_HwTexture.empty()) {
      CAM_TRACE_BEGIN(s8Trace_HwTexture.c_str());
    } else if (!s8Trace_HwVideoEncoder.empty()) {
      CAM_TRACE_BEGIN(s8Trace_HwVideoEncoder.c_str());
    }
    mpCallbackOps->process_capture_result(mpCallbackOps, &result);
    if (!s8Trace_HwComposer.empty() || !s8Trace_HwTexture.empty() ||
        !s8Trace_HwVideoEncoder.empty()) {
      CAM_TRACE_END();
    }
  }
  //
  MY_LOGI("- %s", str.c_str());
}

status_t NSCam::v3::Imp::AppStreamMgr::deviceError(void) {
  camera3_notify_msg msg;
  msg.type = CAMERA3_MSG_ERROR;
  msg.message.error.error_code = CAMERA3_MSG_ERROR_DEVICE;
  MY_LOGE("@%s +", __func__);
  mpCallbackOps->notify(mpCallbackOps, &msg);
  return OK;
}
