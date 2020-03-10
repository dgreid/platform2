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

#define LOG_TAG "3dnr_hal"
//
#include <3dnr_hal.h>
#include <common/3dnr/3dnr_hal_base.h>
#include "hal/inc/camera_custom_3dnr.h"
#include <memory>
#include <mtkcam/aaa/IHal3A.h>
#include <mtkcam/aaa/IIspMgr.h>
#include <mtkcam/drv/def/ispio_sw_scenario.h>
#include <mtkcam/feature/lmv/lmv_ext.h>
#include <mtkcam/utils/metadata/hal/mtk_platform_metadata_tag.h>
#include <mtkcam/utils/std/common.h>
#include <mtkcam/utils/std/Log.h>
#include <mtkcam/utils/std/Misc.h>
#include <sys/resource.h>

using NS3Av3::E3ACtrl_SetAEPlineLimitation;
using NSCam::IMetadata;
using NSCam::Type2Type;
using NSCam::NR3D::NR3DMVInfo;
/***************************************************************************
 *
 ***************************************************************************/

#define NR3D_FORCE_GMV_ZERO 0
#define NR3D_NO_HW_POWER_OFF 0

/**************************************************************************
 *
 **************************************************************************/
static std::shared_ptr<hal3dnrBase> pHal3dnr = NULL;
static MINT32 clientCnt = 0;

/****************************************************************************
 *
 ****************************************************************************/

typedef enum {
  NR3D_PATH_NOT_DEF = 0x00,   // invalid path
  NR3D_PATH_RRZO = 0x01,      // rrzo path
  NR3D_PATH_RRZO_CRZ = 0x02,  // rrzo + EIS1.2 apply CMV crop
  NR3D_PATH_IMGO = 0x03,      // ZSD preview IMGO path
} NR3D_PATH_ENUM;

struct NR3DAlignParam {
 public:
  MUINT32 onOff_onOfStX;
  MUINT32 onOff_onOfStY;
  MUINT32 onSiz_onWd;
  MUINT32 onSiz_onHt;
  MUINT32 u4VipiOffset_X;
  MUINT32 u4VipiOffset_Y;
  MUINT32 vipi_readW;  // in pixel
  MUINT32 vipi_readH;  // in pixel

  NR3DAlignParam()
      : onOff_onOfStX(0x0),
        onOff_onOfStY(0x0),
        onSiz_onWd(0x0),
        onSiz_onHt(0x0),
        u4VipiOffset_X(0x0),
        u4VipiOffset_Y(0x0),
        vipi_readW(0x0),
        vipi_readH(0x0) {}
};

struct hal3dnrDebugParam {
  MINT32 mLogLevel;
  MINT32 mForce3DNR;  // hal force support 3DNR
  MBOOL mSupportZoom3DNR;
};

struct hal3dnrPolicyTable {
  MUINT32(*policyFunc)
  (const NR3DHALParam& nr3dHalParam,
   const hal3dnrDebugParam& debugParam,
   const hal3dnrSavedFrameInfo&
       preSavedFrameInfo);  // executable policy function
};

static void print_NR3DHALParam(const NR3DHALParam& nr3dHalParam,
                               MINT32 mLogLevel) {
  MY_LOGD("=== mkdbg: print_NR3DHALParam: start ===");

  if (nr3dHalParam.pTuningData) {
    MY_LOGD("\t pTuningData = %p", nr3dHalParam.pTuningData);
  }
  if (nr3dHalParam.p3A) {
    MY_LOGD("\t p3A = %p", nr3dHalParam.p3A.get());
  }
  // frame generic
  MY_LOGD("\t frameNo = %d", nr3dHalParam.frameNo);
  MY_LOGD("\t iso = %d", nr3dHalParam.iso);
  MY_LOGD("\t isoThreshold = %d", nr3dHalParam.isoThreshold);

  // imgi related
  MY_LOGD("\t isCRZUsed = %d", nr3dHalParam.isCRZUsed);
  MY_LOGD("\t isIMGO = %d", nr3dHalParam.isIMGO);

  // lmv related info
  MY_LOGD("\t gmvX= %d => %d pixel", nr3dHalParam.GMVInfo.gmvX,
          nr3dHalParam.GMVInfo.gmvX / LMV_GMV_VALUE_TO_PIXEL_UNIT);
  MY_LOGD("\t gmvY= %d => %d pixel", nr3dHalParam.GMVInfo.gmvY,
          nr3dHalParam.GMVInfo.gmvY / LMV_GMV_VALUE_TO_PIXEL_UNIT);
  MY_LOGD("\t (confX,confY)=(%d,%d)", nr3dHalParam.GMVInfo.confX,
          nr3dHalParam.GMVInfo.confY);
  MY_LOGD("\t x_int= %d", nr3dHalParam.GMVInfo.x_int);
  MY_LOGD("\t y_int= %d", nr3dHalParam.GMVInfo.y_int);

  // vipi related
  if (nr3dHalParam.pIMGBufferVIPI == NULL) {
    MY_LOGW("\t pIMGBufferVIPI == NULL");
  } else {
    MY_LOGD("\t pIMGBufferVIPI: %p", nr3dHalParam.pIMGBufferVIPI);
    MY_LOGD("\t\t vipi_image.w = %d",
            nr3dHalParam.pIMGBufferVIPI->getImgSize().w);
    MY_LOGD("\t\t vipi_image.h = %d",
            nr3dHalParam.pIMGBufferVIPI->getImgSize().h);
    MY_LOGD("\t\t vipi_format = %d, eImgFmt_YUY2: %d, eImgFmt_YV12: %d)",
            nr3dHalParam.pIMGBufferVIPI->getImgFormat(), NSCam::eImgFmt_YUY2,
            NSCam::eImgFmt_YV12);
    MY_LOGD("\t\t vipi_strides = %zu",
            nr3dHalParam.pIMGBufferVIPI->getBufStridesInBytes(0));
  }

  // output related, ex: img3o
  MRect dst_resizer_rect;
  MY_LOGD("\t destRect.w = %d", nr3dHalParam.dst_resizer_rect.s.w);
  MY_LOGD("\t destRect.w = %d", nr3dHalParam.dst_resizer_rect.s.h);

  MY_LOGD("=== mkdbg: print_NR3DHALParam: end ===");
}

static void print_NR3DParam(const NR3DParam& nr3dParam, MINT32 mLogLevel) {
  MY_LOGD("=== mkdbg: print_NR3DParam: start ===");

  MY_LOGD("\t ctrl_onEn = %d", nr3dParam.ctrl_onEn);
  MY_LOGD("\t onOff_onOfStX = %d", nr3dParam.onOff_onOfStX);
  MY_LOGD("\t onOff_onOfStY = %d", nr3dParam.onOff_onOfStY);
  MY_LOGD("\t onSiz_onWd = %d", nr3dParam.onSiz_onWd);
  MY_LOGD("\t onSiz_onWd = %d", nr3dParam.onSiz_onHt);
  MY_LOGD("\t vipi_offst = %d", nr3dParam.vipi_offst);
  MY_LOGD("\t vipi_readW = %d", nr3dParam.vipi_readW);
  MY_LOGD("\t vipi_readH = %d", nr3dParam.vipi_readH);
  MY_LOGD("=== mkdbg: print_NR3DParam: end ===");
}

static NR3D_PATH_ENUM determine3DNRPath(const NR3DHALParam& nr3dHalParam) {
  if (nr3dHalParam.isIMGO == MFALSE && nr3dHalParam.isCRZUsed == MTRUE) {
    // RRZ + CRZ
    return NR3D_PATH_RRZO_CRZ;
  } else if (nr3dHalParam.isIMGO == MTRUE) {
    // IMGO crop
    return NR3D_PATH_IMGO;
  } else {
    // RRZ only
    return NR3D_PATH_RRZO;
  }
}

static MUINT32 checkIso(const NR3DHALParam& nr3dHalParam,
                        const hal3dnrDebugParam& debugParam,
                        const hal3dnrSavedFrameInfo& preSavedFrameInfo) {
  MUINT32 errorStatus = NR3D_ERROR_NONE;
  MINT32 i4IsoThreshold = nr3dHalParam.isoThreshold;

  MY_LOGD("iso=%d, Poweroff threshold=%d, frame:%d", nr3dHalParam.iso,
          i4IsoThreshold, nr3dHalParam.frameNo);

  if (nr3dHalParam.iso < i4IsoThreshold) {
    errorStatus |= NR3D_ERROR_UNDER_ISO_THRESHOLD;
    return errorStatus;
  }

  return errorStatus;
}

