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

#define LOG_TAG "MtkCam/P1NodeUtility"
//
#include "P1Common.h"
#include "mtkcam/pipeline/hwnode/p1/P1Utility.h"
#include <memory>
#include <string>
#include <vector>

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
namespace NSCam {
namespace v3 {
namespace NSP1Node {

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
#if MTKCAM_HAVE_SANDBOX_SUPPORT
NSCam::NSIoPipe::NSCamIOPipe::IV4L2PipeFactory* getNormalPipeModule() {
  static auto* p_v4l2_factory =
      NSCam::NSIoPipe::NSCamIOPipe::IV4L2PipeFactory::get();
  MY_LOGE_IF(!p_v4l2_factory, "IV4L2PipeFactory::get() fail");

  return p_v4l2_factory;
}
#else
INormalPipeModule* getNormalPipeModule() {
  static auto pModule = INormalPipeModule::get();
  MY_LOGE_IF(!pModule, "INormalPipeModule::get() fail");
  return pModule;
}
#endif
/******************************************************************************
 *
 ******************************************************************************/
MUINT32 getResizeMaxRatio(MUINT32 imageFormat) {
  static MUINT32 static_max_ratio = 0;
  // if need to query from NormalPipe every time, remove "static" as following
  // MUINT32 static_max_ratio = 0;
  if (static_max_ratio == 0) {
    if (auto pModule = getNormalPipeModule()) {
      NSCam::NSIoPipe::NSCamIOPipe::NormalPipe_QueryInfo info;
      pModule->query(NSCam::NSIoPipe::PORT_RRZO.index,
                     NSCam::NSIoPipe::NSCamIOPipe::ENPipeQueryCmd_BS_RATIO,
                     (EImageFormat)imageFormat, 0, &info);
      MY_LOGI("Get ENPipeQueryCmd_BS_RATIO (%d)", info.bs_ratio);
      static_max_ratio = info.bs_ratio;
    }
    if (static_max_ratio == 0) {
      MUINT32 ratio = RESIZE_RATIO_MAX_100X;
      MY_LOGI(
          "Cannot get ENPipeQueryCmd_BS_RATIO, "
          "use default ratio (%d)",
          ratio);
      return ratio;
    }
  }
  return static_max_ratio;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL calculateCropInfoFull(MUINT32 pixelMode,
                            MSize const& sensorSize,
                            MSize const& bufferSize,
                            MRect const& querySrcRect,
                            MRect* resultSrcRect,
                            MSize* resultDstSize,
                            MINT32 mLogLevelI) {
  MBOOL bSkip = MFALSE;
  if ((querySrcRect.size().w == sensorSize.w) &&
      (querySrcRect.size().h == sensorSize.h)) {
    MY_LOGI_IF((2 <= mLogLevelI), "No need to calculate");
    bSkip = MTRUE;
  }
  if ((querySrcRect.size().w > bufferSize.w ||  // cannot over buffer size
       querySrcRect.size().h > bufferSize.h) ||
      (((querySrcRect.leftTop().x + querySrcRect.size().w) > sensorSize.w) ||
       ((querySrcRect.leftTop().y + querySrcRect.size().h) > sensorSize.h))) {
    MY_LOGI_IF((2 <= mLogLevelI), "Input need to check");
    bSkip = MTRUE;
  }
  MY_LOGI_IF((3 <= mLogLevelI) || ((2 <= mLogLevelI) && bSkip),
             "[CropInfo] Input pixelMode(%d)"
             " sensorSize" P1_SIZE_STR "bufferSize" P1_SIZE_STR
             "querySrcRect" P1_RECT_STR,
             pixelMode, P1_SIZE_VAR(sensorSize), P1_SIZE_VAR(bufferSize),
             P1_RECT_VAR(querySrcRect));
  if (bSkip) {
    return MFALSE;
  }
  // TODO(MTK): query the valid value, currently do not crop in IMGO
  *resultDstSize = MSize(sensorSize.w, sensorSize.h);
  *resultSrcRect = MRect(MPoint(0, 0), *resultDstSize);
  MY_LOGI_IF((2 <= mLogLevelI),
             "Result-Full SrcRect" P1_RECT_STR "DstSize" P1_SIZE_STR,
             P1_RECT_VAR((*resultSrcRect)), P1_SIZE_VAR((*resultDstSize)));
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL calculateCropInfoResizer(MUINT32 pixelMode,
                               MUINT32 imageFormat,
                               MSize const& sensorSize,
                               MSize const& bufferSize,
                               MRect const& querySrcRect,
                               MRect* resultSrcRect,
                               MSize* resultDstSize,
                               MINT32 mLogLevelI) {
  MBOOL bSkip = MFALSE;
  if ((querySrcRect.size().w == sensorSize.w) &&
      (querySrcRect.size().h == sensorSize.h)) {
    MY_LOGI_IF((2 <= mLogLevelI), "No need to calculate");
    bSkip = MTRUE;
  } else if ((((querySrcRect.leftTop().x + querySrcRect.size().w) >
               sensorSize.w) ||
              ((querySrcRect.leftTop().y + querySrcRect.size().h) >
               sensorSize.h))) {
    MY_LOGI_IF((2 <= mLogLevelI), "Input need to check");
    bSkip = MTRUE;
  }
  MY_LOGI_IF((3 <= mLogLevelI) || ((2 <= mLogLevelI) && bSkip),
             "[CropInfo] Input pixelMode(%d) imageFormat(0x%x)"
             " sensorSize" P1_SIZE_STR "bufferSize" P1_SIZE_STR
             "querySrcRect" P1_RECT_STR,
             pixelMode, imageFormat, P1_SIZE_VAR(sensorSize),
             P1_SIZE_VAR(bufferSize), P1_RECT_VAR(querySrcRect));
  if (bSkip) {
    return MFALSE;
  }
  //
  MPoint::value_type src_crop_x = querySrcRect.leftTop().x;
  MPoint::value_type src_crop_y = querySrcRect.leftTop().y;
  MSize::value_type src_crop_w = querySrcRect.size().w;
  MSize::value_type src_crop_h = querySrcRect.size().h;
  MSize::value_type dst_size_w = 0;
  MSize::value_type dst_size_h = 0;
  // check X and W
  if (querySrcRect.size().w < bufferSize.w) {
    dst_size_w = querySrcRect.size().w;
    // check start.x
    if (auto pModule = getNormalPipeModule()) {
      NSCam::NSIoPipe::NSCamIOPipe::NormalPipe_QueryInfo info;
      pModule->query(NSCam::NSIoPipe::PORT_RRZO.index,
                     NSCam::NSIoPipe::NSCamIOPipe::ENPipeQueryCmd_CROP_START_X,
                     (EImageFormat)imageFormat, src_crop_x, &info);
      if ((MUINT32)src_crop_x != info.crop_x) {
        MY_LOGI_IF((2 <= mLogLevelI), "src_crop_x(%d) info.crop_x(%d)",
                   src_crop_x, info.crop_x);
      }
      src_crop_x = info.crop_x;
    }
    // check size.w
    if (auto pModule = getNormalPipeModule()) {
      NSCam::NSIoPipe::NSCamIOPipe::NormalPipe_QueryInfo info;
      pModule->query(
          NSCam::NSIoPipe::PORT_RRZO.index,
          NSCam::NSIoPipe::NSCamIOPipe::ENPipeQueryCmd_X_PIX |
              NSCam::NSIoPipe::NSCamIOPipe::ENPipeQueryCmd_STRIDE_BYTE,
          (EImageFormat)imageFormat, dst_size_w, &info);
      if ((MUINT32)dst_size_w != info.x_pix) {
        MY_LOGI_IF((2 <= mLogLevelI), "dst_size_w(%d) info.x_pix(%d)",
                   dst_size_w, info.x_pix);
      }
      dst_size_w = info.x_pix;
    }
    //
    MSize::value_type cur_src_crop_x = src_crop_x;
    MSize::value_type cur_src_crop_w = src_crop_w;
    MSize::value_type cur_dst_size_w = dst_size_w;
    dst_size_w = MIN(dst_size_w, sensorSize.w);
    src_crop_w = dst_size_w;
    if (src_crop_w > querySrcRect.size().w) {
      if ((src_crop_x + src_crop_w) > sensorSize.w) {
        src_crop_x = sensorSize.w - src_crop_w;
      }
    }
    if ((cur_src_crop_x != src_crop_x) || (cur_src_crop_w != src_crop_w) ||
        (cur_dst_size_w != dst_size_w)) {
      MY_LOGI_IF((2 <= mLogLevelI),
                 "ValueChanged-XW src_crop_x(%d):(%d) "
                 "src_crop_w(%d):(%d) dst_size_w(%d):(%d) sensor_w(%d)",
                 cur_src_crop_x, src_crop_x, cur_src_crop_w, src_crop_w,
                 cur_dst_size_w, dst_size_w, sensorSize.w);
    }
    MY_LOGI_IF(
        (3 <= mLogLevelI),
        "CheckXW Crop<Buf(%d<%d) Res-Src:X(%d):W(%d)-Dst:W(%d) SensorW(%d)",
        querySrcRect.size().w, bufferSize.w, src_crop_x, src_crop_w, dst_size_w,
        sensorSize.w);
  } else {
    MUINT32 ratio = getResizeMaxRatio(imageFormat);
    if (((MUINT32)src_crop_w * ratio) > ((MUINT32)bufferSize.w * 100)) {
      MY_LOGW(
          "calculateCropInfoResizer re-size width invalid "
          "(%d):(%d) @(%d)",
          src_crop_w, bufferSize.w, ratio);
      return MFALSE;
    }
    dst_size_w = bufferSize.w;
    MY_LOGI_IF(
        (3 <= mLogLevelI),
        "CheckXW Crop>Buf(%d>%d) Res-Src:X(%d):W(%d)-Dst:W(%d) SensorW(%d)",
        querySrcRect.size().w, bufferSize.w, src_crop_x, src_crop_w, dst_size_w,
        sensorSize.w);
  }
  // check Y and H
  if (querySrcRect.size().h < bufferSize.h) {
    dst_size_h = querySrcRect.size().h;
    dst_size_h = MIN(ALIGN_UPPER(dst_size_h, 2), sensorSize.h);
    src_crop_h = dst_size_h;
    if (src_crop_h > querySrcRect.size().h) {
      if ((src_crop_y + src_crop_h) > sensorSize.h) {
        MPoint::value_type cur_src_crop_y = src_crop_y;
        src_crop_y = sensorSize.h - src_crop_h;
        MY_LOGI_IF((2 <= mLogLevelI),
                   "src_crop_y(%d):(%d) "
                   "sensor_h(%d) - src_crop_h(%d)",
                   cur_src_crop_y, src_crop_y, sensorSize.h, src_crop_h);
      }
    }
    MY_LOGI_IF(
        (3 <= mLogLevelI),
        "CheckYH Crop<Buf(%d<%d) Res-Src:Y(%d):H(%d)-Dst:H(%d) SensorH(%d)",
        querySrcRect.size().h, bufferSize.h, src_crop_y, src_crop_h, dst_size_h,
        sensorSize.h);
  } else {
    MUINT32 ratio = getResizeMaxRatio(imageFormat);
    if (((MUINT32)src_crop_h * ratio) > ((MUINT32)bufferSize.h * 100)) {
      MY_LOGW(
          "calculateCropInfoResizer re-size height invalid "
          "(%d):(%d) @(%d)",
          src_crop_h, bufferSize.h, ratio);
      return MFALSE;
    }
    dst_size_h = bufferSize.h;
    MY_LOGI_IF(
        (3 <= mLogLevelI),
        "CheckYH Crop>Buf(%d>%d) Res-Src:Y(%d):H(%d)-Dst:H(%d) SensorH(%d)",
        querySrcRect.size().h, bufferSize.h, src_crop_y, src_crop_h, dst_size_h,
        sensorSize.h);
  }
  *resultDstSize = MSize(dst_size_w, dst_size_h);
  *resultSrcRect =
      MRect(MPoint(src_crop_x, src_crop_y), MSize(src_crop_w, src_crop_h));
  MY_LOGI_IF((2 <= mLogLevelI),
             "Result-Resize SrcRect" P1_RECT_STR "DstSize" P1_SIZE_STR,
             P1_RECT_VAR((*resultSrcRect)), P1_SIZE_VAR((*resultDstSize)));
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL verifySizeResizer(MUINT32 pixelMode,
                        MUINT32 imageFormat,
                        MSize const& sensorSize,
                        MSize const& streamBufSize,
                        MSize const& queryBufSize,
                        MSize* resultBufSize,
                        MINT32 mLogLevelI) {
  MY_LOGI_IF((3 <= mLogLevelI),
             "[CropInfo] +++ pixelMode(%d) imageFormat(0x%x)"
             " sensor" P1_SIZE_STR "streamBuf" P1_SIZE_STR
             "queryBuf" P1_SIZE_STR "resultBuf" P1_SIZE_STR,
             pixelMode, imageFormat, P1_SIZE_VAR(sensorSize),
             P1_SIZE_VAR(streamBufSize), P1_SIZE_VAR(queryBufSize),
             P1_SIZE_VAR((*resultBufSize)));
  //
  *resultBufSize = streamBufSize;
  //
  // check origin stream buffer size
  if (queryBufSize.w > streamBufSize.w || queryBufSize.h > streamBufSize.h) {
    MY_LOGW("[CropInfo] MTK_P1NODE_RESIZER_SET_SIZE" P1_SIZE_STR
            " > "
            "STREAM_BUF_SIZE" P1_SIZE_STR
            " : ignore-MTK_P1NODE_RESIZER_SET_SIZE"
            " use-stream_buffer_size" P1_SIZE_STR,
            P1_SIZE_VAR(queryBufSize), P1_SIZE_VAR(streamBufSize),
            P1_SIZE_VAR(streamBufSize));
    return MFALSE;
  }
  //
  // check size.w and size.h should be even
  if (((((MUINT32)queryBufSize.w) & ((MUINT32)0x1)) > 0) ||
      ((((MUINT32)queryBufSize.h) & ((MUINT32)0x1)) > 0)) {
    MY_LOGW("[CropInfo] MTK_P1NODE_RESIZER_SET_SIZE" P1_SIZE_STR
            " != Even"
            " : ignore-MTK_P1NODE_RESIZER_SET_SIZE"
            " use-stream_buffer_size" P1_SIZE_STR,
            P1_SIZE_VAR(queryBufSize), P1_SIZE_VAR(streamBufSize));
    return MFALSE;
  }
  //
  // check size.w alignment limitation
  if (auto pModule = getNormalPipeModule()) {
    MSize::value_type dst_size_w = queryBufSize.w;
    NSCam::NSIoPipe::NSCamIOPipe::NormalPipe_QueryInfo info;
    pModule->query(NSCam::NSIoPipe::PORT_RRZO.index,
                   NSCam::NSIoPipe::NSCamIOPipe::ENPipeQueryCmd_X_PIX |
                       NSCam::NSIoPipe::NSCamIOPipe::ENPipeQueryCmd_STRIDE_BYTE,
                   (EImageFormat)imageFormat, dst_size_w, &info);
    if ((MUINT32)dst_size_w != info.x_pix) {
      MY_LOGW("[CropInfo] MTK_P1NODE_RESIZER_SET_SIZE" P1_SIZE_STR
              " size_w(%d) != x_pix(%d)"
              " : ignore-MTK_P1NODE_RESIZER_SET_SIZE"
              " use-stream_buffer_size" P1_SIZE_STR,
              P1_SIZE_VAR(queryBufSize), dst_size_w, info.x_pix,
              P1_SIZE_VAR(streamBufSize));
      return MFALSE;
    }
  }
  //
  // check size.w and size.h ratio limitation
  {
    MUINT32 ratio = getResizeMaxRatio(imageFormat);
    if ((((MUINT32)queryBufSize.w * 100) < ((MUINT32)sensorSize.w * ratio)) ||
        (((MUINT32)queryBufSize.h * 100) < ((MUINT32)sensorSize.h * ratio))) {
      MY_LOGW("[CropInfo] MTK_P1NODE_RESIZER_SET_SIZE" P1_SIZE_STR
              " < "
              "SensorSize" P1_SIZE_STR
              "x Ratio(0.%d) "
              " : ignore-MTK_P1NODE_RESIZER_SET_SIZE"
              " use-stream_buffer_size" P1_SIZE_STR,
              P1_SIZE_VAR(queryBufSize), P1_SIZE_VAR(sensorSize), ratio,
              P1_SIZE_VAR(streamBufSize));
      return MFALSE;
    }
  }
  //
  *resultBufSize = queryBufSize;
  MY_LOGI_IF((3 <= mLogLevelI),
             "[CropInfo] --- pixelMode(%d) imageFormat(0x%x)"
             " sensor" P1_SIZE_STR "streamBuf" P1_SIZE_STR
             "queryBuf" P1_SIZE_STR "resultBuf" P1_SIZE_STR,
             pixelMode, imageFormat, P1_SIZE_VAR(sensorSize),
             P1_SIZE_VAR(streamBufSize), P1_SIZE_VAR(queryBufSize),
             P1_SIZE_VAR((*resultBufSize)));
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
void queryRollingSkew(MUINT const openId,
                      MINT64* nsRolling,
                      MINT32 mLogLevelI) {
  MINT32 rolling = 0;
  std::shared_ptr<NSCam::IHalSensor> pSensorHalObj;
  auto pHalSensorList = GET_HalSensorList();
  auto deleter = [&](IHalSensor* p_halsensor) {
    if (CC_LIKELY(p_halsensor != nullptr)) {
      p_halsensor->destroyInstance(LOG_TAG);
    }
  };
  pSensorHalObj.reset(pHalSensorList->createSensor(LOG_TAG, openId), deleter);
  MINT res = pSensorHalObj->sendCommand(
      pHalSensorList->querySensorDevIdx(openId),
      SENSOR_CMD_GET_SENSOR_ROLLING_SHUTTER, (MUINTPTR)(&rolling),
      sizeof(MINT32), (MUINTPTR)(0), sizeof(MINT32), (MUINTPTR)(0),
      sizeof(MINT32));
  MY_LOGD("rolling:%d", rolling);
  if (res) {
    *nsRolling = rolling;
  }
}

/******************************************************************************
 *
 ******************************************************************************/
void generateMetaInfoStr(IMetadata::IEntry const& entry, std::string* string) {
  base::StringAppendF(string, "[TAG:0x%X _%d #%d]={ ", entry.tag(),
                      entry.type(), entry.count());

  typedef IMetadata::Memory Memory;

#define P1_FMT_MUINT8(v) "%d ", v
#define P1_FMT_MINT32(v) "%d ", v
#define P1_FMT_MINT64(v) "%" PRId64 " ", v
#define P1_FMT_MFLOAT(v) "%f ", v
#define P1_FMT_MDOUBLE(v) "%lf ", v
#define P1_FMT_MPoint(v) "%d,%d ", v.x, v.y
#define P1_FMT_MSize(v) "%dx%d ", v.w, v.h
#define P1_FMT_MRect(v) "%d,%d_%dx%d ", v.p.x, v.p.y, v.s.w, v.s.h
#define P1_FMT_MRational(v) "%d:%d ", v.numerator, v.denominator
#define P1_FMT_Memory(v) "[%zu] ", v.size()
#define P1_META_CASE_STR(T)                           \
  case TYPE_##T: {                                    \
    for (size_t i = 0; i < entry.count(); i++) {      \
      T value = entry.itemAt(i, Type2Type<T>());      \
      base::StringAppendF(string, P1_FMT_##T(value)); \
    }                                                 \
  }; break;

  switch (entry.type()) {
    P1_META_CASE_STR(MUINT8);
    P1_META_CASE_STR(MINT32);
    P1_META_CASE_STR(MINT64);
    P1_META_CASE_STR(MFLOAT);
    P1_META_CASE_STR(MDOUBLE);
    P1_META_CASE_STR(MPoint);
    P1_META_CASE_STR(MSize);
    P1_META_CASE_STR(MRect);
    P1_META_CASE_STR(MRational);
    P1_META_CASE_STR(Memory);
    case TYPE_IMetadata:
      base::StringAppendF(string, "metadata ... ");
      break;
    default:
      base::StringAppendF(string, "UNKNOWN_%d", entry.type());
      break;
  }

#undef P1_FMT_MUINT8
#undef P1_FMT_MINT32
#undef P1_FMT_MINT64
#undef P1_FMT_MFLOAT
#undef P1_FMT_MDOUBLE
#undef P1_FMT_MPoint
#undef P1_FMT_MSize
#undef P1_FMT_MRect
#undef P1_FMT_MRational
#undef P1_META_CASE_STR

  base::StringAppendF(string, "} ");
}

/******************************************************************************
 *
 ******************************************************************************/
void logMeta(MINT32 option,
             IMetadata const* pMeta,
             char const* pInfo,
             MUINT32 tag) {
  if (option <= 0) {
    return;
  }
  if ((pMeta == nullptr) || (pInfo == nullptr)) {
    return;
  }
  MUINT32 numPerLine = option;
  MUINT32 cnt = 0;
  MUINT32 end = 0;
  MBOOL found = MFALSE;
  std::string str("");
  if (pMeta->count() == 0) {
    str.clear();
    str += base::StringPrintf("%s metadata.count(0)", pInfo);
    if (tag > 0) {
      str += base::StringPrintf(" - while find MetaTag[0x%X=%d]", tag, tag);
    }
    MY_LOGI("%s", str.c_str());
    str.clear();
    return;
  }
  for (MUINT32 i = 0; i < pMeta->count(); i++) {
    if (tag != 0) {
      if (tag == pMeta->entryAt(i).tag()) {
        found = MTRUE;
        str.clear();
        str += base::StringPrintf("%s Found-MetaTag[0x%X=%d] ", pInfo,
                                  pMeta->entryAt(i).tag(), tag);
        generateMetaInfoStr(pMeta->entryAt(i), &str);
        MY_LOGI("%s", str.c_str());
        break;
      }
      continue;
    }
    //
    if (cnt == 0) {
      end = ((i + numPerLine - 1) < (pMeta->count() - 1))
                ? (i + numPerLine - 1)
                : (pMeta->count() - 1);
      str.clear();
      str += base::StringPrintf("%s [%03d~%03d/%03d] ", pInfo, i, end,
                                pMeta->count());
    }
    generateMetaInfoStr(pMeta->entryAt(i), &str);
    cnt++;
    if (i == end) {
      cnt = 0;
      MY_LOGI("%s", str.c_str());
    }
  }
  if ((tag != 0) && (!found)) {
    MY_LOGI("%s NotFound-MetaTag[0x%X=%d]", pInfo, tag, tag);
  }
  str.clear();
  return;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  StuffBufferPool Implementation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
StuffBufferPool::compareLayout(MINT32 format,
                               MSize size,
                               MUINT32 stride0,
                               MUINT32 stride1,
                               MUINT32 stride2) {
  return ((format == mFormat) && (stride0 == mStride0) &&
          (stride1 == mStride1) && (stride2 == mStride2) && (size == mSize));
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
StuffBufferPool::acquireBuffer(std::shared_ptr<IImageBuffer>* imageBuffer) {
  FUNCTION_IN;
  //
  MERROR ret = OK;
  std::shared_ptr<IImageBuffer> pImgBuf = nullptr;
  BufNote bufNote;
  size_t i = 0;
  *imageBuffer = nullptr;
  //
  for (auto& it : mvInfoMap) {
    bufNote = it.second;
    if (BUF_STATE_RELEASED == bufNote.mState) {
      std::shared_ptr<IImageBuffer> const pImageBuffer = it.first;
      bufNote.mState = BUF_STATE_ACQUIRED;
      it.second = bufNote;
      pImgBuf = pImageBuffer;
      break;
    }
  }
  //
  if (pImgBuf != nullptr) {
    MY_LOGD("Acquire Stuff Buffer (%s) index(%zu) (%zu/%d)",
            bufNote.msName.c_str(), i, mvInfoMap.size(), mWaterMark);
    mUsage |= GRALLOC_USAGE_SW_WRITE_OFTEN;
    mUsage |= GRALLOC_USAGE_SW_READ_OFTEN;
    if (!(pImgBuf->lockBuf(bufNote.msName.c_str(), mUsage))) {
      MY_LOGE("[%s] Stuff ImgBuf lock fail", bufNote.msName.c_str());
      return BAD_VALUE;
    }
    *imageBuffer = pImgBuf;
    return OK;
  }
  //
  MY_LOGD("StuffBuffer-Acquire (NoAvailable) (%zu/%d)", mvInfoMap.size(),
          mWaterMark);
  //
  ret = createBuffer(imageBuffer);
  FUNCTION_OUT;
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
StuffBufferPool::releaseBuffer(std::shared_ptr<IImageBuffer>* imageBuffer) {
  FUNCTION_IN;
  //
  MERROR ret = OK;
  if (*imageBuffer == nullptr) {
    MY_LOGW("Stuff ImageBuffer not exist");
    return BAD_VALUE;
  }
  auto it_mvInfoMap = mvInfoMap.find(*imageBuffer);
  if (it_mvInfoMap == mvInfoMap.end()) {
    MY_LOGW("ImageBuffer(%p) not found (%zu)", imageBuffer->get(),
            mvInfoMap.size());
    return BAD_VALUE;
  }
  (*imageBuffer)->unlockBuf(it_mvInfoMap->second.msName.c_str());
  BufNote bufNote = it_mvInfoMap->second;
  bufNote.mState = BUF_STATE_RELEASED;
  it_mvInfoMap->second = bufNote;
  //
  if (mvInfoMap.size() > mWaterMark) {
    ssize_t index = std::distance(mvInfoMap.begin(), it_mvInfoMap);
    ret = destroyBuffer(index);
  }
  //
  MY_LOGD("StuffBuffer-Release (%s) (%zu/%d)", bufNote.msName.c_str(),
          mvInfoMap.size(), mWaterMark);
  //
  FUNCTION_OUT;
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
StuffBufferPool::createBuffer(std::shared_ptr<IImageBuffer>* imageBuffer) {
  FUNCTION_IN;
  //
  *imageBuffer = nullptr;
  // add information to buffer name
  std::string imgBufName = std::string(msName);
  char str[256] = {0};
  snprintf(str, sizeof(str), ":Size%dx%d:Stride%d.%d.%d:Sn%d", mSize.w, mSize.h,
           mStride0, mStride1, mStride2, ++mSerialNum);
  imgBufName += str;
  //
  if (mvInfoMap.size() >= mMaxAmount) {
    MY_LOGW(
        "[%s] the pool size is over max amount, "
        "please check the buffer usage and situation (%zu/%d)",
        imgBufName.c_str(), mvInfoMap.size(), mMaxAmount);
    return NO_MEMORY;
  }
  // create buffer
  MINT32 bufBoundaryInBytes[3] = {0, 0, 0};
  MUINT32 bufStridesInBytes[3] = {mStride0, mStride1, mStride2};
  if (mPlaneCnt == 0) {  // ref. StuffBufferPool::CTR mStride0/1/2 checking
    MY_LOGE("[%s] Stuff ImageBufferHeap stride invalid (%d.%d.%d)",
            imgBufName.c_str(), mStride0, mStride1, mStride2);
    return BAD_VALUE;
  }
  IImageBufferAllocator::ImgParam imgParam = IImageBufferAllocator::ImgParam(
      (EImageFormat)mFormat, mSize, bufStridesInBytes, bufBoundaryInBytes,
      (size_t)mPlaneCnt);

  std::shared_ptr<IGbmImageBufferHeap> pHeap =
      IGbmImageBufferHeap::create(imgBufName.c_str(), imgParam);
  if (pHeap == nullptr) {
    MY_LOGE("[%s] Stuff ImageBufferHeap create fail", imgBufName.c_str());
    return BAD_VALUE;
  }
  MINT reqImgFormat = pHeap->getImgFormat();
  ImgBufCreator creator(reqImgFormat);
  std::shared_ptr<IImageBuffer> pImgBuf = pHeap->createImageBuffer(&creator);
  if (pImgBuf == nullptr) {
    MY_LOGE("[%s] Stuff ImageBuffer create fail", imgBufName.c_str());
    return BAD_VALUE;
  }
  // lock buffer
  mUsage |= GRALLOC_USAGE_SW_WRITE_OFTEN;
  mUsage |= GRALLOC_USAGE_SW_READ_OFTEN;
  if (!(pImgBuf->lockBuf(imgBufName.c_str(), mUsage))) {
    MY_LOGE("[%s] Stuff ImageBuffer lock fail", imgBufName.c_str());
    return BAD_VALUE;
  }
  BufNote bufNote(imgBufName, BUF_STATE_ACQUIRED);
  mvInfoMap.emplace(pImgBuf, bufNote);
  *imageBuffer = pImgBuf;
  //
  MY_LOGD(
      "StuffBuffer-Create (%s) (%zu/%d) "
      "ImgBuf(%p)(0x%X)(%dx%d,%zu,%zu)(P:0x%zx)(V:0x%zx)",
      imgBufName.c_str(), mvInfoMap.size(), mWaterMark, imageBuffer->get(),
      (*imageBuffer)->getImgFormat(), (*imageBuffer)->getImgSize().w,
      (*imageBuffer)->getImgSize().h, (*imageBuffer)->getBufStridesInBytes(0),
      (*imageBuffer)->getBufSizeInBytes(0), (*imageBuffer)->getBufPA(0),
      (*imageBuffer)->getBufVA(0));
  //
  FUNCTION_OUT;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
StuffBufferPool::destroyBuffer(std::shared_ptr<IImageBuffer>* imageBuffer) {
  FUNCTION_IN;
  //
  MERROR ret = OK;
  if (*imageBuffer == nullptr) {
    MY_LOGW("Stuff ImageBuffer not exist");
    return BAD_VALUE;
  }
  //
  auto it_mvInfoMap = mvInfoMap.find(*imageBuffer);
  if (it_mvInfoMap == mvInfoMap.end()) {
    MY_LOGW("ImageBuffer(%p) not found (%zu)", imageBuffer->get(),
            mvInfoMap.size());
    return BAD_VALUE;
  }
  ssize_t index = std::distance(mvInfoMap.begin(), it_mvInfoMap);
  ret = destroyBuffer(index);
  FUNCTION_OUT;
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
StuffBufferPool::destroyBuffer(size_t index) {
  FUNCTION_IN;
  //
  if (index >= mvInfoMap.size()) {
    MY_LOGW("index(%zu) not exist, size(%zu)", index, mvInfoMap.size());
    return BAD_VALUE;
  }
  auto it_mvInfoMap = mvInfoMap.begin();
  for (; it_mvInfoMap != mvInfoMap.end(); ++it_mvInfoMap) {
    if (std::distance(mvInfoMap.begin(), it_mvInfoMap) == index) {
      break;
    }
  }
  BufNote bufNote = it_mvInfoMap->second;
  std::shared_ptr<IImageBuffer> const pImageBuffer = it_mvInfoMap->first;
  MY_LOGD(
      "StuffBuffer-Destroy (%s) index(%zu) state(%d) "
      "(%zu/%d)",
      bufNote.msName.c_str(), index, bufNote.mState, mvInfoMap.size(),
      mWaterMark);
  if (bufNote.mState == BUF_STATE_ACQUIRED) {
    std::shared_ptr<IImageBuffer> pImgBuf = pImageBuffer;
    pImgBuf->unlockBuf(bufNote.msName.c_str());
  }
  // destroy buffer
  mvInfoMap.erase(it_mvInfoMap);
  FUNCTION_OUT;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
StuffBufferManager::collectBufferInfo(MUINT32 pixelMode,
                                      MBOOL isFull,
                                      MINT32 format,
                                      MSize size,
                                      std::vector<MUINT32>* stride) {
  FUNCTION_IN;
  //
  std::lock_guard<std::mutex> _l(mLock);
  //
  MY_LOGI("pixel-mode-%d full:%d format[x%x] size(%dx%d)", pixelMode, isFull,
          format, size.w, size.h);
  MBOOL found = MFALSE;
  stride->clear();
  std::vector<InfoSet>::iterator it = mvInfoSet.begin();
  for (; it != mvInfoSet.end(); it++) {
    if ((it->mFormat == format) && (it->mSize == size)) {
      *stride = it->mvStride;
      found = MTRUE;
      break;
    }
  }
  if (found == MFALSE) {  // add new InfoSet by the querying from DRV
    InfoSet addInfoSet(mOpenId, mLogLevel, mLogLevelI);
    addInfoSet.mFormat = format;
    addInfoSet.mSize =
        size;  // save the size here, the size might be changed by HwInfoHelper
    //
    NSCamHW::HwInfoHelper helper(mOpenId);
    switch (format) {  // for UFO case
      case eImgFmt_UFO_BAYER8:
      case eImgFmt_UFO_BAYER10:
      case eImgFmt_UFO_BAYER12:
      case eImgFmt_UFO_BAYER14:
      case eImgFmt_UFO_FG_BAYER8:
      case eImgFmt_UFO_FG_BAYER10:
      case eImgFmt_UFO_FG_BAYER12:
      case eImgFmt_UFO_FG_BAYER14: {
        size_t ufoStride[3] = {0};
        if (!helper.queryUFOStride(format, size, ufoStride)) {
          MY_LOGE("QueryUFOStride - FAIL(%d-%d)[x%x](%dx%d)", pixelMode, isFull,
                  format, size.w, size.h);
          return BAD_VALUE;
        }
        MY_LOGI(
            "add-BufInfoSet(%d)[%d][x%x](%dx%d)-"
            "(%dx%d)(%zu,%zu,%zu)",
            pixelMode, isFull, format, size.w, size.h, addInfoSet.mSize.w,
            addInfoSet.mSize.h, ufoStride[0], ufoStride[1], ufoStride[2]);
        addInfoSet.mvStride.push_back(ufoStride[0]);
        addInfoSet.mvStride.push_back(ufoStride[1]);
        addInfoSet.mvStride.push_back(ufoStride[2]);
      } break;
      default: {  // IMGO/RRZO with non-UFO
        size_t stride = 0;
        if (!helper.alignPass1HwLimitation(pixelMode, format, isFull, &size,
                                           &stride)) {
          MY_LOGE("QueryBufferInfo - FAIL(%d-%d)[x%x](%dx%d)", pixelMode,
                  isFull, format, size.w, size.h);
          return BAD_VALUE;
        }  // not replace the size
        MY_LOGI("add-BufInfoSet(%d)[%d][x%x](%dx%d)-(%dx%d)(%zu)", pixelMode,
                isFull, format, size.w, size.h, addInfoSet.mSize.w,
                addInfoSet.mSize.h, stride);
        if (size.w != addInfoSet.mSize.w) {
          if (auto pModule = getNormalPipeModule()) {
            NSCam::NSIoPipe::NSCamIOPipe::NormalPipe_QueryInfo queryRst;
            NSCam::NSIoPipe::NSCamIOPipe::NormalPipe_QueryIn input;
            input.width = addInfoSet.mSize.w;
            pModule->query(
                isFull ? NSCam::NSIoPipe::PORT_IMGO.index
                       : NSCam::NSIoPipe::PORT_RRZO.index,
                NSCam::NSIoPipe::NSCamIOPipe::ENPipeQueryCmd_STRIDE_BYTE,
                format, input, &queryRst);
            stride = queryRst.stride_byte;
            MY_LOGI(
                "add-BufInfoSet(%d)[%d][x%x]-(%dx%d) "
                "Get ENPipeQueryCmd_STRIDE_BYTE(%zu)",
                pixelMode, isFull, format, addInfoSet.mSize.w,
                addInfoSet.mSize.h, stride);
          } else {
            MY_LOGE("CANNOT getNormalPipeModule");
            return BAD_VALUE;
          }
        }
        addInfoSet.mvStride.push_back(stride);
      } break;
    }
    *stride = addInfoSet.mvStride;
    mvInfoSet.push_back(addInfoSet);
  }
  //
  FUNCTION_OUT;
  return OK;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  StuffBufferManager Implementation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
MERROR
StuffBufferManager::acquireStoreBuffer(
    std::shared_ptr<IImageBuffer>* imageBuffer,
    char const* szName,
    MINT32 format,
    MSize size,
    std::vector<MUINT32> vStride,
    MUINT8 multiple,
    MBOOL writable) {
  FUNCTION_IN;
  //
  //
  std::lock_guard<std::mutex> _l(mLock);
  MERROR ret = OK;
  //
  std::shared_ptr<StuffBufferPool> bufPool = nullptr;
  *imageBuffer = nullptr;

  //
  MUINT32 stride[3] = {0, 0, 0};
  size_t count = vStride.size();
  if (count > 3) {
    MY_LOGW("Fmt:0x%x (%dx%d) Cnt(%zu)", format, size.w, size.h, count);
    count = 3;
  }
  for (size_t i = 0; i < count; i++) {
    stride[i] = vStride[i];
  }
  //
  std::vector<std::shared_ptr<StuffBufferPool> >::iterator it =
      mvPoolSet.begin();
  for (; it != mvPoolSet.end(); it++) {
    std::shared_ptr<StuffBufferPool> sp = (*it);
    if ((sp != nullptr) &&
        sp->compareLayout(format, size, stride[0], stride[1], stride[2])) {
      bufPool = sp;
      break;
    }
  }
  //
  if (bufPool == nullptr) {
    auto newPool = std::make_shared<StuffBufferPool>(
        szName, format, size, stride[0], stride[1], stride[2], multiple,
        writable, mLogLevel, mLogLevelI);
    mvPoolSet.push_back(newPool);
    it = mvPoolSet.end();
    bufPool = *(it - 1);
    MY_LOGD("PoolSet.size(%zu)", mvPoolSet.size());
  }
  //
  if (bufPool == nullptr) {
    MY_LOGE("Cannot create stuff buffer pool");
    return BAD_VALUE;
  } else {
    ret = bufPool->acquireBuffer(imageBuffer);
  }
  //
  //
  FUNCTION_OUT;
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
StuffBufferManager::releaseStoreBuffer(
    std::shared_ptr<IImageBuffer>* imageBuffer) {
  FUNCTION_IN;
  //
  std::lock_guard<std::mutex> _l(mLock);
  //
  if (*imageBuffer == nullptr) {
    MY_LOGW("Stuff ImageBuffer not exist");
    return BAD_VALUE;
  }
  //
  MINT const format = (*imageBuffer)->getImgFormat();
  MSize const size = (*imageBuffer)->getImgSize();
  MUINT32 stride[3] = {0, 0, 0};
  size_t count = (*imageBuffer)->getPlaneCount();
  if (count > 3) {
    MY_LOGW("ImageBuffer Fmt:0x%x (%dx%d) PlaneCount(%zu)",
            (*imageBuffer)->getImgFormat(), (*imageBuffer)->getImgSize().w,
            (*imageBuffer)->getImgSize().h, count);
    count = 3;
  }
  for (size_t i = 0; i < count; i++) {
    stride[i] = (*imageBuffer)->getBufStridesInBytes(i);
  }
  //
  std::shared_ptr<StuffBufferPool> bufPool = nullptr;
  std::vector<std::shared_ptr<StuffBufferPool> >::iterator it =
      mvPoolSet.begin();
  for (; it != mvPoolSet.end(); it++) {
    std::shared_ptr<StuffBufferPool> sp = (*it);
    if ((sp != nullptr) &&
        sp->compareLayout(format, size, stride[0], stride[1], stride[2])) {
      bufPool = sp;
      break;
    }
  }
  //
  if (bufPool == nullptr) {
    MY_LOGE("Cannot find stuff buffer pool");
    return BAD_VALUE;
  } else {
    bufPool->releaseBuffer(imageBuffer);
  }
  //
  FUNCTION_OUT;
  return OK;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  TimingChecker Implementation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
void TimingChecker::Client::action(void) {
  std::lock_guard<std::mutex> _l(mLock);
  switch (mType) {
    case EVENT_TYPE_WARNING:
      MY_LOGW(
          "[TimingChecker-W] [%s] (%dms) "
          "= ( %" PRId64 " - %" PRId64 " ns)",
          mStr.c_str(), mTimeInvMs, mEndTsNs, mBeginTsNs);
      break;
    case EVENT_TYPE_ERROR:
      MY_LOGE(
          "[TimingChecker-E] [%s] (%dms) "
          "= ( %" PRId64 " - %" PRId64 " ns)",
          mStr.c_str(), mTimeInvMs, mEndTsNs, mBeginTsNs);
      break;
    case EVENT_TYPE_FATAL:
      MY_LOGF(
          "[TimingChecker-F] [%s] (%dms) "
          "= ( %" PRId64 " - %" PRId64 " ns)",
          mStr.c_str(), mTimeInvMs, mEndTsNs, mBeginTsNs);
      // AEE trigger
      break;
    default:  // EVENT_TYPE_NONE:
      // do nothing
      break;
  }
}

/******************************************************************************
 *
 ******************************************************************************/
/*void
TimingChecker::Client::onLastStrongRef(const void* ) {
    dump("TC_Client::onLastStrongRef");
};*/

/******************************************************************************
 *
 ******************************************************************************/
void TimingChecker::Client::dump(char const* tag) {
  std::lock_guard<std::mutex> _l(mLock);
  if (mLogLevelI >= 2) {
    char const* str = (tag != nullptr) ? tag : "nullptr";
    MY_LOGI(
        "[%s][%s] (%dms) = "
        "( %" PRId64 " - %" PRId64 " ns)",
        str, mStr.c_str(), mTimeInvMs, mEndTsNs, mBeginTsNs);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
size_t TimingChecker::RecStore::size(void) {
  return mHeap.size();
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
TimingChecker::RecStore::isEmpty(void) {
  return mHeap.empty();
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
TimingChecker::RecStore::addRec(RecPtr rp) {
  if (rp == nullptr) {
    return MFALSE;
  }
  mHeap.push(rp);
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
TimingChecker::RecPtr const& TimingChecker::RecStore::getMin(void) {
  return mHeap.top();
}

/******************************************************************************
 *
 ******************************************************************************/
void TimingChecker::RecStore::delMin(void) {
  mHeap.pop();
}

/******************************************************************************
 *
 ******************************************************************************/
void TimingChecker::RecStore::dump(char const* tag) {
  const RecPtr* pTop = reinterpret_cast<const RecPtr*>(&(mHeap.top()));
  MY_LOGI("RecPtrHeap @ %s", (tag != nullptr) ? tag : "NULL");
  MY_LOGI(
      "RecPtrHeap[0/%zu]@(%p) = (%p) "
      "( %" PRId64 " ns)",
      mHeap.size(), (void*)(pTop), (void*)(*pTop), (*pTop)->mTimeMarkNs);
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
TimingChecker::doThreadLoop(void) {
  std::unique_lock<std::mutex> _l(mLock);
  // mData.clear();
  mWakeTiming = 0;
  mExitPending = MFALSE;
  mRunning = MTRUE;
  mEnterCond.notify_all();
  /*
      For less affecting, the TimingChecker caller
      might not wait for this thread loop ready.
      Hence, it checks the current time with the
      registered client's timing mark directly.
  */
  while (!mExitPending) {
    int64_t current = NSCam::Utils::getTimeInNs();
    if (mWakeTiming <= current) {
      mWakeTiming = checkList(current);
      //
      if (mWakeTiming == 0) {
        mClientCond.wait(_l);
      }
      continue;
    }
    //
    int64_t sleep = mWakeTiming - current;
    mClientCond.wait_for(_l, std::chrono::nanoseconds(sleep));
  }
  mRunning = MFALSE;
  mExitedCond.notify_all();
  return MFALSE;
}

/******************************************************************************
 *
 ******************************************************************************/
void TimingChecker::doRequestExit(void) {
  std::unique_lock<std::mutex> _l(mLock);
  mWakeTiming = 0;
  mExitPending = MTRUE;
  mEnterCond.notify_all();
  mClientCond.notify_all();
  // join loop
  while (mRunning) {
    mExitedCond.wait_for(_l, std::chrono::nanoseconds(ONE_MS_TO_NS));
  }
  // clear data
  while (!mData.isEmpty()) {
    std::shared_ptr<TimingChecker::Client> c = mData.getMin()->mpClient.lock();
    if (c != nullptr) {
      c->setLog(mOpenId, mLogLevel, mLogLevelI);
      c->dump("RecordStoreCleaning");
    }
    delete (mData.getMin());
    mData.delMin();
  }
}

/******************************************************************************
 *
 ******************************************************************************/
void TimingChecker::doWaitReady(void) {
  std::unique_lock<std::mutex> _l(mLock);
  while (!mRunning && !mExitPending) {
    mEnterCond.wait_for(_l, std::chrono::nanoseconds(ONE_MS_TO_NS));
  }
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<TimingChecker::Client> TimingChecker::createClient(
    char const* str, MUINT32 uTimeoutMs, EVENT_TYPE eType) {
  std::shared_ptr<TimingChecker::Client> client =
      std::make_shared<TimingChecker::Client>(str, uTimeoutMs, eType);
  if (client == nullptr) {
    MY_LOGE(
        "CANNOT create TimingCheckerClient "
        "[%s]",
        str);
    return nullptr;
  }
  {
    std::lock_guard<std::mutex> _l(mLock);
    int64_t ts = client->getTimeStamp();
    RecPtr pRec = new Record(ts, client);
    if (pRec == nullptr || !(mData.addRec(pRec))) {
      MY_LOGE("CANNOT new Record");
      client = nullptr;
      return nullptr;
    }
    if (mWakeTiming == 0 || ts < mWakeTiming) {
      mClientCond.notify_all();
    }
  }
  return client;
}

/******************************************************************************
 *
 ******************************************************************************/
int64_t TimingChecker::checkList(int64_t time) {
  int64_t ts = 0;
  RecPtr pRec = nullptr;
  while (!mData.isEmpty()) {
    pRec = mData.getMin();
    if (pRec == nullptr) {
      MY_LOGE("Error Record in Store");
      ts = 0;
      break;
    }
    ts = pRec->mTimeMarkNs;
    if (ts > time) {
      break;
    }
    auto c = pRec->mpClient.lock();
    if (c != nullptr) {
      c->action();
    }
    delete pRec;
    mData.delMin();
    ts = 0;
  }
  return ts;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  TimingCheckerMgr Implementation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
void TimingCheckerMgr::setEnable(MBOOL en) {
  if (mpTimingChecker == nullptr) {
    std::lock_guard<std::mutex> _l(mLock);
    mIsEn = MFALSE;
    return;
  }
  MY_LOGD("TimingChecker enable(%d)", en);
  {
    std::lock_guard<std::mutex> _l(mLock);
    mIsEn = en;
    if (mIsEn) {
      return;
    }
  }
  // as (mIsEn == false)
  mpTimingChecker->doRequestExit();
}

/******************************************************************************
 *
 ******************************************************************************/
void TimingCheckerMgr::waitReady(void) {
  if (mpTimingChecker == nullptr) {
    return;
  }
  // no waiting for less affecting
  // mpTimingChecker->doWaitReady();
}

/******************************************************************************
 *
 ******************************************************************************/
void TimingCheckerMgr::onCheck(void) {
  if (mpTimingChecker == nullptr) {
    return;
  }
  {
    std::lock_guard<std::mutex> _l(mLock);
    if (!mIsEn) {
      return;
    }
  }
  // as (mIsEn == true)
  if (mpTimingChecker->doThreadLoop()) {
    MY_LOGD("TimingChecker next loop");
  }
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<TimingChecker::Client> TimingCheckerMgr::createClient(
    char const* str, MUINT32 uTimeoutMs, TimingChecker::EVENT_TYPE eType) {
  if (mpTimingChecker == nullptr) {
    return nullptr;
  }
  return mpTimingChecker->createClient(str, uTimeoutMs * mFactor, eType);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  LongExposureStatus Implementation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
void LongExposureStatus::config(MINT32 nOpenId,
                                MINT32 nLogLevel,
                                MINT32 nLogLevelI) {
  std::lock_guard<std::mutex> _l(mLock);
  mOpenId = nOpenId;
  mLogLevel = nLogLevel;
  mLogLevelI = nLogLevelI;
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
LongExposureStatus::reset(MINT num) {
  std::lock_guard<std::mutex> _l(mLock);
  if (mvSet.empty()) {
    return MFALSE;
  }
  std::vector<MINT32>::iterator it = mvSet.begin();
  for (; it != mvSet.end(); it++) {
    if (num == *it) {
      mvSet.erase(it);
      break;
    }
  }
  if (mvSet.empty()) {
    mRunning = MFALSE;
  }
  MY_LOGI("(%d/%zu) LongExposure[%d]", num, mvSet.size(), mRunning);
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
LongExposureStatus::set(MINT num, MINT64 exp_ns) {
  std::lock_guard<std::mutex> _l(mLock);
  if (exp_ns >= mThreshold && num > 0) {
    MBOOL isFound = MFALSE;
    std::vector<MINT32>::iterator it = mvSet.begin();
    for (; it != mvSet.end(); it++) {
      if (num == *it) {
        isFound = MTRUE;
        break;
      }
    }
    if (!isFound) {
      mvSet.push_back(num);
      mRunning = MTRUE;
    }
    MY_LOGI("(%d/%zu) LongExposure[%d]", num, mvSet.size(), mRunning);
    return MTRUE;
  }
  return MFALSE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
LongExposureStatus::get(void) {
  std::lock_guard<std::mutex> _l(mLock);
  MBOOL isRunning = mRunning;
  return isRunning;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  ProcedureStageControl Implementation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
ProcedureStageControl::reset(void) {
  for (MUINT32 i = 0; i < mvpStage.size(); i++) {
    std::shared_ptr<StageNote> p = mvpStage.at(i);
    std::lock_guard<std::mutex> _l(p->mLock);
    if (p->mWait) {
      p->mCond.notify_all();
    }
    p->mWait = MFALSE;
    p->mDone = MFALSE;
    p->mSuccess = MFALSE;
  }
  MY_LOGI("StageCtrl reset OK");
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
ProcedureStageControl::wait(MUINT32 eStage, MBOOL* rSuccess) {
  if (eStage >= mvpStage.size()) {
    MY_LOGW("wait - illegal (%d >= %zu)", eStage, mvpStage.size());
    return MFALSE;
  }
  //
  {
    std::shared_ptr<StageNote> p = mvpStage.at(eStage);
    std::unique_lock<std::mutex> _l(p->mLock);
    if (!p->mDone) {
      P1_TRACE_F_BEGIN(SLG_S, "S_Wait(%d)", p->mId);
      MY_LOGI("StageCtrl waiting(%d)", eStage);
      p->mWait = MTRUE;
      p->mCond.wait(_l);
      P1_TRACE_C_END(SLG_S);  // "S_Wait"
    }
    p->mWait = MFALSE;
    *rSuccess = p->mSuccess;
  }
  MY_LOGI("StageCtrl wait(%d) OK", eStage);
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
ProcedureStageControl::done(MUINT32 eStage, MBOOL bSuccess) {
  if (eStage >= mvpStage.size()) {
    MY_LOGW("done - illegal (%d >= %zu)", eStage, mvpStage.size());
    return MFALSE;
  }
  //
  {
    std::shared_ptr<StageNote> p = mvpStage.at(eStage);
    std::unique_lock<std::mutex> _l(p->mLock);
    p->mDone = MTRUE;
    p->mSuccess = bSuccess;
    if (p->mWait) {
      MY_LOGI("StageCtrl signal(%d)", eStage);
      p->mCond.notify_all();
    }
  }
  MY_LOGI("StageCtrl done(%d) OK", eStage);
  return MTRUE;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  ConcurrenceControl Implementation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
ConcurrenceControl::initBufInfo_clean(void) {
  std::unique_lock<std::mutex> _l(mLock);
  if (mpBufInfo != nullptr) {
    delete mpBufInfo;
    mpBufInfo = nullptr;
    return MTRUE;
  }
  return MFALSE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
ConcurrenceControl::initBufInfo_get(
    NSCam::NSIoPipe::NSCamIOPipe::QBufInfo** ppBufInfo) {
  std::unique_lock<std::mutex> _l(mLock);
  if (mpBufInfo == nullptr) {
    *ppBufInfo = nullptr;
    return MFALSE;
  }
  *ppBufInfo = mpBufInfo;
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
ConcurrenceControl::initBufInfo_create(
    NSCam::NSIoPipe::NSCamIOPipe::QBufInfo** ppBufInfo) {
  std::unique_lock<std::mutex> _l(mLock);
  if (mpBufInfo != nullptr) {
    delete mpBufInfo;
    mpBufInfo = nullptr;
  }
  //
  mpBufInfo = new NSCam::NSIoPipe::NSCamIOPipe::QBufInfo();
  //
  if (mpBufInfo == nullptr) {
    *ppBufInfo = nullptr;
    return MFALSE;
  }
  *ppBufInfo = mpBufInfo;
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
void ConcurrenceControl::setAidUsage(MBOOL enable) {
  std::lock_guard<std::mutex> _l(mLock);
  mIsAssistUsing = enable;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
ConcurrenceControl::getAidUsage(void) {
  std::lock_guard<std::mutex> _l(mLock);
  return mIsAssistUsing;
}

/******************************************************************************
 *
 ******************************************************************************/
void ConcurrenceControl::cleanAidStage(void) {
  setAidUsage(MFALSE);
  if (getStageCtrl() != nullptr) {
    getStageCtrl()->reset();
  }
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<ProcedureStageControl> ConcurrenceControl::getStageCtrl(void) {
  return mpStageCtrl;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  HardwareStateControl Implementation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
void HardwareStateControl::config(
    MINT32 nOpenId,
    MINT32 nLogLevel,
    MINT32 nLogLevelI,
    MINT32 nSysLevel,
    MUINT8 nBurstNum,
    NSCam::NSIoPipe::NSCamIOPipe::V4L2IIOPipe* pCamIO,
    std::shared_ptr<IHal3A_T> p3A,
    MBOOL isLegacyStandby) {
  std::lock_guard<std::mutex> _l(mLock);
  mOpenId = nOpenId;
  mLogLevel = nLogLevel;
  mLogLevelI = nLogLevelI;
  mSysLevel = nSysLevel;
  mBurstNum = nBurstNum;
  mpCamIO = pCamIO;
  mp3A = p3A;
  mIsLegacyStandby = isLegacyStandby;
  //
  clean();
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HardwareStateControl::isActive(void) {
  // by DRV comment, SUSPEND is not supported in burst mode
  return (IS_BURST_OFF && (mpCamIO != nullptr) && (mp3A != nullptr));
}

/******************************************************************************
 *
 ******************************************************************************/
SENSOR_STATUS_CTRL
HardwareStateControl::checkReceiveFrame(IMetadata* pMeta) {
  if (!isActive()) {
    return SENSOR_STATUS_CTRL_NONE;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  MINT32 ctrl = MTK_P1_SENSOR_STATUS_NONE;
  MBOOL tag = MFALSE;
  SENSOR_STATUS_CTRL ret = SENSOR_STATUS_CTRL_NONE;
  //
  if (tryGetMetadata<MINT32>(pMeta, MTK_P1NODE_SENSOR_STATUS, &ctrl)) {
    tag = MTRUE;
    if (ctrl == MTK_P1_SENSOR_STATUS_SW_STANDBY ||
        ctrl == MTK_P1_SENSOR_STATUS_HW_STANDBY) {
      switch (mState) {
        case STATE_NORMAL:
          mState = STATE_SUS_WAIT_NUM;
          ret = SENSOR_STATUS_CTRL_STANDBY;
          break;
        default:
          break;
      }
      MY_LOGI("[SUS-RES] meta-sus(%d) @(%d)", ctrl, mState);
    } else if (ctrl == MTK_P1_SENSOR_STATUS_STREAMING) {
      switch (mState) {
        case STATE_SUS_DONE:
          mState = STATE_RES_WAIT_NUM;
          ret = SENSOR_STATUS_CTRL_STREAMING;
          break;
        default:
          break;
      }
      MY_LOGI("[SUS-RES] meta-res(%d) @(%d)", ctrl, mState);
    }
  }
  MY_LOGD("tag(%d) : sensor(%d) - state(%d)", tag, ctrl, mState);
  if (mState == STATE_RES_WAIT_NUM) {
    mShutterTimeUs = (MINT32)0;
    if (tryGetMetadata<MINT32>(pMeta, MTK_P1NODE_RESUME_SHUTTER_TIME_US,
                               &mShutterTimeUs)) {
      MY_LOGI("[SUS-RES] re-streaming with (%d)us", mShutterTimeUs);
    } else {
      MY_LOGI("[SUS-RES] re-streaming without time-set");
    }
  }
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HardwareStateControl::checkReceiveRestreaming(void) {
  if (!isActive()) {
    return MFALSE;
  }
  return (mState == STATE_RES_WAIT_NUM) ? MTRUE : MFALSE;
}

/******************************************************************************
 *
 ******************************************************************************/
void HardwareStateControl::checkShutterTime(MINT32* rShutterTimeUs) {
  if (!isActive()) {
    return;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  if (mState >= STATE_RES_WAIT_NUM) {
    *rShutterTimeUs = mShutterTimeUs;
    MY_LOGI(
        "[SUS-RES] ShutterTime(%d) "
        "@(%d)",
        mShutterTimeUs, mState);
  } else {
    *rShutterTimeUs = 0;
    MY_LOGI(
        "[SUS-RES] none-ShutterTime(%d) "
        "@(%d)",
        mShutterTimeUs, mState);
  }
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
void HardwareStateControl::checkRestreamingNum(MINT32 num) {
  if (!isActive()) {
    return;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  if (mState == STATE_RES_WAIT_NUM) {
    mStreamingSetNum = num;
    //
    mState = STATE_RES_WAIT_SYNC;
    MY_LOGI(
        "[SUS-RES] StreamingSet(%d) "
        "@(%d)",
        mStreamingSetNum, mState);
  }
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HardwareStateControl::checkCtrlStandby(MINT32 num) {
  if (!isActive()) {
    return MFALSE;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);

  // TODO(MTK): remove STATE_SUS_WAIT_NUM
  if (mState == STATE_SUS_WAIT_NUM) {
    mStandbySetNum = num;
    mRequestPass = MFALSE;
    mState = STATE_SUS_WAIT_SYNC;
    MY_LOGI("[SUS-RES] StandbySet(%d) @(%d)", mStandbySetNum, mState);
  }

  if (mState == STATE_SUS_WAIT_SYNC) {
    // TODO(MTK): remove mStandbySetNum
    mStandbySetNum = num;
    //
    P1_TRACE_S_BEGIN(SLG_E, "P1:3A-pause");
    mp3A->pause();
    P1_TRACE_C_END(SLG_E);  // "P1:3A-pause"
    //
    MBOOL ret = MFALSE;
#if (MTKCAM_HAVE_SANDBOX_SUPPORT == 0)
    P1_TRACE_S_BEGIN(SLG_E, "P1:DRV-suspend");
    ret = mpCamIO->suspend();
    P1_TRACE_C_END(SLG_E);  // "P1:DRV-suspend"
#endif
    if (!ret) {
      MY_LOGE("[SUS-RES] FAIL : num-sus(%d) @(%d)", num, mState);
      mp3A->resume();
      clean();
      return MTRUE;
    }
    //
    mState = STATE_SUS_READY;
    mRequestCond.notify_all();
    MY_LOGI("[SUS-RES] CurNum(%d) (%d/%d) @(%d)", num, mStandbySetNum,
            mStreamingSetNum, mState);
    return MTRUE;
  }
  return MFALSE;
}

/******************************************************************************
 *
 ******************************************************************************/
void HardwareStateControl::checkRequest(void) {
  if (!isActive()) {
    return;
  }
  //
  std::unique_lock<std::mutex> _l(mLock);
  if (mState == STATE_SUS_WAIT_SYNC || mState == STATE_SUS_WAIT_NUM) {
    MY_LOGI("[SUS-RES] Suspend-Request @(%d)", mState);
    P1_TRACE_S_BEGIN(SLG_E, "P1:pause");
    MY_LOGD("[SUS-RES] wait pause +");
    mRequestCond.wait(_l);
    MY_LOGD("[SUS-RES] wait pause -");
    P1_TRACE_C_END(SLG_E);  // "P1:pause"
  }
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
void HardwareStateControl::checkThreadStandby(void) {
  if (!isActive()) {
    return;
  }
  //
  std::unique_lock<std::mutex> _l(mLock);
  if (mState == STATE_SUS_READY) {
    mState = STATE_SUS_DONE;
    MY_LOGI("[SUS-RES] Suspend-Loop @(%d)", mState);
    P1_TRACE_S_BEGIN(SLG_E, "P1:suspend");
    MY_LOGD("[SUS-RES] wait re-streaming +");
    mThreadCond.wait(_l);
    MY_LOGD("[SUS-RES] wait re-streaming -");
    P1_TRACE_C_END(SLG_E);  // "P1:pause"
  }
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
void HardwareStateControl::checkThreadWeakup(void) {
  if (!isActive()) {
    return;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  if (mState == STATE_RES_WAIT_SYNC) {
    MY_LOGI("[SUS-RES] Recover-Loop-W");
    mThreadCond.notify_all();
  }
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HardwareStateControl::checkFirstSync(void) {
  if (!isActive()) {
    return MFALSE;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  if (mState == STATE_RES_WAIT_SYNC) {
    mState = STATE_RES_WAIT_DONE;
    MY_LOGI("[SUS-RES] FirstSync (%d/%d) @(%d)", mStandbySetNum,
            mStreamingSetNum, mState);
    return MTRUE;
  }
  return MFALSE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HardwareStateControl::checkSkipSync(void) {
  if (!isActive()) {
    return MFALSE;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  switch (mState) {
    case STATE_NORMAL:
    case STATE_SUS_WAIT_NUM:
    case STATE_SUS_WAIT_SYNC:
    case STATE_RES_WAIT_SYNC:
    case STATE_RES_WAIT_DONE:
      return MFALSE;
      // case STATE_RES_WAIT_NUM:
      // case STATE_SUS_READY:
      // case STATE_SUS_DONE:
    default:
      break;
  }
  MY_LOGI("[SUS-RES] SkipSync (%d/%d) @(%d)", mStandbySetNum, mStreamingSetNum,
          mState);
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HardwareStateControl::checkSkipWait(void) {
  if (!isActive()) {
    return MFALSE;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  if (mRequestPass) {
    MY_LOGI("[SUS-RES] SkipWait pass (%d/%d) @(%d)", mStandbySetNum,
            mStreamingSetNum, mState);
    mRequestPass = MFALSE;
    return MTRUE;
  }
  switch (mState) {
    case STATE_NORMAL:
    case STATE_SUS_WAIT_NUM:
    case STATE_SUS_WAIT_SYNC:
    case STATE_SUS_READY:
    case STATE_SUS_DONE:
    case STATE_RES_WAIT_SYNC:
    case STATE_RES_WAIT_DONE:
      return MFALSE;
      // case STATE_RES_WAIT_NUM:
    default:
      break;
  }
  MY_LOGI("[SUS-RES] SkipWait (%d/%d) @(%d)", mStandbySetNum, mStreamingSetNum,
          mState);
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HardwareStateControl::checkSkipBlock(void) {
  if (!isActive()) {
    return MFALSE;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  if (mRequestPass) {
    MY_LOGI("[SUS-RES] SkipBlock pass (%d/%d) @(%d)", mStandbySetNum,
            mStreamingSetNum, mState);
    mRequestPass = MFALSE;
    return MTRUE;
  }
  switch (mState) {
    case STATE_NORMAL:
    case STATE_SUS_WAIT_NUM:
    case STATE_RES_WAIT_NUM:
    case STATE_RES_WAIT_SYNC:
    case STATE_RES_WAIT_DONE:
      return MFALSE;
      // case STATE_SUS_WAIT_SYNC:
      // case STATE_SUS_READY:
      // case STATE_SUS_DONE:
    default:
      break;
  }
  MY_LOGI("[SUS-RES] SkipBlock (%d/%d) @(%d)", mStandbySetNum, mStreamingSetNum,
          mState);
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HardwareStateControl::checkBufferState(void) {
  if (!isActive()) {
    // zero buffer count is abnormal
    return MFALSE;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  switch (mState) {
    case STATE_NORMAL:
    case STATE_SUS_WAIT_NUM:
    case STATE_SUS_WAIT_SYNC:
    case STATE_RES_WAIT_DONE:
      // zero buffer count is abnormal
      return MFALSE;
      // case STATE_SUS_READY:
      // case STATE_SUS_DONE:
      // case STATE_RES_WAIT_NUM:
      // case STATE_RES_WAIT_SYNC:
    default:
      break;
  }
  MY_LOGI("[SUS-RES] NormalCase (%d/%d) @(%d)", mStandbySetNum,
          mStreamingSetNum, mState);
  // zero buffer count is normal
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HardwareStateControl::checkDoneNum(MINT32 num) {
  if (!isActive()) {
    return MFALSE;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  switch (mState) {
    case STATE_NORMAL:
    case STATE_SUS_WAIT_NUM:
    case STATE_SUS_WAIT_SYNC:
    case STATE_SUS_READY:
      // do nothing
      return MFALSE;
      // case STATE_SUS_DONE:
      // case STATE_RES_WAIT_NUM:
      // case STATE_RES_WAIT_SYNC:
      // case STATE_RES_WAIT_DONE:
    default:
      break;
  }
  mvStoreNum.clear();
  if (mState == STATE_RES_WAIT_DONE && mStreamingSetNum == num) {
    mStandbySetNum = 0;
    mStreamingSetNum = 0;
    mShutterTimeUs = 0;
    mRequestPass = MFALSE;
    mState = STATE_NORMAL;
  }
  MY_LOGI("[SUS-RES] CurNum(%d) SetNum(%d/%d) @(%d)", num, mStandbySetNum,
          mStreamingSetNum, mState);
  return MTRUE;  // need to drop previous frame
}

/******************************************************************************
 *
 ******************************************************************************/
void HardwareStateControl::checkNotePass(MBOOL pass) {
  if (!isActive()) {
    return;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  mRequestPass = pass;
  MY_LOGI("[SUS-RES] NoteNextRequestPass(%d) (%d/%d) @(%d)", mRequestPass,
          mStandbySetNum, mStreamingSetNum, mState);
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
void HardwareStateControl::setDropNum(MINT32 num) {
  if (!isActive()) {
    return;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  mvStoreNum.push_back(num);
  MY_LOGI("[SUS-RES] CurNum(%d) (%d/%d) @(%d)", num, mStandbySetNum,
          mStreamingSetNum, mState);
}

/******************************************************************************
 *
 ******************************************************************************/
MINT32
HardwareStateControl::getDropNum(void) {
  if (!isActive()) {
    return 0;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  MINT32 num = 0;
  if (!mvStoreNum.empty()) {
    std::vector<MINT32>::iterator it = mvStoreNum.begin();
    num = *it;
    mvStoreNum.erase(it);
  }
  return num;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HardwareStateControl::isLegacyStandby(void) {
  return mIsLegacyStandby;
}

/******************************************************************************
 *
 ******************************************************************************/
void HardwareStateControl::reset(void) {
  if (!isActive()) {
    return;
  }
  //
  std::lock_guard<std::mutex> _l(mLock);
  if (mState != STATE_NORMAL) {
    MY_LOGI("[SUS-RES] reset (%d/%d) @(%d ===>>> %d)", mStandbySetNum,
            mStreamingSetNum, mState, STATE_NORMAL);
  }
  mp3A = nullptr;
  mpCamIO = nullptr;
  clean();
  MY_LOGD("HardwareStateControl RESET");
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
void HardwareStateControl::clean(void) {
  mIsLegacyStandby = MFALSE;
  mState = STATE_NORMAL;
  mStandbySetNum = 0;
  mStreamingSetNum = 0;
  mShutterTimeUs = 0;
  mRequestPass = MFALSE;
  mvStoreNum.clear();
  mRequestCond.notify_all();
  mThreadCond.notify_all();
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
void HardwareStateControl::dump(void) {
  MY_LOGW("[SUS-RES] DUMP : num-sus(%d) num-res(%d) legacy(%d) @(%d)",
          mStandbySetNum, mStreamingSetNum, mIsLegacyStandby, mState);
  return;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  FrameNote Implementation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
MVOID
FrameNote::set(MINT32 num) {
  if (CC_UNLIKELY(mSlotCapacity == 0)) {
    MY_LOGW("Capacity(%d)", mSlotCapacity);
    return;
  }
  pthread_rwlock_wrlock(&mLock);
  gettimeofday(&mLastTv, NULL);
  mLastTid = (MUINT32)gettid();
  mLastNum = num;
  //
  mSlotIndex = (mSlotIndex + 1) % mSlotCapacity;
  if (CC_LIKELY(mSlotIndex < (MUINT32)mvSlot.size())) {
    mvSlot[mSlotIndex] = num;
  } else {
    MY_LOGW("index(%d) >= size(%d)", mSlotIndex, (MUINT32)mvSlot.size());
  }
  pthread_rwlock_unlock(&mLock);
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
FrameNote::get(std::string* pStr) {
  if (CC_UNLIKELY(mSlotCapacity == 0)) {
    MY_LOGW("Capacity(%d)", mSlotCapacity);
    return;
  }
  if (CC_UNLIKELY(pStr == nullptr)) {
    MY_LOGW("get string null");
    return;
  }

  pthread_rwlock_rdlock(&mLock);
  std::string info("");
  struct tm* tm = NULL;
  struct tm now_time;
  char date_time[32] = {0};
  if ((tm = localtime_r(&(mLastTv.tv_sec), &now_time)) == nullptr) {
    snprintf(date_time, sizeof(date_time), "%s", "NO_LOCAL_TIME");
  } else {
    strftime(reinterpret_cast<char*>(date_time), sizeof(date_time), "%H:%M:%S",
             tm);
  }
  base::StringAppendF(&info, " [Last-Frame-Num(%d_%s.%06ld@%05d) ", mLastNum,
                      date_time, mLastTv.tv_usec, mLastTid);
  //
  MUINT32 currIdx = mSlotIndex;
  MUINT32 thisIdx = currIdx;
  MUINT32 cnt = (MUINT32)mvSlot.size();
  MINT32 num = P1NODE_FRAME_NOTE_NUM_UNKNOWN;
  for (MUINT32 i = 0; i < mSlotCapacity; i++) {
    if (CC_LIKELY(thisIdx < cnt)) {
      num = mvSlot[thisIdx];
      if (CC_LIKELY(num != P1NODE_FRAME_NOTE_NUM_UNKNOWN)) {
        base::StringAppendF(&info, "%d ", num);
      }
    }
    // move to the previous slot
    thisIdx = (thisIdx + mSlotCapacity - 1) % mSlotCapacity;
  }
  base::StringAppendF(&info, "... ]");
  //
  pStr->append(info);
  pthread_rwlock_unlock(&mLock);
  return;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  LogInfo Implementation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
void LogInfo::clear(void) {
  pthread_rwlock_wrlock(&mLock);
  for (int cp = LogInfo::CP_FIRST; cp < LogInfo::CP_MAX; cp++) {
    mSlots[cp].clear();
  }
  pthread_rwlock_unlock(&mLock);
}

/******************************************************************************
 *
 ******************************************************************************/
void LogInfo::setMemo(LogInfo::CheckPoint cp,
                      MINT64 param0,
                      MINT64 param1,
                      MINT64 param2,
                      MINT64 param3) {
  if (!getActive()) {
    return;
  }
  // for performance consideration, only RLock while per-frame memo set/get
  pthread_rwlock_rdlock(&mLock);
  //
  write(cp, param0, param1, param2, param3);
  pthread_rwlock_unlock(&mLock);
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
void LogInfo::write(LogInfo::CheckPoint cp,
                    MINT64 param0,
                    MINT64 param1,
                    MINT64 param2,
                    MINT64 param3) {
  if (!getActive()) {
    return;
  }
  struct timeval tv = {0, 0};
  gettimeofday(&tv, NULL);
  if (cp < LogInfo::CP_MAX) {
    pthread_rwlock_wrlock(&mSlots[cp].mLock);
    mSlots[cp].mTv = tv;
    mSlots[cp].mTid = (MUINT32)gettid();
    mSlots[cp].mParam[0] = param0;
    mSlots[cp].mParam[1] = param1;
    mSlots[cp].mParam[2] = param2;
    mSlots[cp].mParam[3] = param3;
    pthread_rwlock_unlock(&mSlots[cp].mLock);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
void LogInfo::getMemo(LogInfo::CheckPoint cp, std::string* str) {
  if (!getActive()) {
    return;
  }
  // for performance consideration, only RLock while per-frame memo set/get
  pthread_rwlock_rdlock(&mLock);
  //
  read(cp, str);
  pthread_rwlock_unlock(&mLock);
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
void LogInfo::read(LogInfo::CheckPoint cp, std::string* str) {
  if (!getActive()) {
    return;
  }
  //

  if (cp < LogInfo::CP_MAX && str != NULL) {
    pthread_rwlock_rdlock(&mSlots[cp].mLock);
    struct tm* tm = NULL;
    char date_time[32] = {0};
    struct tm now_time;
    if ((tm = localtime_r(&(mSlots[cp].mTv.tv_sec), &now_time)) == nullptr) {
      snprintf(date_time, sizeof(date_time), "%s", "NO_LOCAL_TIME");
    } else {
      strftime(reinterpret_cast<char*>(date_time), sizeof(date_time),
               "%H:%M:%S", tm);
    }
    base::StringAppendF(str, " [ %s.%06ld_%05d-%05d= ", date_time,
                        mSlots[cp].mTv.tv_usec,
                        ((mSlots[cp].mTid > 0) ? mPid : 0), mSlots[cp].mTid);
    if (cp != mNotes[cp].idx) {
      base::StringAppendF(str, "< NOTE_MISMATCH - %d!=%d >", cp,
                          mNotes[cp].idx);
    } else {
      base::StringAppendF(str, "<%s> ", mNotes[cp].main);
    }
    for (int i = 0; i < PARAM_NUM; i++) {
      base::StringAppendF(str, "%s(%" PRId64 ") ", mNotes[cp].sub[i],
                          mSlots[cp].mParam[i]);
    }
    base::StringAppendF(str, "] ");
    pthread_rwlock_unlock(&mSlots[cp].mLock);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
void LogInfo::extract() {
  if (!getActive()) {
    return;
  }
  //
  gettimeofday(&(mData.mNowTv), NULL);
  mData.mNowTime = (mData.mNowTv.tv_sec * ONE_S_TO_US) + mData.mNowTv.tv_usec;
  mData.mNowTid = (MUINT32)gettid();
  for (int cp = LogInfo::CP_FIRST; cp < LogInfo::CP_MAX; cp++) {
    mData.mTv[cp] = mSlots[cp].mTv;
    mData.mTime[cp] =
        (mSlots[cp].mTv.tv_sec * ONE_S_TO_US) + mSlots[cp].mTv.tv_usec;
    mData.mTid[cp] = mSlots[cp].mTid;
  }
  //
#ifdef GET_DATA
#undef GET_DATA
#endif
#define GET_DATA(slot, idx) (MINT32)(mSlots[slot].mParam[idx])
  //
  mData.mCbSyncType = GET_DATA(CP_CB_SYNC_REV, 0);
  mData.mCbProcType = GET_DATA(CP_CB_PROC_REV, 0);
  //
  mData.mStartSetType = GET_DATA(CP_START_SET_END, 0);
  mData.mStartSetMn = GET_DATA(CP_START_SET_END, 1);
  //
  mData.mPreSetKey = GET_DATA(CP_PRE_SET_END, 0);
  //
  mData.mSetFn = GET_DATA(CP_SET_END, 2);
  //
  mData.mSetMn = GET_DATA(CP_SET_END, 1);
  mData.mEnqMn = GET_DATA(CP_ENQ_END, 0);
  mData.mDeqMn = GET_DATA(CP_DEQ_END, 0);
  //
  mData.mBufStream = GET_DATA(CP_BUF_BGN, 0);
  mData.mBufMn = GET_DATA(CP_BUF_BGN, 1);
  mData.mBufFn = GET_DATA(CP_BUF_BGN, 2);
  mData.mBufRn = GET_DATA(CP_BUF_BGN, 3);
  //
  mData.mAcceptFn = GET_DATA(CP_REQ_ACCEPT, 0);
  mData.mAcceptRn = GET_DATA(CP_REQ_ACCEPT, 1);
  mData.mAcceptResult = GET_DATA(CP_REQ_ACCEPT, 3);
  //
  mData.mRevFn = GET_DATA(CP_REQ_REV, 0);
  mData.mRevRn = GET_DATA(CP_REQ_REV, 1);
  //
  mData.mOutFn = GET_DATA(CP_OUT_BGN, 0);
  mData.mOutRn = GET_DATA(CP_OUT_BGN, 1);
  //
#undef GET_DATA
  //
  mData.mReady = MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
void LogInfo::analyze(MBOOL bForceToPrint) {
  if (!getActive()) {
    return;
  }
  //
  reset();
  extract();
  if (!mData.mReady) {
    return;
  }
  //
  MBOOL bBlockInStart = MFALSE;
  MBOOL bBlockInStop = MFALSE;
  MBOOL bBlockAfterFlush = MFALSE;
#ifdef START_STOP_OK
#undef START_STOP_OK
#endif
#define START_STOP_OK \
  ((!bBlockInStart) && (!bBlockInStop) && (!bBlockAfterFlush))
  //
  MBOOL bStreaming = MTRUE;
  if (mData.mTime[CP_OP_STOP_END] > mData.mTime[CP_OP_START_BGN]) {
    bStreaming = MFALSE;
  }
  //
  // for start flow
  CHECK_STUCK(CP_OP_START_BGN, CP_OP_START_END, CcDeduce_OpStartBlocking);
  if (HAS(CC_DEDUCE, CcDeduce_OpStartBlocking)) {
    bBlockInStart = MTRUE;
  }
  // for stop flow
  CHECK_STUCK(CP_OP_STOP_BGN, CP_OP_STOP_END, CcDeduce_OpStopBlocking);
  if (HAS(CC_DEDUCE, CcDeduce_OpStopBlocking)) {
    bBlockInStop = MTRUE;
  }
  // for uninit() not called after flush()
  if (((!bBlockInStart) && (!bBlockInStop)) &&
      (mData.mTime[CP_API_FLUSH_END] >
       mData.mTime[CP_REQ_REV]) &&  // no queue acceptable request after flush
      (mData.mTime[CP_API_FLUSH_END] >
       mData.mTime[CP_API_FLUSH_BGN]) &&  // flush done
      (mData.mTime[CP_API_FLUSH_END] >
       mData
           .mTime[CP_API_UNINIT_BGN]) &&  // uninit is not call after flush done
      (DIFF_NOW(CP_API_FLUSH_END, P1_GENERAL_API_CHECK_US))) {
    ADD(CC_DEDUCE, CcDeduce_UninitNotCalledAfterFlush);
    bBlockAfterFlush = MTRUE;
  }
  //
  CHECK_OP(CP_START_SET_BGN, CP_START_SET_END, CcOpTimeout_StartSet);
  CHECK_OP(CP_PRE_SET_BGN, CP_PRE_SET_END, CcOpTimeout_PreSet);
  CHECK_OP(CP_SET_BGN, CP_SET_END, CcOpTimeout_Set);
  CHECK_OP(CP_BUF_BGN, CP_BUF_END, CcOpTimeout_Buf);
  CHECK_OP(CP_ENQ_BGN, CP_ENQ_END, CcOpTimeout_Enq);
  CHECK_OP(CP_OUT_BGN, CP_OUT_END, CcOpTimeout_Dispatch);
  //
  if (START_STOP_OK && bStreaming) {
    CHECK_WAIT(CP_REQ_RET, CP_REQ_REV, CcWaitOvertime_Request);
    CHECK_WAIT(CP_CB_SYNC_RET, CP_CB_SYNC_REV, CcWaitOvertime_3aCbSyncDone);
    CHECK_WAIT(CP_CB_PROC_RET, CP_CB_PROC_REV, CcWaitOvertime_3aCbProcFinish);
  }
  //
  // for no request arrival
  if (START_STOP_OK && bStreaming &&
      (mData.mSetFn <=
       mData.mRevFn) &&  // include non-set and set-first-request-Fn-0 cases //
                         // include dummy request (SetFn < 0) // since all of
                         // the accept request set to 3A
      (mData.mTime[CP_REQ_RET] > mData.mTime[CP_REQ_REV]) &&
      (DIFF_NOW(CP_REQ_RET, P1_GENERAL_API_CHECK_US)) &&
      (!((mData.mAcceptFn >
          mData.mRevFn) &&  // exclude the case about the next request is
                            // arrival but rejected since not-available
         (mData.mAcceptResult == REQ_REV_RES_REJECT_NOT_AVAILABLE) &&
         (mData.mTime[CP_REQ_ACCEPT] > mData.mTime[CP_SET_END])))) {
    ADD(CC_DEDUCE, CcDeduce_FwNoRequestAccept);
  }
  //
  // for 3A no first callback
  if (mData.mTime[CP_CB_PROC_REV] == 0 && mData.mTime[CP_START_SET_END] > 0) {
    if (mData.mStartSetType == START_SET_CAPTURE) {
      ADD(CC_DEDUCE, CcDeduce_3aNoFirstCbInCapture);
    } else if (mData.mStartSetType == START_SET_REQUEST) {
      ADD(CC_DEDUCE, CcDeduce_3aNoFirstCbInRequest);
    } else {
      ADD(CC_DEDUCE, CcDeduce_3aNoFirstCbInGeneral);
    }
  }
  //
  // for 3A callback stuck-with / look-for
  CHECK_STUCK(CP_SET_BGN, CP_SET_END, CcDeduce_3aStuckWithSet);
  CHECK_STUCK(CP_BUF_BGN, CP_BUF_END, CcDeduce_3aStuckWithBuf);
  CHECK_STUCK(CP_ENQ_BGN, CP_ENQ_END, CcDeduce_3aStuckWithEnq);
  if (HAS(CC_DEDUCE, CcDeduce_3aStuckWithSet) ||
      HAS(CC_DEDUCE, CcDeduce_3aStuckWithBuf) ||
      HAS(CC_DEDUCE, CcDeduce_3aStuckWithEnq)) {
    // 3A-stuck-clue defined
  } else if (START_STOP_OK &&
             bStreaming) {  // HAS(CcWaitOvertime_3aCbSyncDone) ||
                            // HAS(CcWaitOvertime_3aCbProcFinish)
    if (mData.mTime[CP_CB_PROC_RET] >
        mData.mTime[CP_CB_SYNC_RET]) {  // the last CB is PROC_FINISH
      if ((mData.mTime[CP_CB_PROC_RET] > mData.mTime[CP_CB_PROC_REV]) &&
          (DIFF_NOW(CP_CB_PROC_RET, P1_GENERAL_STUCK_JUDGE_US))) {
        ADD(CC_DEDUCE, CcDeduce_3aLookForCbSyncDone);
      }
    } else {  // the last CB is SYNC_DONE
      if ((mData.mTime[CP_CB_SYNC_RET] > mData.mTime[CP_CB_SYNC_REV]) &&
          (DIFF_NOW(CP_CB_SYNC_RET, P1_GENERAL_STUCK_JUDGE_US))) {
        ADD(CC_DEDUCE, CcDeduce_3aLookForCbProcFinish);
      }
    }
  }
  //
  // for DRV DeQ case
  if (mData.mTime[CP_DEQ_BGN] > mData.mTime[CP_DEQ_END]) {
    if ((mData.mNowTime - mData.mTime[CP_DEQ_BGN]) >
        P1_GENERAL_WAIT_OVERTIME_US) {
      if (mData.mEnqMn > mData.mDeqMn) {
        if (((mData.mNowTime - mData.mTime[CP_ENQ_END]) >
             P1_GENERAL_WAIT_OVERTIME_US) ||
            (mData.mEnqMn >
             (mData.mDeqMn + (mBurstNum * P1NODE_DEF_QUEUE_DEPTH)))) {
          ADD(CC_DEDUCE, CcDeduce_DrvDeqDelay);
        }
      }
    }
  }
  //
  //
#undef START_STOP_OK
  //
  if (mCode != CC_NONE || bForceToPrint) {
    MY_LOGD(P1_LOG_NOTE_TAG P1_LOG_LINE_BGN);
    MY_LOGD(P1_LOG_NOTE_TAG " ClueCode_ALL[0x%" PRIx64 "]", mCode);
    //
    MBOOL clueCP[LogInfo::CP_MAX];
    std::vector<LogInfo::CheckPoint> vCP;
    ::memset(clueCP, 0, sizeof(clueCP));
    for (MUINT8 i = 0; i < CC_AMOUNT_MAX; i++) {
      MUINT64 bit = (MUINT64)((MUINT64)(0x1) << i);
      if ((bit & mCode) == bit) {
        std::string str("");
        bitStr(bit, &str);
        MY_LOGD(P1_LOG_NOTE_TAG " ClueCode-bit[0x%" PRIx64 "] = %s ", bit,
                str.c_str());
        vCP.clear();
        bitTag(bit, &vCP);
        for (size_t j = 0; j < vCP.size(); j++) {
          if (vCP[j] < LogInfo::CP_MAX) {
            clueCP[vCP[j]] = MTRUE;
          }
        }
      }
    }
    for (int cp = LogInfo::CP_FIRST; cp < LogInfo::CP_MAX; cp++) {
      if (clueCP[cp]) {
        std::string str(P1_LOG_NOTE_TAG);
        read((LogInfo::CheckPoint)cp, &str);
        MY_LOGD("%s", str.c_str());
      }
    }
    //
    MY_LOGD(P1_LOG_NOTE_TAG P1_LOG_LINE_END);
  }
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
void LogInfo::inspect(LogInfo::InspectType type, char const* info) {
  if (!getActive()) {
    return;
  }
  // excluding concurrence per-frame memo set/get
  pthread_rwlock_wrlock(&mLock);
  //
  MBOOL routine = MFALSE;
  switch (type) {
    case IT_PERIODIC_CHECK:
    case IT_STOP_NO_REQ_IN_GENERAL:
      routine = MTRUE;
      break;
    default:
      break;
  }
  analyze(!routine);
  if (routine && (mCode == CC_NONE)) {
    pthread_rwlock_unlock(&mLock);
    return;  // no need to dump
  }
  //
  if (type < LogInfo::IT_MAX) {
    MY_LOGI(P1_LOG_DUMP_TAG " [Burst:%d][Type:%d] %s", mBurstNum, type,
            mTexts[type].main);
  }
  //
  if (info != nullptr) {
    MY_LOGI(P1_LOG_DUMP_TAG " [Info] %s", info);
  }
  //
  MY_LOGI(P1_LOG_DUMP_TAG P1_LOG_LINE_BGN);
  for (int cp = LogInfo::CP_FIRST; cp < LogInfo::CP_MAX; cp++) {
    std::string str(P1_LOG_DUMP_TAG);
    read((LogInfo::CheckPoint)cp, &str);
    MY_LOGI("%s", str.c_str());
  }
  MY_LOGI(P1_LOG_DUMP_TAG P1_LOG_LINE_END);
  pthread_rwlock_unlock(&mLock);
  return;
}

};  // namespace NSP1Node
};  // namespace v3
};  // namespace NSCam