static MUINT32 checkVipiImgiFrameSize(
    const NR3DHALParam& nr3dHalParam,
    const hal3dnrDebugParam& debugParam,
    const hal3dnrSavedFrameInfo& preSavedFrameInfo) {
  MUINT32 errorStatus = NR3D_ERROR_NONE;

  if (nr3dHalParam.pIMGBufferVIPI == NULL) {
    errorStatus |= NR3D_ERROR_INVALID_PARAM;
    return errorStatus;
  }

  MSize vipiFrameSize = nr3dHalParam.pIMGBufferVIPI->getImgSize();
  const MRect& pImg3oFrameRect = nr3dHalParam.dst_resizer_rect;

  // W/H of buffer (i.e. Current frame size) is determined, so check previous
  // vs. current frame size for 3DNR.
  if (pImg3oFrameRect.s == vipiFrameSize) {
    return errorStatus;
  } else {
    if (debugParam.mSupportZoom3DNR) {
      print_NR3DHALParam(nr3dHalParam, debugParam.mLogLevel);

      // Zoom case
      if (vipiFrameSize.w > nr3dHalParam.dst_resizer_rect.s.w) {
        MY_LOGW("!!WARN: mkdbg_zoom: VIPI(%d, %d) > IMGI(%d, %d)",
                vipiFrameSize.w, vipiFrameSize.h,
                nr3dHalParam.dst_resizer_rect.s.w,
                nr3dHalParam.dst_resizer_rect.s.h);
      } else if (vipiFrameSize.w < nr3dHalParam.dst_resizer_rect.s.w) {
        MY_LOGW("!!WARN: mkdbg_zoom: VIPI(%d, %d) > IMGI(%d, %d)",
                vipiFrameSize.w, vipiFrameSize.h,
                nr3dHalParam.dst_resizer_rect.s.w,
                nr3dHalParam.dst_resizer_rect.s.h);
      }

      MINT32 nr3dPathID = determine3DNRPath(nr3dHalParam);
      switch (nr3dPathID) {
        case NR3D_PATH_RRZO:  // === Rule: IMGO --> support IMGO-only, NOT
                              // support RRZO/IMGO switch ===
          break;
        case NR3D_PATH_RRZO_CRZ:  // === Rule:  RRZ + CRZ --> 3DNR OFF ===
          errorStatus |= NR3D_ERROR_NOT_SUPPORT;
          return errorStatus;
        case NR3D_PATH_IMGO:  // === Rule: IMGO --> support IMGO-only, NOT
                              // support RRZO/IMGO switch ===
          break;
        default:
          MY_LOGW("invalid path ID(%d)", nr3dPathID);
          errorStatus |= NR3D_ERROR_NOT_SUPPORT;
          return errorStatus;
      }

      if (preSavedFrameInfo.isCRZUsed != nr3dHalParam.isCRZUsed ||
          preSavedFrameInfo.isIMGO != nr3dHalParam.isIMGO) {
        // Rule: IMGO/RRZO input switch: 3DNR default on by Algo's request
        MBOOL isInputChg3DNROn =
            ::property_get_int32("vendor.debug.3dnr.inputchg.on", 1);
        if (isInputChg3DNROn) {
          MY_LOGD(
              "RRZO/IMGO input change: nr3dPathID: %d, CRZUsed=%d -> %d, "
              "isIMGO=%d->%d --> 3DNR on",
              nr3dPathID, preSavedFrameInfo.isCRZUsed, nr3dHalParam.isCRZUsed,
              preSavedFrameInfo.isIMGO, nr3dHalParam.isIMGO);
        } else {
          MY_LOGD(
              "RRZO/IMGO input change: nr3dPathID: %d, CRZUsed=%d -> %d, "
              "isIMGO=%d->%d --> 3DNR off",
              nr3dPathID, preSavedFrameInfo.isCRZUsed, nr3dHalParam.isCRZUsed,
              preSavedFrameInfo.isIMGO, nr3dHalParam.isIMGO);
          errorStatus |= NR3D_ERROR_INPUT_SRC_CHANGE;
          return errorStatus;
        }
      }
    } else {
      // Current frame don't do 3DNR, but IMG3O still needs to output current
      // frame for next run use.
      errorStatus |= NR3D_ERROR_FRAME_SIZE_CHANGED;
      return errorStatus;
    }
  }

  return errorStatus;
}

static const hal3dnrPolicyTable _ghal3dnrPolicyTable[] = {
    {checkIso}, {checkVipiImgiFrameSize}};

static MUINT32 check3DNRPolicy(const NR3DHALParam& nr3dHalParam,
                               const hal3dnrDebugParam& debugParam,
                               const hal3dnrSavedFrameInfo& preSavedFrameInfo) {
  MUINT32 errorStatus = NR3D_ERROR_NONE;
  MINT32 table_size = sizeof(_ghal3dnrPolicyTable) / sizeof(hal3dnrPolicyTable);

  // check policy
  for (MINT32 i = 0; i < table_size; i++) {
    errorStatus = _ghal3dnrPolicyTable[i].policyFunc(nr3dHalParam, debugParam,
                                                     preSavedFrameInfo);

    if (NR3D_ERROR_NONE != errorStatus) {
      break;
    }
  }

  return errorStatus;
}

static void calCMV(const hal3dnrSavedFrameInfo& preSavedFrameInfo,
                   NR3DMVInfo* pGMVInfo) {
  // For EIS 1.2 (use CMV). gmv_crp (t) = gmv(t) - ( cmv(t) - cmv(t-1) )

  // Use GMV and CMV
  pGMVInfo->gmvX = pGMVInfo->gmvX - (pGMVInfo->x_int - preSavedFrameInfo.CmvX);
  pGMVInfo->gmvY = pGMVInfo->gmvY - (pGMVInfo->y_int - preSavedFrameInfo.CmvY);
}

static void calGMV(const NR3DHALParam& nr3dHalParam,
                   MINT32 force3DNR,
                   const hal3dnrSavedFrameInfo& preSavedFrameInfo,
                   NR3DMVInfo* pGMVInfo) {
  // The unit of Gmv is 256x 'pixel', so /256 to change unit to 'pixel'.  >> 1
  // << 1: nr3d_on_w must be even, so make mNmvX even. Discussed with James
  // Liang. The unit of Gmv is 256x 'pixel', so /256 to change unit to 'pixel'.
  // >> 1 << 1: nr3d_on_h must be even when image format is 420, so make mNmvX
  // even. Discussed with TC & Christopher.
  pGMVInfo->gmvX = (-(pGMVInfo->gmvX) / LMV_GMV_VALUE_TO_PIXEL_UNIT);
  pGMVInfo->gmvY = (-(pGMVInfo->gmvY) / LMV_GMV_VALUE_TO_PIXEL_UNIT);

  MINT32 nr3dPathID = determine3DNRPath(nr3dHalParam);

  switch (nr3dPathID) {
    case NR3D_PATH_RRZO:
    case NR3D_PATH_IMGO:
      // Use GMV only.
      break;
    case NR3D_PATH_RRZO_CRZ:
      calCMV(preSavedFrameInfo, pGMVInfo);
      break;
    default:
      MY_LOGE("invalid path ID(%d)", nr3dPathID);
      break;
  }

  pGMVInfo->gmvX = pGMVInfo->gmvX & ~1;  // Make it even.
  pGMVInfo->gmvY = pGMVInfo->gmvY & ~1;  // Make it even.
}

MUINT32 handleState(MUINT32 errorStatus,
                    MINT32 force3DNR,
                    NR3D_STATE_ENUM* pStateMachine) {
  MUINT32 result = errorStatus;

  if (NR3D_ERROR_NONE == result) {
    if (*pStateMachine ==
        NR3D_STATE_PREPARING) {  // Last frame is NR3D_STATE_PREPARING.
      *pStateMachine = NR3D_STATE_WORKING;  // NR3D, IMG3O, VIPI all enabled.
    } else if (*pStateMachine == NR3D_STATE_STOP) {
      *pStateMachine = NR3D_STATE_PREPARING;
    }

    if (force3DNR) {
      if (::property_get_int32("vendor.camera.3dnr.forceskip", 0)) {
        // Current frame don't do 3DNR, but IMG3O still needs to output current
        // frame for next run use.
        result |= NR3D_ERROR_FORCE_SKIP;
        if (*pStateMachine == NR3D_STATE_WORKING) {
          *pStateMachine = NR3D_STATE_PREPARING;
        }
      }
    }
  } else {
    if (*pStateMachine == NR3D_STATE_WORKING) {
      *pStateMachine = NR3D_STATE_PREPARING;
    }
  }

  return result;
}

static MBOOL getNR3DParam(const NR3DHALParam& nr3dHalParam,
                          const NR3DAlignParam& nr3dAlignParam,
                          NR3DParam* pOutNr3dParam) {
  MUINT32 u4PixelToBytes = 0;
  MINT imgFormat = nr3dHalParam.pIMGBufferVIPI->getImgFormat();
  size_t stride = nr3dHalParam.pIMGBufferVIPI->getBufStridesInBytes(0);

  // Calculate u4PixelToBytes.
  if (imgFormat == NSCam::eImgFmt_YUY2) {
    u4PixelToBytes = 2;
  } else if (imgFormat == NSCam::eImgFmt_YV12) {
    u4PixelToBytes = 1;
  }
  // Calculate VIPI start addr offset.
  MUINT32 u4VipiOffset_X = nr3dAlignParam.u4VipiOffset_X;
  MUINT32 u4VipiOffset_Y = nr3dAlignParam.u4VipiOffset_Y;
  MUINT32 vipi_offst =
      u4VipiOffset_Y * stride + u4VipiOffset_X * u4PixelToBytes;  // in byte

  // save NR3D setting into NR3DParam
  // handle 2 bytes alignment in isp_mgr_nr3d
  pOutNr3dParam->vipi_offst = vipi_offst;
  pOutNr3dParam->vipi_readW = nr3dAlignParam.vipi_readW;
  pOutNr3dParam->vipi_readH = nr3dAlignParam.vipi_readH;
  pOutNr3dParam->onSiz_onWd = nr3dAlignParam.onSiz_onWd;
  pOutNr3dParam->onSiz_onHt = nr3dAlignParam.onSiz_onHt;
  pOutNr3dParam->onOff_onOfStX = nr3dAlignParam.onOff_onOfStX;
  pOutNr3dParam->onOff_onOfStY = nr3dAlignParam.onOff_onOfStY;
  pOutNr3dParam->ctrl_onEn = 1;

  return MTRUE;
}

static MBOOL handleFrameAlign(const NR3DHALParam& nr3dHalParam,
                              const NR3DMVInfo& GMVInfo,
                              NR3DAlignParam* pOutNr3dAlignParam) {
  // Config VIPI for 3DNR previous frame input.
  MUINT32 u4VipiOffset_X = 0, u4VipiOffset_Y = 0;
  MUINT32 u4Nr3dOffset_X = 0, u4Nr3dOffset_Y = 0;
  MSize imgSize;
  MINT32 mvX = GMVInfo.gmvX;
  MINT32 mvY = GMVInfo.gmvY;

  // Calculate VIPI Offset X/Y according to NMV X/Y.
  u4VipiOffset_X = ((mvX >= 0) ? (mvX) : (0));
  u4VipiOffset_Y = ((mvY >= 0) ? (mvY) : (0));
  // Calculate NR3D Offset X/Y according to NMV X/Y.
  u4Nr3dOffset_X = ((mvX >= 0) ? (0) : (-mvX));
  u4Nr3dOffset_Y = ((mvY >= 0) ? (0) : (-mvY));

  MUINT32 vipiW = nr3dHalParam.pIMGBufferVIPI->getImgSize().w;
  MUINT32 vipiH = nr3dHalParam.pIMGBufferVIPI->getImgSize().h;

  // Calculate VIPI valid region w/h.
  imgSize.w = vipiW - abs(mvX);  // valid region w
  imgSize.h = vipiH - abs(mvY);  // valid region h

  // save align info into NR3DAlignParam
  pOutNr3dAlignParam->onOff_onOfStX = u4Nr3dOffset_X;
  pOutNr3dAlignParam->onOff_onOfStY = u4Nr3dOffset_Y;

  pOutNr3dAlignParam->onSiz_onWd = imgSize.w;
  pOutNr3dAlignParam->onSiz_onHt = imgSize.h;

  pOutNr3dAlignParam->u4VipiOffset_X = u4VipiOffset_X;
  pOutNr3dAlignParam->u4VipiOffset_Y = u4VipiOffset_Y;

  pOutNr3dAlignParam->vipi_readW = imgSize.w;  // in pixel
  pOutNr3dAlignParam->vipi_readH = imgSize.h;  // in pixel

  return MTRUE;
}
/*****************************************************************************
 *
 *****************************************************************************/
static void dumpVIPIBuffer(NSCam::IImageBuffer* pIMGBufferVIPI,
                           MUINT32 requestNo) {
  static MSize s_con_write_size;
  static MINT32 s_con_write_countdown = 0;
  if (!s_con_write_countdown &&
      (s_con_write_size != pIMGBufferVIPI->getImgSize())) {
    s_con_write_size = pIMGBufferVIPI->getImgSize();
    s_con_write_countdown = 5;
  }

  if (s_con_write_countdown) {
    MINT imgFormat = pIMGBufferVIPI->getImgFormat();
    MUINT32 u4PixelToBytes = 0;

    if (imgFormat == NSCam::eImgFmt_YUY2) {
      u4PixelToBytes = 2;
    }
    if (imgFormat == NSCam::eImgFmt_YV12) {
      u4PixelToBytes = 1;
    }

    if (NSCam::Utils::makePath(DUMP_PATH, 0660) == false) {
      MY_LOGW("makePath() error");
    }
    char filename[256] = {0};
    snprintf(filename, sizeof(filename), "%s/vipi_%dx%d_S%zu_p2b_%d_N%d.yuv",
             DUMP_PATH, pIMGBufferVIPI->getImgSize().w,
             pIMGBufferVIPI->getImgSize().h,
             pIMGBufferVIPI->getBufStridesInBytes(0), u4PixelToBytes,
             requestNo);
    pIMGBufferVIPI->saveToFile(filename);
    s_con_write_countdown--;
  }
}

/*****************************************************************************
 *
 *****************************************************************************/

std::shared_ptr<hal3dnrBase> Hal3dnr::getInstance() {
  clientCnt++;
  MY_LOGD("clientCnt:%d", clientCnt);

  if (pHal3dnr == NULL) {
    pHal3dnr = std::make_shared<Hal3dnr>();
  }
  return pHal3dnr;
}

/*****************************************************************************
 *
 ****************************************************************************/
std::shared_ptr<hal3dnrBase> Hal3dnr::getInstance(char const* userName,
                                                  const MUINT32 sensorIdx) {
  MY_LOGD("%s sensorIdx %d", userName, sensorIdx);
  return std::make_shared<Hal3dnr>();
}

/****************************************************************************
 *
 *****************************************************************************/
Hal3dnr::Hal3dnr() : mSensorIdx(0) {
  mPrevFrameWidth = 0;
  mPrevFrameHeight = 0;
  mNmvX = 0;
  mNmvY = 0;
  mCmvX = 0;
  mCmvY = 0;
  mPrevCmvX = 0;
  mPrevCmvY = 0;
  m3dnrGainZeroCount = 0;
  m3dnrErrorStatus = 0;
  m3dnrStateMachine = NR3D_STATE_STOP;
  // TODO(MTK): use static instead of new
  mpNr3dParam = std::make_shared<NR3DParam>();
  mIsCMVMode = MFALSE;
  mLogLevel = 0;
  mForce3DNR = 0;
  mSupportZoom3DNR = MFALSE;
  mUsers = 0;
}

/***************************************************************************
 *
 ****************************************************************************/
Hal3dnr::Hal3dnr(const MUINT32 sensorIdx) : mSensorIdx(sensorIdx) {
  mPrevFrameWidth = 0;
  mPrevFrameHeight = 0;
  mNmvX = 0;
  mNmvY = 0;
  mCmvX = 0;
  mCmvY = 0;
  mPrevCmvX = 0;
  mPrevCmvY = 0;
  m3dnrGainZeroCount = 0;
  m3dnrErrorStatus = 0;
  m3dnrStateMachine = NR3D_STATE_STOP;
  mpNr3dParam = std::make_shared<NR3DParam>();
  mIsCMVMode = MFALSE;
  mLogLevel = 0;
  mForce3DNR = 0;
  mSupportZoom3DNR = MFALSE;
  mUsers = 0;
}

/*****************************************************************************
 *
 ******************************************************************************/
Hal3dnr::~Hal3dnr() {}

MVOID Hal3dnr::setCMVMode(MBOOL useCMV) {
  mIsCMVMode = useCMV;
}

/*******************************************************************************
 *
 ******************************************************************************/
MBOOL
Hal3dnr::init(MINT32 force3DNR) {
  std::lock_guard<std::mutex> autoLock(mLock);

  FUNC_START;

  MY_LOGD("m3dnrStateMachine=%d->NR3D_STATE_PREPARING", m3dnrStateMachine);

  //====== Check Reference Count ======

  if (mUsers > 0) {
    std::atomic_fetch_add_explicit(&mUsers, 1, std::memory_order_release);
    MY_LOGW("snesorIdx(%u) has one more users", mSensorIdx);
    return MTRUE;
  }

  memset(mpNr3dParam.get(), 0, sizeof(NR3DParam));

  mPrevFrameWidth = 0;
  mPrevFrameHeight = 0;
  m3dnrGainZeroCount = 0;
  m3dnrErrorStatus = NR3D_ERROR_NONE;
  m3dnrStateMachine = NR3D_STATE_PREPARING;
  mNmvX = 0;
  mNmvY = 0;
  mCmvX = 0;
  mCmvY = 0;
  mPrevCmvX = 0;
  mPrevCmvY = 0;

  mLogLevel = ::property_get_int32("vendor.camera.3dnr.log.level", 0);
  mForce3DNR = force3DNR;
  mSupportZoom3DNR = ::property_get_int32("vendor.debug.3dnr.zoom",
                                          1);  // set zoom_3dnr default ON

  mIIspMgr = MAKE_IspMgr("3dnr_hal");
  //====== Increase User Count ======

  std::atomic_fetch_add_explicit(&mUsers, 1, std::memory_order_release);

  FUNC_END;
  return MTRUE;
}

/*******************************************************************************
 *
 ******************************************************************************/
MBOOL
Hal3dnr::uninit() {
  std::lock_guard<std::mutex> autoLock(mLock);

  FUNC_START;

  MY_LOGD("m3dnrStateMachine=%d->NR3D_STATE_PREPARING", m3dnrStateMachine);
  //====== Check Reference Count ======

  if (mUsers <= 0) {
    MY_LOGW("mSensorIdx(%u) has 0 user", mSensorIdx);
    return MTRUE;
  }
  //====== Uninitialize ======

  std::atomic_fetch_sub_explicit(&mUsers, 1, std::memory_order_release);

  // delete mpNr3dParam; // Return allocated memory.

#if (MTKCAM_ENABLE_IPC == 1)
  if (mIIspMgr)
    mIIspMgr->uninit("3dnr_hal");
#endif

  mPrevFrameWidth = 0;
  mPrevFrameHeight = 0;
  m3dnrGainZeroCount = 0;
  m3dnrErrorStatus = NR3D_ERROR_NONE;
  m3dnrStateMachine = NR3D_STATE_PREPARING;
  mNmvX = 0;
  mNmvY = 0;
  mCmvX = 0;
  mCmvY = 0;
  mPrevCmvX = 0;
  mPrevCmvY = 0;
  FUNC_END;
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
Hal3dnr::prepare(MUINT32 frameNo, MINT32 iso) {
  FUNC_START;
  MSize imgSize, TempImgSize;

  /************************************************************************/
  /*                          Preprocessing                               */
  /************************************************************************/

  // *****************STEP 1
  //////////////////////////////////////////////////////////////////////////
  // 3DNR State Machine operation                                         //
  //////////////////////////////////////////////////////////////////////////
  NR3D_STATE_ENUM e3dnrStateMachine = m3dnrStateMachine;
  if (e3dnrStateMachine ==
      NR3D_STATE_PREPARING) {  // Last frame is NR3D_STATE_PREPARING.
    MY_LOGD("m3dnrStateMachine=(NR3D_STATE_PREPARING->NR3D_STATE_WORKING)");
    m3dnrStateMachine = NR3D_STATE_WORKING;  // NR3D, IMG3O, VIPI all enabled.
  }
  MY_LOGD("STEP 1,2: m3dnrStateMachine=(%d->%d), frame:%d", e3dnrStateMachine,
          m3dnrStateMachine, frameNo);
  // *****************STEP 2
  // Reset m3dnrErrorStatus.
  m3dnrErrorStatus = NR3D_ERROR_NONE;

  MINT32 i4IsoThreshold = NR3DCustom::get_3dnr_off_iso_threshold(mForce3DNR);

  if (iso < i4IsoThreshold) {
    m3dnrStateMachine = NR3D_STATE_STOP;
  } else {
    if (m3dnrStateMachine == NR3D_STATE_STOP) {
      m3dnrStateMachine = NR3D_STATE_PREPARING;
      m3dnrGainZeroCount = 0;  // Reset m3dnrGainZeroCount.
    }
  }
  MY_LOGD("STEP 3: StateMachine=%d, iso=%d, Poweroff threshold=%d, frame:%d",
          m3dnrStateMachine, iso, i4IsoThreshold, frameNo);

  FUNC_END;
  return MTRUE;
}

MBOOL
Hal3dnr::setGMV(MUINT32 frameNo,
                MINT32 gmvX,
                MINT32 gmvY,
                MINT32 cmvX_Int,
                MINT32 cmvY_Int) {
  FUNC_START;

  // *****************STEP 4
  //////////////////////////////////////////////////////////////////////////
  //  3DNR GMV Calculation                                                //
  //////////////////////////////////////////////////////////////////////////
  MINT32 i4TempNmvXFromQueue = 0, i4TempNmvYFromQueue = 0;
  MINT32 i4TempX = 0, i4TempY = 0;

  i4TempNmvXFromQueue =
      (-(gmvX) /
       LMV_GMV_VALUE_TO_PIXEL_UNIT);  // The unit of Gmv is 256x 'pixel', so
                                      // /256 to change unit to 'pixel'.  >> 1
                                      // << 1: nr3d_on_w must be even, so make
                                      // mNmvX even. Discussed with James Liang.
  i4TempNmvYFromQueue =
      (-(gmvY) /
       LMV_GMV_VALUE_TO_PIXEL_UNIT);  // The unit of Gmv is 256x 'pixel', so
                                      // /256 to change unit to 'pixel'.  >> 1
                                      // << 1: nr3d_on_h must be even when image
                                      // format is 420, so make mNmvX even.
                                      // Discussed with TC & Christopher.

  if (mForce3DNR) {
    MINT32 value =
        ::property_get_int32("vendor.camera.3dnr.forcegmv.enable", 0);
    if (value) {
      i4TempNmvXFromQueue =
          ::property_get_int32("vendor.camera.3dnr.forcegmv.x", 0);

      i4TempNmvYFromQueue =
          ::property_get_int32("vendor.camera.3dnr.forcegmv.y", 0);

      MY_LOGD("Force GMV X/Y (%d, %d)", i4TempNmvXFromQueue,
              i4TempNmvYFromQueue);
    }
  }

#if (NR3D_FORCE_GMV_ZERO)  // Force GMV to be 0.
  mNmvX = 0;
  mNmvY = 0;
#else   // Normal flow.

  if (mIsCMVMode) {  // For EIS 1.2 (use CMV). gmv_crp (t) = gmv(t) - ( cmv(t) -
                     // cmv(t-1) )
    // Use GMV and CMV
    mCmvX = cmvX_Int;  // Curr frame CMV X. Make it even.
    mCmvY = cmvY_Int;  // Curr frame CMV Y. Make it even.
    mNmvX = (i4TempNmvXFromQueue - (mCmvX - mPrevCmvX)) & ~1;  // Make it even.
    mNmvY = (i4TempNmvYFromQueue - (mCmvY - mPrevCmvY)) & ~1;  // Make it even.
    i4TempX = mCmvX - mPrevCmvX;
    i4TempY = mCmvY - mPrevCmvY;

    mPrevCmvX = mCmvX;  // Recore last frame CMV X.
    mPrevCmvY = mCmvY;  // Recore last frame CMV Y.

  } else {  // For EIS 2.0 (use GMV)
    // Use GMV only.
    mNmvX = i4TempNmvXFromQueue & ~1;  // Make it even.
    mNmvY = i4TempNmvYFromQueue & ~1;  // Make it even.

    mCmvX = 0;
    mCmvY = 0;
    mPrevCmvX = 0;  // Recore last frame CMV X.
    mPrevCmvY = 0;  // Recore last frame CMV Y.
  }
#endif  // NR3D_FORCE_GMV_ZERO

  MY_LOGD(
      "STEP 4: mSensorIdx=%u gmv cal,ST=%d, "
      "gmv(x,y)=(%5d,%5d),CmvX/Y(%5d,%5d),NmvX/Y(%5d,%5d), (cmv diff %5d,%5d), "
      "frame:%d",
      mSensorIdx, m3dnrStateMachine, gmvX, gmvY, cmvX_Int, cmvY_Int, mNmvX,
      mNmvY, i4TempX, i4TempY, frameNo);

  FUNC_END;
  return MTRUE;
}

/************************************************************************
 *
 *************************************************************************/
MBOOL
Hal3dnr::checkIMG3OSize(MUINT32 frameNo, MUINT32 imgiW, MUINT32 imgiH) {
  FUNC_START;

  // *****************STEP 5
  //////////////////////////////////////////////////////////////////////////
  //  Calculate target width/height to set IMG3O                          //
  //////////////////////////////////////////////////////////////////////////
  /* Calculate P2A output width and height */

  //: in hal3 img3o size is the same to imgi in

  // *****************STEP 6
  // W/H of buffer (i.e. Current frame size) is determined, so check previous
  // vs. current frame size for 3DNR.
  if ((mPrevFrameWidth != imgiW) || (mPrevFrameHeight != imgiH)) {
    MY_LOGW(
        "PrevFrameW/H(%d,%d),imgiW/H(%d,%d), frame:%d, m3dnrStateMachine=%d",
        mPrevFrameWidth, mPrevFrameHeight, imgiW, imgiH, frameNo,
        m3dnrStateMachine);
    m3dnrErrorStatus |= NR3D_ERROR_FRAME_SIZE_CHANGED;
    // Current frame don't do 3DNR, but IMG3O still needs to output current
    // frame for next run use.
    if (m3dnrStateMachine == NR3D_STATE_WORKING) {
      m3dnrStateMachine = NR3D_STATE_PREPARING;
    }
  }

  FUNC_END;
  return MTRUE;
}

/**************************************************************************
 *
 ***************************************************************************/
MBOOL
Hal3dnr::setVipiParams(MBOOL isVIPIIn,
                       MUINT32 vipiW,
                       MUINT32 vipiH,
                       MINT imgFormat,
                       size_t stride) {
  FUNC_START;
  if (isVIPIIn) {
    // Config VIPI for 3DNR previous frame input.
    MUINT32 u4VipiOffset_X = 0;
    MUINT32 u4VipiOffset_Y = 0;
    MUINT32 u4PixelToBytes = 0;
    MSize imgSize, tempImgSize;

    // Calculate VIPI Start Address = nmv_x + nmv_y * vipi_stride. Unit: bytes.
    //     Calculate Offset X/Y according to NMV X/Y.
    u4VipiOffset_X = (mNmvX >= 0) ? (mNmvX) : (0);
    u4VipiOffset_Y = (mNmvY >= 0) ? (mNmvY) : (0);
    //     Calculate u4PixelToBytes.
    if (imgFormat == NSCam::eImgFmt_YUY2) {
      u4PixelToBytes = 2;
    }
    if (imgFormat == NSCam::eImgFmt_YV12) {
      u4PixelToBytes = 1;
    }
    //     Calculate VIPI start addr offset.
    mpNr3dParam->vipi_offst =
        u4VipiOffset_Y * stride + u4VipiOffset_X * u4PixelToBytes;  // in byte
    MY_LOGD("vipi offset=%d,(xy=%d,%d), stride=%zu, u4PixelToBytes=%d",
            mpNr3dParam->vipi_offst, u4VipiOffset_X, u4VipiOffset_Y, stride,
            u4PixelToBytes);

    //     Calculate VIPI valid region w/h.
    tempImgSize.w = vipiW - abs(mNmvX);  // valid region w
    tempImgSize.h = vipiH - abs(mNmvY);  // valid region h
    imgSize.w = tempImgSize.w & ~1;      // valid region w
    imgSize.h = tempImgSize.h & ~1;      // valid region h

    mpNr3dParam->vipi_readW = imgSize.w;  // in pixel
    mpNr3dParam->vipi_readH = imgSize.h;  // in pixel

    // Check whether current frame size is equal to last frame size.
    if (mForce3DNR) {
      MINT32 value = ::property_get_int32("vendor.camera.3dnr.forceskip", 0);
      if (value) {
        m3dnrErrorStatus |= NR3D_ERROR_FORCE_SKIP;
        if (m3dnrStateMachine == NR3D_STATE_WORKING) {
          m3dnrStateMachine = NR3D_STATE_PREPARING;
        }  // Current frame don't do 3DNR, but IMG3O still needs to output
           // current frame for next run use.
      }
    }

    if (m3dnrStateMachine == NR3D_STATE_WORKING) {
      MY_LOGD("[P2A sets VIPI mvIn  ] 3dnrSM1(%d S0P1W2),ES(0x%02X FsSzDfLrIn)",
              m3dnrStateMachine, m3dnrErrorStatus);
      mpNr3dParam->onSiz_onWd = imgSize.w & ~1;  // Must be even.
      mpNr3dParam->onSiz_onHt = imgSize.h & ~1;
    } else {  // Not NR3D_STATE_WORKING.
      MY_LOGD(
          "[P2A not sets VIPI mvIn  ] 3dnrSM1(%d S0P1W2),ES(0x%02X FsSzDfLrIn)",
          m3dnrStateMachine, m3dnrErrorStatus);
      mpNr3dParam->onSiz_onWd = 0;
      mpNr3dParam->onSiz_onHt = 0;
      return MFALSE;
    }
  } else {
    MY_LOGD(
        "[P2A not sets VIPI mvIn  ] 3dnrSM1(%d S0P1W2),ES(0x%02X FsSzDfLrIn). "
        "m3dnrPrvFrmQueue is empty",
        m3dnrStateMachine,
        m3dnrErrorStatus);  // m3dnrPrvFrmQueue is empty => maybe first run.
    mpNr3dParam->onSiz_onWd = 0;
    mpNr3dParam->onSiz_onHt = 0;
    if (m3dnrStateMachine == NR3D_STATE_WORKING) {
      m3dnrStateMachine = NR3D_STATE_PREPARING;
    }
    return MFALSE;
  }

  FUNC_END;
  return MTRUE;
}

/****************************************************************************
 *
 ****************************************************************************/
MBOOL
Hal3dnr::get3dnrParams(MUINT32 frameNo,
                       MUINT32 imgiW,
                       MUINT32 imgiH,
                       std::shared_ptr<NR3DParam>* pNr3dParam) {
  FUNC_START;
  MBOOL ret = MTRUE;
  // update mpNr3dParam info
  if (m3dnrStateMachine ==
      NR3D_STATE_WORKING) {  // Only set NR3D register when N3RD state machine
                             // is NR3D_STATE_WORKING.
    mpNr3dParam->ctrl_onEn = 1;
    // david modified, u4Img3oOffset_X can skip because tile driver doesn't have
    // limitation about crop region.
    mpNr3dParam->onOff_onOfStX =
        ((mNmvX >= 0) ? (0) : (-mNmvX));  // Must be even.
    mpNr3dParam->onOff_onOfStY = ((mNmvY >= 0) ? (0) : (-mNmvY));
  } else {
    mpNr3dParam->ctrl_onEn = 0;
    mpNr3dParam->onOff_onOfStX = 0;  // Must be even.
    mpNr3dParam->onOff_onOfStY = 0;
    mpNr3dParam->onSiz_onWd = 0;  // Must be even.
    mpNr3dParam->onSiz_onHt = 0;
    mpNr3dParam->vipi_offst = 0;  // in byte
    mpNr3dParam->vipi_readW = 0;  // in pixel
    mpNr3dParam->vipi_readH = 0;  // in pixel
    ret = MFALSE;
  }
  MY_LOGD(
      "3dnrSM2(%d S0P1W2),ES(0x%02X FsSzDfLrIn),NmvX/Y(%d, %d),onOfX/Y(%d, "
      "%d).onW/H(%d, %d).VipiOff/W/H(%d, %d, %d).MaxIsoInc(%d)",
      m3dnrStateMachine, m3dnrErrorStatus, mNmvX, mNmvY,
      mpNr3dParam->onOff_onOfStX, mpNr3dParam->onOff_onOfStY,
      mpNr3dParam->onSiz_onWd, mpNr3dParam->onSiz_onHt, mpNr3dParam->vipi_offst,
      mpNr3dParam->vipi_readW, mpNr3dParam->vipi_readH,
      get_3dnr_max_iso_increase_percentage());

  pNr3dParam = &mpNr3dParam;
  // Recordng mPrevFrameWidth/mPrevFrameHeight for next frame.
  mPrevFrameWidth = imgiW;
  mPrevFrameHeight = imgiH;
  FUNC_END;
  return ret;
}

/***************************************************************************
 *
 ****************************************************************************/
MBOOL
Hal3dnr::get3dnrParams(MUINT32 frameNo,
                       MUINT32 imgiW,
                       MUINT32 imgiH,
                       std::shared_ptr<NR3DParam> nr3dParam) {
  FUNC_START;
  MBOOL ret = MTRUE;
  // update mpNr3dParam info
  if (m3dnrStateMachine ==
      NR3D_STATE_WORKING) {  // Only set NR3D register when N3RD state machine
                             // is NR3D_STATE_WORKING.
    mpNr3dParam->ctrl_onEn = 1;
    // david modified, u4Img3oOffset_X can skip because tile driver doesn't have
    // limitation about crop region.
    mpNr3dParam->onOff_onOfStX =
        ((mNmvX >= 0) ? (0) : (-mNmvX));  // Must be even.
    mpNr3dParam->onOff_onOfStY = ((mNmvY >= 0) ? (0) : (-mNmvY));
  } else {
    mpNr3dParam->ctrl_onEn = 0;
    mpNr3dParam->onOff_onOfStX = 0;  // Must be even.
    mpNr3dParam->onOff_onOfStY = 0;
    mpNr3dParam->onSiz_onWd = 0;  // Must be even.
    mpNr3dParam->onSiz_onHt = 0;
    mpNr3dParam->vipi_offst = 0;  // in byte
    mpNr3dParam->vipi_readW = 0;  // in pixel
    mpNr3dParam->vipi_readH = 0;  // in pixel
    ret = MFALSE;
  }
  MY_LOGD(
      "3dnrSM2(%d S0P1W2),ES(0x%02X FsSzDfLrIn),NmvX/Y(%d, %d),onOfX/Y(%d, "
      "%d).onW/H(%d, %d).VipiOff/W/H(%d, %d, %d).MaxIsoInc(%d)",
      m3dnrStateMachine, m3dnrErrorStatus, mNmvX, mNmvY,
      mpNr3dParam->onOff_onOfStX, mpNr3dParam->onOff_onOfStY,
      mpNr3dParam->onSiz_onWd, mpNr3dParam->onSiz_onHt, mpNr3dParam->vipi_offst,
      mpNr3dParam->vipi_readW, mpNr3dParam->vipi_readH,
      get_3dnr_max_iso_increase_percentage());

  nr3dParam = mpNr3dParam;

  // Recordng mPrevFrameWidth/mPrevFrameHeight for next frame.
  mPrevFrameWidth = imgiW;
  mPrevFrameHeight = imgiH;
  FUNC_END;
  return ret;
}

MBOOL
Hal3dnr::checkStateMachine(NR3D_STATE_ENUM status) {
  return (status == m3dnrStateMachine);
}

MBOOL
Hal3dnr::do3dnrFlow(void* pTuningData,
                    MBOOL useCMV,
                    MRect const& dst_resizer_rect,
                    NSCam::NR3D::NR3DMVInfo const& GMVInfo,
                    NSCam::IImageBuffer* pIMGBufferVIPI,
                    MINT32 iso,
                    MUINT32 requestNo,
                    std::shared_ptr<NS3Av3::IHal3A> p3A) {
  MBOOL ret = MFALSE;
  MBOOL bDrvNR3DEnabled = 1;

  if (mForce3DNR) {
    bDrvNR3DEnabled =
        ::property_get_int32("vendor.camera.3dnr.drv.nr3d.enable", 1);
  }

  if (MTRUE != prepare(requestNo, iso)) {
    MY_LOGW("3dnr prepare err");
  }

  setCMVMode(useCMV);

  NR3DCustom::AdjustmentInput adjInput;
  adjInput.force3DNR = mForce3DNR ? true : false;
  adjInput.setGmv(GMVInfo.confX, GMVInfo.confY, GMVInfo.gmvX, GMVInfo.gmvY);
  fillGyroForAdjustment(&adjInput);

  NR3DCustom::AdjustmentOutput adjOutput;
  NR3DCustom::adjust_parameters(adjInput, &adjOutput);
  MINT32 adjustGMVX = GMVInfo.gmvX;
  MINT32 adjustGMVY = GMVInfo.gmvY;
  if (adjOutput.isGmvOverwritten) {
    adjustGMVX = adjOutput.gmvX;
    adjustGMVY = adjOutput.gmvY;
    MY_LOGD("AfterAdjusting: (confX,confY)=(%d,%d), gmvX(%d->%d), gmvY(%d->%d)",
            GMVInfo.confX, GMVInfo.confY, GMVInfo.gmvX, adjustGMVX,
            GMVInfo.gmvY, adjustGMVY);
  }

  if (MTRUE !=
      setGMV(requestNo, adjustGMVX, adjustGMVY, GMVInfo.x_int, GMVInfo.y_int)) {
    MY_LOGW("3dnr getGMV err");
  }

  // TODO(MTK): need to check IMG3O x,y also for IMGO->IMGI path
  if (MTRUE !=
      checkIMG3OSize(requestNo, dst_resizer_rect.s.w, dst_resizer_rect.s.h)) {
    MY_LOGW("3dnr checkIMG3OSize err");
  }

  if (pIMGBufferVIPI != NULL) {
    if (MTRUE != setVipiParams(MTRUE, pIMGBufferVIPI->getImgSize().w,
                               pIMGBufferVIPI->getImgSize().h,
                               pIMGBufferVIPI->getImgFormat(),
                               pIMGBufferVIPI->getBufStridesInBytes(0))) {
      MY_LOGD("skip configVipi flow");
    } else {
      if (mForce3DNR &&
          ::property_get_int32("vendor.debug.3dnr.vipi.dump", 0)) {
        // dump vipi img
        dumpVIPIBuffer(pIMGBufferVIPI, requestNo);
      }

      MY_LOGD("configVipi: address:%p, W/H(%d,%d)", pIMGBufferVIPI,
              pIMGBufferVIPI->getImgSize().w, pIMGBufferVIPI->getImgSize().h);
      /* config Input for VIPI: this part is done in prepareIO(..) */
    }
  } else {
    if (MTRUE != setVipiParams(MFALSE /* vipi is NULL */, 0, 0, 0, 0)) {
      MY_LOGW("3dnr configVipi err");
    }
  }

  std::shared_ptr<NR3DParam> Nr3dParam;

  if (MTRUE != get3dnrParams(requestNo, dst_resizer_rect.s.w,
                             dst_resizer_rect.s.h, &Nr3dParam)) {
    MY_LOGD("skip config3dnrParams flow");
  }

  MY_LOGD(
      "Nr3dParam: onOff_onOfStX/Y(%d, %d), onSiz_onW/H(%d, %d), "
      "vipi_readW/H(%d, %d)",
      Nr3dParam->onOff_onOfStX, Nr3dParam->onOff_onOfStY, Nr3dParam->onSiz_onWd,
      Nr3dParam->onSiz_onHt, Nr3dParam->vipi_readW, Nr3dParam->vipi_readH);

  NS3Av3::NR3D_Config_Param param;

  if ((MTRUE == checkStateMachine(NR3D_STATE_WORKING)) && bDrvNR3DEnabled) {
    param.enable = bDrvNR3DEnabled;
    param.onRegion.p.x = Nr3dParam->onOff_onOfStX;
    param.onRegion.p.y = Nr3dParam->onOff_onOfStY;
    param.onRegion.s.w = Nr3dParam->onSiz_onWd;
    param.onRegion.s.h = Nr3dParam->onSiz_onHt;
    param.fullImg.p.x = dst_resizer_rect.p.x & ~1;
    param.fullImg.p.y = dst_resizer_rect.p.y & ~1;
    param.fullImg.s.w = dst_resizer_rect.s.w & ~1;
    param.fullImg.s.h = dst_resizer_rect.s.h & ~1;

    param.vipiOffst = Nr3dParam->vipi_offst;
    param.vipiReadSize.w = Nr3dParam->vipi_readW;
    param.vipiReadSize.h = Nr3dParam->vipi_readH;

    if (p3A) {
      // turn ON 'pull up ISO value to gain FPS'

      NS3Av3::AE_Pline_Limitation_T params;
      params.bEnable = MTRUE;
      params.bEquivalent = MTRUE;
      // use property "camera.3dnr.forceisolimit" to control
      // max_iso_increase_percentage ex: setprop camera.3dnr.forceisolimit 200
      params.u4IncreaseISO_x100 = get_3dnr_max_iso_increase_percentage();
      params.u4IncreaseShutter_x100 = 100;
      p3A->send3ACtrl(E3ACtrl_SetAEPlineLimitation, (MINTPTR)&params, 0);

      MY_LOGD("turn ON 'pull up ISO value to gain FPS': max: %d %%",
              get_3dnr_max_iso_increase_percentage());
    }

    ret = MTRUE;
  } else {
    if (p3A) {
      // turn OFF 'pull up ISO value to gain FPS'

      // mp3A->modifyPlineTableLimitation(MTRUE, MTRUE,  100, 100);
      NS3Av3::AE_Pline_Limitation_T params;
      params.bEnable = MFALSE;  // disable
      params.bEquivalent = MTRUE;
      params.u4IncreaseISO_x100 = 100;
      params.u4IncreaseShutter_x100 = 100;
      p3A->send3ACtrl(E3ACtrl_SetAEPlineLimitation, (MINTPTR)&params, 0);

      MY_LOGD("turn OFF  'pull up ISO value to gain FPS'");
    }
  }

  if (pTuningData) {
    void* pIspPhyReg = pTuningData;

    // log keyword for auto test
    MY_LOGD("postProcessNR3D: EN(%d)", param.enable);

#if (MTKCAM_ENABLE_IPC == 1)
    if (mIIspMgr)
      mIIspMgr->postProcessNR3D(mSensorIdx, &param, pIspPhyReg);
#else
    if (auto pIspMgr = MAKE_IspMgr()) {
      pIspMgr->postProcessNR3D(mSensorIdx, param, pIspPhyReg);
    }
#endif
  }

  return ret;
}

MBOOL
Hal3dnr::do3dnrFlow_v2(const NR3DHALParam& nr3dHalParam) {
  FUNC_START;

  MBOOL ret = MTRUE;
  NR3DParam Nr3dParam;  // default off

  if (savedFrameInfo(nr3dHalParam) != MTRUE) {
    MY_LOGW_IF(mLogLevel >= 1, "3DNR off: savedFrameInfo failed");
    // set NR3D off
    ret = MFALSE;
    goto configNR3DOnOff;
  }

  if (handle3DNROnOffPolicy(nr3dHalParam) != MTRUE) {
    MY_LOGW_IF(mLogLevel >= 1, "3DNR off: handle3DNROnOffPolicy failed");
    // set NR3D off
    ret = MFALSE;
    goto configNR3DOnOff;
  }

  if (handleAlignVipiIMGI(nr3dHalParam, &Nr3dParam) != MTRUE) {
    MY_LOGW("3DNR off: handleAlignVipiIMGI failed");
    // set NR3D off
    ret = MFALSE;
    goto configNR3DOnOff;
  }

configNR3DOnOff:

  if (configNR3D(nr3dHalParam, Nr3dParam) != MTRUE) {
    MY_LOGW("3DNR off: configNR3D failed");
    ret = MFALSE;
  }

  FUNC_END;

  return ret;
}

MBOOL Hal3dnr::savedFrameInfo(const NR3DHALParam& nr3dHalParam) {
  // save data from current frame to previous frame
  mPreSavedFrameInfo = mCurSavedFrameInfo;

  mCurSavedFrameInfo.CmvX = nr3dHalParam.GMVInfo.x_int;
  mCurSavedFrameInfo.CmvY = nr3dHalParam.GMVInfo.y_int;
  mCurSavedFrameInfo.isCRZUsed = nr3dHalParam.isCRZUsed;
  mCurSavedFrameInfo.isIMGO = nr3dHalParam.isIMGO;
  mCurSavedFrameInfo.isBinning = nr3dHalParam.isBinning;

  return MTRUE;
}

MBOOL Hal3dnr::handle3DNROnOffPolicy(const NR3DHALParam& nr3dHalParam) {
  hal3dnrDebugParam debugParam;
  debugParam.mLogLevel = mLogLevel;
  debugParam.mForce3DNR = mForce3DNR;
  debugParam.mSupportZoom3DNR = mSupportZoom3DNR;

  MUINT32 errorStatus = NR3D_ERROR_NONE;

  // check policy
  errorStatus = check3DNRPolicy(nr3dHalParam, debugParam, mPreSavedFrameInfo);

  // handle state
  NR3D_STATE_ENUM preStateMachine = m3dnrStateMachine;
  m3dnrErrorStatus = handleState(errorStatus, mForce3DNR, &m3dnrStateMachine);

  MY_LOGD("SensorIdx(%u), 3dnr state=(%d->%d), status(0x%x)", mSensorIdx,
          preStateMachine, m3dnrStateMachine, m3dnrErrorStatus);

  return (NR3D_ERROR_NONE == m3dnrErrorStatus) ? MTRUE : MFALSE;
}

MBOOL Hal3dnr::handleAlignVipiIMGI(const NR3DHALParam& nr3dHalParam,
                                   NR3DParam* outNr3dParam) {
  if (nr3dHalParam.pIMGBufferVIPI == NULL) {
    MY_LOGW("Invalid pIMGBufferVIPI");
    return MFALSE;
  }

  MSize vipiFrameSize = nr3dHalParam.pIMGBufferVIPI->getImgSize();
  const MRect& pImg3oFrameRect = nr3dHalParam.dst_resizer_rect;

  NR3DMVInfo GMVInfo = nr3dHalParam.GMVInfo;
  NR3DCustom::AdjustmentInput adjInput;
  adjInput.force3DNR = mForce3DNR ? true : false;
  adjInput.setGmv(GMVInfo.confX, GMVInfo.confY, GMVInfo.gmvX, GMVInfo.gmvY);
  const NSCam::NR3D::GyroData& gyroData = nr3dHalParam.gyroData;
  adjInput.setGyro(gyroData.isValid, gyroData.x, gyroData.y, gyroData.z);
  const NR3DRSCInfo& RSCInfo = nr3dHalParam.RSCInfo;
  adjInput.setRsc(RSCInfo.isValid, RSCInfo.pMV, RSCInfo.pBV, RSCInfo.rrzoSize.w,
                  RSCInfo.rrzoSize.h, RSCInfo.rssoSize.w, RSCInfo.rssoSize.h,
                  RSCInfo.staGMV);
  MY_LOGD("Gyro isValid(%d), value(%f,%f,%f)", (gyroData.isValid ? 1 : 0),
          gyroData.x, gyroData.y, gyroData.z);

  NR3DCustom::AdjustmentOutput adjOutput;
  NR3DCustom::adjust_parameters(adjInput, &adjOutput);
  if (adjOutput.isGmvOverwritten) {
    MY_LOGD("AfterAdjusting: (confX,confY)=(%d,%d), gmvX(%d->%d), gmvY(%d->%d)",
            GMVInfo.confX, GMVInfo.confY, GMVInfo.gmvX, adjOutput.gmvX,
            GMVInfo.gmvY, adjOutput.gmvY);
    GMVInfo.gmvX = adjOutput.gmvX;
    GMVInfo.gmvY = adjOutput.gmvY;
  }

  if (pImg3oFrameRect.s == vipiFrameSize) {
    calGMV(nr3dHalParam, mForce3DNR, mPreSavedFrameInfo, &GMVInfo);

    // === Algo code: start: align vipi/imgi ===
    NR3DAlignParam nr3dAlignParam;
    // TODO(MTK): integrate zoom flow here
    if (!handleFrameAlign(nr3dHalParam, GMVInfo, &nr3dAlignParam)) {
      MY_LOGW("handleFrameAlign failed");
      return MFALSE;
    }

    getNR3DParam(nr3dHalParam, nr3dAlignParam, outNr3dParam);
    MY_LOGD("vipi offset=%d,(w,h=%d,%d), on region(%d,%d,%d,%d)",
            outNr3dParam->vipi_offst, outNr3dParam->vipi_readW,
            outNr3dParam->vipi_readH, outNr3dParam->onOff_onOfStX,
            outNr3dParam->onOff_onOfStY, outNr3dParam->onSiz_onWd,
            outNr3dParam->onSiz_onHt);
  } else {
    MINT32 nr3dPathID = determine3DNRPath(nr3dHalParam);

    // === Algo code: start: align vipi/imgi ===
    MINT32 adjustGMVX = GMVInfo.gmvX;
    MINT32 adjustGMVY = GMVInfo.gmvY;

    MINT32 i4GMVX = adjustGMVX / LMV_GMV_VALUE_TO_PIXEL_UNIT;
    MINT32 i4GMVY = adjustGMVY / LMV_GMV_VALUE_TO_PIXEL_UNIT;

    MINT32 i4PVOfstX1 = 0;
    MINT32 i4PVOfstY1 = 0;
    MINT32 i4CUOfstX1 = 0;
    MINT32 i4CUOfstY1 = 0;
    MINT32 i4FrmWidthCU = nr3dHalParam.dst_resizer_rect.s.w;
    MINT32 i4FrmHeightCU = nr3dHalParam.dst_resizer_rect.s.h;
    MINT32 i4FrmWidthPV = nr3dHalParam.pIMGBufferVIPI->getImgSize().w;
    MINT32 i4FrmHeightPV = nr3dHalParam.pIMGBufferVIPI->getImgSize().h;
    MINT32 i4CUOfstX2 = 0;
    MINT32 i4CUOfstY2 = 0;
    MINT32 i4PVOfstX2 = 0;
    MINT32 i4PVOfstY2 = 0;
    MINT32 i4OvlpWD = 0;
    MINT32 i4OvlpHT = 0;
    MINT32 NR3D_WD = 0;
    MINT32 NR3D_HT = 0;
    MINT32 VIPI_OFST_X = 0;
    MINT32 VIPI_OFST_Y = 0;
    MINT32 VIPI_WD = 0;
    MINT32 VIPI_HT = 0;
    MINT32 NR3D_ON_EN = 0;
    MINT32 NR3D_ON_OFST_X = 0;
    MINT32 NR3D_ON_OFST_Y = 0;
    MINT32 NR3D_ON_WD = 0;
    MINT32 NR3D_ON_HT = 0;
    MINT32 nmvX = 0;
    MINT32 nmvY = 0;

    switch (nr3dPathID) {
      case NR3D_PATH_RRZO:  // RRZO only
        break;
      case NR3D_PATH_RRZO_CRZ:  // CRZ case
        mCmvX = nr3dHalParam.GMVInfo.x_int;
        mCmvY = nr3dHalParam.GMVInfo.y_int;
        nmvX = (-i4GMVX - (mCmvX - mPrevCmvX)) & ~1;
        nmvY = (-i4GMVY - (mCmvY - mPrevCmvY)) & ~1;
        i4GMVX = -nmvX;
        i4GMVY = -nmvY;
        mPrevCmvX = mCmvX;  // Recore last frame CMV X.
        mPrevCmvY = mCmvY;  // Recore last frame CMV Y.
        break;
      case NR3D_PATH_IMGO:  // IMGO only
        break;
      default:
        MY_LOGE("!!err: should not happen");
        break;
    }

    if (i4GMVX <= 0) {
      i4PVOfstX1 = -i4GMVX;
      i4CUOfstX1 = 0;
    } else {
      i4PVOfstX1 = 0;
      i4CUOfstX1 = i4GMVX;
    }
    if (i4GMVY <= 0) {
      i4PVOfstY1 = -i4GMVY;
      i4CUOfstY1 = 0;
    } else {
      i4PVOfstY1 = 0;
      i4CUOfstY1 = i4GMVY;
    }

    if ((i4FrmWidthCU <= i4FrmWidthPV) &&
        (i4FrmHeightCU <= i4FrmHeightPV)) {  // case: vipi >= imgi
      i4CUOfstX2 = 0;
      i4CUOfstY2 = 0;
      i4PVOfstX2 = (i4FrmWidthPV - i4FrmWidthCU) / 2;
      i4PVOfstY2 = (i4FrmHeightPV - i4FrmHeightCU) / 2;
    }

    if ((i4FrmWidthCU >= i4FrmWidthPV) &&
        (i4FrmHeightCU >= i4FrmHeightPV)) {  // case: vipi <= imgi
      i4CUOfstX2 = (i4FrmWidthCU - i4FrmWidthPV) / 2;
      i4CUOfstY2 = (i4FrmHeightCU - i4FrmHeightPV) / 2;
      i4PVOfstX2 = 0;
      i4PVOfstY2 = 0;
    }

    i4OvlpWD = MIN(i4FrmWidthCU, i4FrmWidthPV) - abs(i4GMVX);
    i4OvlpHT = MIN(i4FrmHeightCU, i4FrmHeightPV) - abs(i4GMVY);

    NR3D_WD = i4FrmWidthCU;
    NR3D_HT = i4FrmHeightCU;

    VIPI_OFST_X = i4PVOfstX1 + i4PVOfstX2;
    VIPI_OFST_Y = i4PVOfstY1 + i4PVOfstY2;
    VIPI_WD = i4FrmWidthCU;
    VIPI_HT = i4FrmHeightCU;

    NR3D_ON_EN = 1;
    NR3D_ON_OFST_X = i4CUOfstX1 + i4CUOfstX2;
    NR3D_ON_OFST_Y = i4CUOfstY1 + i4CUOfstY2;
    NR3D_ON_WD = i4OvlpWD;
    NR3D_ON_HT = i4OvlpHT;
    // === Algo code: end: align vipi/imgi ===

    // === save the vipi/imgi align info
    MUINT32 u4PixelToBytes = 0;
    MINT imgFormat = nr3dHalParam.pIMGBufferVIPI->getImgFormat();
    if (imgFormat == NSCam::eImgFmt_YUY2) {
      u4PixelToBytes = 2;
    }
    if (imgFormat == NSCam::eImgFmt_YV12) {
      u4PixelToBytes = 1;
    }

    outNr3dParam->vipi_offst =
        VIPI_OFST_Y * nr3dHalParam.pIMGBufferVIPI->getBufStridesInBytes(0) +
        VIPI_OFST_X * u4PixelToBytes;
    outNr3dParam->vipi_offst &= ~1;
    outNr3dParam->vipi_readW = i4FrmWidthCU & ~1;
    outNr3dParam->vipi_readH = i4FrmHeightCU & ~1;
    outNr3dParam->onSiz_onWd = i4OvlpWD & ~1;
    outNr3dParam->onSiz_onHt = i4OvlpHT & ~1;
    outNr3dParam->onOff_onOfStX = NR3D_ON_OFST_X & ~1;
    outNr3dParam->onOff_onOfStY = NR3D_ON_OFST_Y & ~1;
    outNr3dParam->ctrl_onEn = NR3D_ON_EN;

    print_NR3DParam(*outNr3dParam, mLogLevel);
  }

  MY_LOGD(
      "3dnr: SIdx(%u), ST=%d, path(%d), gmvX/Y(%5d,%5d), int_x/y=(%5d,%5d), "
      "confX/Y(%d, %d), "
      "f:%d, isResized(%d) offst(%d) (%d,%d)->(%d,%d,%d,%d) ",
      mSensorIdx, m3dnrStateMachine, (MINT32)determine3DNRPath(nr3dHalParam),
      GMVInfo.gmvX, GMVInfo.gmvY, GMVInfo.x_int, GMVInfo.y_int, GMVInfo.confX,
      GMVInfo.confY, nr3dHalParam.frameNo, (pImg3oFrameRect.s == vipiFrameSize),
      outNr3dParam->vipi_offst, outNr3dParam->vipi_readW,
      outNr3dParam->vipi_readH, outNr3dParam->onOff_onOfStX,
      outNr3dParam->onOff_onOfStY, outNr3dParam->onSiz_onWd,
      outNr3dParam->onSiz_onHt);

  return MTRUE;
}

MBOOL Hal3dnr::configNR3D(const NR3DHALParam& nr3dHalParam,
                          const NR3DParam& Nr3dParam) {
  MBOOL ret = MTRUE;
  NS3Av3::NR3D_Config_Param param;

  MBOOL bDrvNR3DEnabled = MTRUE;

  if (mForce3DNR) {
    bDrvNR3DEnabled =
        ::property_get_int32("vendor.camera.3dnr.drv.nr3d.enable", 1);
  }

  if ((MTRUE == checkStateMachine(NR3D_STATE_WORKING)) && bDrvNR3DEnabled) {
    param.enable = bDrvNR3DEnabled;
    param.onRegion.p.x = Nr3dParam.onOff_onOfStX & ~1;
    param.onRegion.p.y = Nr3dParam.onOff_onOfStY & ~1;
    param.onRegion.s.w = Nr3dParam.onSiz_onWd & ~1;
    param.onRegion.s.h = Nr3dParam.onSiz_onHt & ~1;

    param.fullImg.p.x = nr3dHalParam.dst_resizer_rect.p.x & ~1;
    param.fullImg.p.y = nr3dHalParam.dst_resizer_rect.p.y & ~1;
    param.fullImg.s.w = nr3dHalParam.dst_resizer_rect.s.w & ~1;
    param.fullImg.s.h = nr3dHalParam.dst_resizer_rect.s.h & ~1;

    param.vipiOffst = Nr3dParam.vipi_offst & ~1;
    param.vipiReadSize.w = Nr3dParam.vipi_readW & ~1;
    param.vipiReadSize.h = Nr3dParam.vipi_readH & ~1;

    if (nr3dHalParam.p3A) {
      // turn ON 'pull up ISO value to gain FPS'

      NS3Av3::AE_Pline_Limitation_T params;
      params.bEnable = MTRUE;
      params.bEquivalent = MTRUE;
      // use property "camera.3dnr.forceisolimit" to control
      // max_iso_increase_percentage ex: setprop camera.3dnr.forceisolimit 200
      params.u4IncreaseISO_x100 = get_3dnr_max_iso_increase_percentage();
      params.u4IncreaseShutter_x100 = 100;
      nr3dHalParam.p3A->send3ACtrl(E3ACtrl_SetAEPlineLimitation,
                                   (MINTPTR)&params, 0);

      MY_LOGD("turn ON 'pull up ISO value to gain FPS': max: %d %%",
              get_3dnr_max_iso_increase_percentage());
    }

    if (mForce3DNR && ::property_get_int32("vendor.debug.3dnr.vipi.dump", 0)) {
      // dump vipi img
      dumpVIPIBuffer(nr3dHalParam.pIMGBufferVIPI, nr3dHalParam.frameNo);
    }
  } else {
    param.enable = MFALSE;

    if (nr3dHalParam.p3A) {
      // turn OFF 'pull up ISO value to gain FPS'
      NS3Av3::AE_Pline_Limitation_T params;
      params.bEnable = MFALSE;  // disable
      params.bEquivalent = MTRUE;
      params.u4IncreaseISO_x100 = 100;
      params.u4IncreaseShutter_x100 = 100;
      nr3dHalParam.p3A->send3ACtrl(E3ACtrl_SetAEPlineLimitation,
                                   (MINTPTR)&params, 0);

      MY_LOGD("turn OFF  'pull up ISO value to gain FPS'");
    }
  }

  if (nr3dHalParam.pTuningData) {
    void* pIspPhyReg = nr3dHalParam.pTuningData;

    // log keyword for auto test
    MY_LOGD("postProcessNR3D: EN(%d)", param.enable);

#if (MTKCAM_ENABLE_IPC == 1)
    if (mIIspMgr)
      mIIspMgr->postProcessNR3D(mSensorIdx, &param, pIspPhyReg);
#else
    if (auto pIspMgr = MAKE_IspMgr()) {
      pIspMgr->postProcessNR3D(mSensorIdx, param, pIspPhyReg);
    }
#endif
  }

  FUNC_END;
  return ret;
}

MBOOL Hal3dnr::fillGyroForAdjustment(void* __adjInput) {
  NR3DCustom::AdjustmentInput* pAdjInput =
      static_cast<NR3DCustom::AdjustmentInput*>(__adjInput);
  pAdjInput->isGyroValid = false;

  MY_LOGD("Gyro isValid(%d), value(%f,%f,%f)", (pAdjInput->isGyroValid ? 1 : 0),
          pAdjInput->gyroX, pAdjInput->gyroY, pAdjInput->gyroZ);

  return (pAdjInput->isGyroValid ? MTRUE : MFALSE);
}
MBOOL Hal3dnr::updateISPMetadata(
    NSCam::IMetadata* pMeta_InHal,
    const NSCam::NR3D::NR3DTuningInfo& tuningInfo) {
  if (pMeta_InHal == NULL) {
    MY_LOGE("no meta inHal: %p", pMeta_InHal);
    return MFALSE;
  }

  MY_LOGD(
      "Sensor(%d) Enable:%d, GMV status:%d, XY(%d,%d); Input size:(%d,%d), "
      "Crop:(%d,%d)/(%d,%d)",
      mSensorIdx, tuningInfo.canEnable3dnrOnFrame, tuningInfo.mvInfo.status,
      tuningInfo.mvInfo.gmvX, tuningInfo.mvInfo.gmvY, tuningInfo.inputSize.w,
      tuningInfo.inputSize.h, tuningInfo.inputCrop.p.x,
      tuningInfo.inputCrop.p.y, tuningInfo.inputCrop.s.w,
      tuningInfo.inputCrop.s.h);

  // If 3DNR suspended on some frame, we have to reset the internal state of
  // algorithm
  MINT32 frameReset = tuningInfo.canEnable3dnrOnFrame ? 0 : 1;

  IMetadata::IEntry entry(
      MTK_3A_ISP_NR3D_SW_PARAMS);  // refer to ISP_NR3D_META_INFO_T
  entry.push_back(tuningInfo.mvInfo.gmvX, Type2Type<MINT32>());
  entry.push_back(tuningInfo.mvInfo.gmvY, Type2Type<MINT32>());
  entry.push_back(tuningInfo.mvInfo.confX, Type2Type<MINT32>());
  entry.push_back(tuningInfo.mvInfo.confY, Type2Type<MINT32>());
  entry.push_back(tuningInfo.mvInfo.maxGMV, Type2Type<MINT32>());
  entry.push_back(frameReset, Type2Type<MINT32>());
  entry.push_back(tuningInfo.mvInfo.status,
                  Type2Type<MINT32>());  // GMV_Status 0: invalid state
  entry.push_back(tuningInfo.isoThreshold, Type2Type<MINT32>());

  pMeta_InHal->update(MTK_3A_ISP_NR3D_SW_PARAMS, entry);

  if (tuningInfo.inputCrop.s.w > 0 && tuningInfo.inputCrop.s.h > 0) {
    const MSize& sl2eOriSize = tuningInfo.inputSize;
    const MRect& sl2eCropInfo = tuningInfo.inputCrop;
    const MSize& sl2eRrzSize = tuningInfo.inputCrop.s;

    IMetadata::setEntry<MSize>(pMeta_InHal, MTK_ISP_P2_ORIGINAL_SIZE,
                               sl2eOriSize);
    IMetadata::setEntry<MRect>(pMeta_InHal, MTK_ISP_P2_CROP_REGION,
                               sl2eCropInfo);
    IMetadata::setEntry<MSize>(pMeta_InHal, MTK_ISP_P2_RESIZER_SIZE,
                               sl2eRrzSize);
  } else {
    MY_LOGE("SensorIdx(%d) zero input size", mSensorIdx);
  }

  return MTRUE;
}
