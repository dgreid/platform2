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

#define LOG_TAG "MtkCam/P1NodeConnectLMV"
//
#include <memory>
#include "mtkcam/pipeline/hwnode/p1/P1ConnectLMV.h"

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
namespace NSCam {
namespace v3 {
namespace NSP1Node {

//
#define SUPPORT_LMV (1)
#define FORCE_EIS_ON (SUPPORT_LMV && (0))
#define FORCE_3DNR_ON (1)

#if 1
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  P1ConnectLMV Implementation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1ConnectLMV::support(void) {
#if SUPPORT_LMV
  return MTRUE;
#else
  return MFALSE;
#endif
}

/******************************************************************************
 *
 ******************************************************************************/
MINT32
P1ConnectLMV::getOpenId(void) {
  return mOpenId;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1ConnectLMV::init(MUINT32 eisMode,
                   const MUINT32 eisFactor,
                   MSize sensorSize,
                   MSize rrzoSize) {
  uninit();
  //
  std::lock_guard<std::mutex> autoLock(mLock);
  mEisMode = eisMode;
  mIsCalibration =
      /*EIS_MODE_IS_CALIBRATION_ENABLED(mEisMode) ? MTRUE : */ MFALSE;
  MY_LOGD("mEisMode=0x%x, mIsCalibration=%d", mEisMode, mIsCalibration);
  P1_TRACE_S_BEGIN(SLG_S, "P1Connect:LMV-init");
  mpLMV = LMVHal::CreateInstance(LOG_TAG, mOpenId);
  if (mpLMV == NULL) {
    MY_LOGE("LMVHal::CreateInstance fail");
    return MFALSE;
  }
  mpLMV->Init(eisFactor, sensorSize, rrzoSize);
  IHalSensorList* sensorList = GET_HalSensorList();
  if (sensorList == NULL) {
    MY_LOGE("Get-SensorList fail");
    return MFALSE;
  }
  mConfigData.sensorType = sensorList->queryType(mOpenId);

  P1_TRACE_C_END(SLG_S);  // "P1Connect:LMV-init"

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1ConnectLMV::uninit(void) {
  std::lock_guard<std::mutex> autoLock(mLock);
  P1_TRACE_S_BEGIN(SLG_S, "P1Connect:LMV-uninit");
  if (mpLMV) {
    mpLMV->Uninit();
    mpLMV = NULL;
  }
  //
  P1_TRACE_C_END(SLG_S);  // "P1Connect:LMV-uninit"
  return MFALSE;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1ConnectLMV::config(std::vector<std::shared_ptr<IImageBuffer>> buffers) {
  MY_LOGD("config+");

  std::lock_guard<std::mutex> autoLock(mLock);
  if ((mpLMV != NULL) && (mpLMV->GetLMVSupportInfo(mOpenId))) {
    P1_TRACE_S_BEGIN(SLG_S, "P1Connect:LMV-ConfigLMV");
    mConfigData.lmvBuffers = buffers;
    mpLMV->ConfigLMV(mConfigData);
    P1_TRACE_C_END(SLG_S);  // "P1Connect:LMV-ConfigLMV"
  }
  MY_LOGD("config-");
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1ConnectLMV::enableSensor(void) {}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1ConnectLMV::enableOIS(std::shared_ptr<IHal3A_T> p3A) {
  std::lock_guard<std::mutex> autoLock(mLock);
  if (EIS_MODE_IS_CALIBRATION_ENABLED(mEisMode) && mpLMV && p3A) {
    // Enable OIS
    MY_LOGD("[LMVHal] mEisMode:%d => Enable OIS ", mEisMode);
    P1_TRACE_S_BEGIN(SLG_R, "P1Connect:LMV-SetEnableOIS");
    p3A->send3ACtrl(NS3Av3::E3ACtrl_SetEnableOIS, 1, 0);
    P1_TRACE_C_END(SLG_R);  // "P1Connect:LMV-SetEnableOIS"
    mEisMode = EIS_MODE_OFF;
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1ConnectLMV::getBuf(std::shared_ptr<IImageBuffer>* rEISOBuf) {
  std::lock_guard<std::mutex> autoLock(mLock);
  P1_TRACE_S_BEGIN(SLG_I, "P1Connect:LMV-GetBufLMV");
  mpLMV->GetBufLMV(rEISOBuf);
  P1_TRACE_C_END(SLG_I);  // "P1Connect:LMV-GetBufLMV"
  if (rEISOBuf == NULL) {
    MY_LOGE("LMVHal::GetBufLMV fail");
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1ConnectLMV::isEISOn(IMetadata* const inApp) {
  if (inApp == NULL) {
    return MFALSE;
  }
  MUINT8 eisMode = MTK_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
  MINT32 adveisMode = MTK_EIS_FEATURE_EIS_MODE_OFF;
  if (!tryGetMetadata<MUINT8>(inApp, MTK_CONTROL_VIDEO_STABILIZATION_MODE,
                              &eisMode)) {
    MY_LOGD("no MTK_CONTROL_VIDEO_STABILIZATION_MODE");
  }
  if (!tryGetMetadata<MINT32>(inApp, MTK_EIS_FEATURE_EIS_MODE, &adveisMode)) {
    MY_LOGD("no MTK_EIS_FEATURE_EIS_MODE");
  }

#if FORCE_EIS_ON
  eisMode = MTK_CONTROL_VIDEO_STABILIZATION_MODE_ON;
#endif
  return (eisMode == MTK_CONTROL_VIDEO_STABILIZATION_MODE_ON ||
          adveisMode == MTK_EIS_FEATURE_EIS_MODE_ON);
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1ConnectLMV::is3DNROn(IMetadata* const inApp, IMetadata* const inHal) {
  if (inApp == NULL) {
    return MFALSE;
  }
  MINT32 e3DnrMode = MTK_NR_FEATURE_3DNR_MODE_OFF;
  if (!tryGetMetadata<MINT32>(inApp, MTK_NR_FEATURE_3DNR_MODE, &e3DnrMode)) {
    MY_LOGD("no MTK_NR_FEATURE_3DNR_MODE");
  }

  MINT32 eHal3DnrMode = MTK_NR_FEATURE_3DNR_MODE_ON;
  if (!tryGetMetadata<MINT32>(inHal, MTK_DUALZOOM_3DNR_MODE, &eHal3DnrMode)) {
    // Only valid for dual cam. On single cam, we don't care HAL meta,
    // and can assume HAL is "ON" on single cam.
    eHal3DnrMode = MTK_NR_FEATURE_3DNR_MODE_ON;
    MY_LOGD("no MTK_NR_FEATURE_3DNR_MODE in HAL");
  }

#if FORCE_3DNR_ON
  e3DnrMode = MTK_NR_FEATURE_3DNR_MODE_ON;
  eHal3DnrMode = MTK_NR_FEATURE_3DNR_MODE_ON;
#endif
  return (e3DnrMode == MTK_NR_FEATURE_3DNR_MODE_ON &&
          eHal3DnrMode == MTK_NR_FEATURE_3DNR_MODE_ON);
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1ConnectLMV::checkSwitchOut(IMetadata* const inHAL) {
  if (inHAL == NULL) {
    return MFALSE;
  }
  MBOOL result = MFALSE;
  //
#if 1  // #if 0 /* for forced disable UNI_SWITCH_OUT */
  MINT32 needSwitchOut = 0;
  if (tryGetMetadata<MINT32>(inHAL, MTK_LMV_SEND_SWITCH_OUT, &needSwitchOut)) {
    if (needSwitchOut == 1) {
      result = MTRUE;
    }
  }
#endif
  //
  return result;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1ConnectLMV::adjustCropInfo(IMetadata* pAppMetadata,
                             IMetadata* pHalMetadata,
                             MRect* p_cropRect_control,
                             MSize sensorParam_Size,
                             MBOOL bEnableFrameSync,
                             MBOOL bIsStereoCamMode) {
  std::lock_guard<std::mutex> autoLock(mLock);
  MRect& cropRect_control = *p_cropRect_control;
  MY_LOGD("control" P1_RECT_STR "sensor" P1_SIZE_STR,
          P1_RECT_VAR(cropRect_control), P1_SIZE_VAR(sensorParam_Size));
  if (mpLMV) {
    MSize videoSize = MSize(0, 0);
    MBOOL isEisOn = false;
    MRect const requestRect = MRect(cropRect_control);
    MSize const sensorSize = MSize(sensorParam_Size);
    MSize FovMargin = MSize(0, 0);
    MPoint const requestCenter =
        MPoint((requestRect.p.x + (requestRect.s.w >> 1)),
               (requestRect.p.y + (requestRect.s.h >> 1)));
    isEisOn = isEISOn(pAppMetadata);

    if (!tryGetMetadata<MSize>(pHalMetadata, MTK_EIS_VIDEO_SIZE, &videoSize)) {
      MY_LOGD("cannot get MTK_EIS_VIDEO_SIZE");
    }

    MY_LOGD("FOVMargin : %dx%d", FovMargin.w, FovMargin.h);

    cropRect_control.s = mpLMV->QueryMinSize(isEisOn, sensorSize, videoSize,
                                             requestRect.size(), FovMargin);

    MY_LOGD("Sensor(%dx%d) Video(%dx%d) REQ(%dx%d) LMV(%dx%d)", sensorSize.w,
            sensorSize.h, videoSize.w, videoSize.h, requestRect.size().w,
            requestRect.size().h, cropRect_control.s.w, cropRect_control.s.h);

    if (isEisOn && (bEnableFrameSync || bIsStereoCamMode)) {
      cropRect_control = MRect(requestRect);
      MY_LOGD(
          "EIS minimun size not supported in dual cam mode (%d,%d) "
          "request_ctrl" P1_RECT_STR,
          bEnableFrameSync, bIsStereoCamMode, P1_RECT_VAR(cropRect_control));
    }

    if (cropRect_control.s.w != requestRect.size().w) {
      MSize::value_type half_len = ((cropRect_control.s.w + 1) >> 1);
      MY_LOGD(
          "Check_X_W half_len(%d) requestCenter.x(%d).w(%d) "
          "CenterX(%d) SensorW(%d)",
          half_len, cropRect_control.p.x, cropRect_control.s.w, requestCenter.x,
          sensorSize.w);
      if (requestCenter.x < half_len) {
        cropRect_control.p.x = 0;
      } else if ((requestCenter.x + half_len) > sensorSize.w) {
        cropRect_control.p.x = sensorSize.w - cropRect_control.s.w;
      } else {
        cropRect_control.p.x = requestCenter.x - half_len;
      }
    }
    if (cropRect_control.s.h != requestRect.size().h) {
      MSize::value_type half_len = ((cropRect_control.s.h + 1) >> 1);
      MY_LOGD(
          "Check_Y_H half_len(%d) requestCenter.y(%d).h(%d) "
          "CenterY(%d) SensorH(%d)",
          half_len, cropRect_control.p.y, cropRect_control.s.h, requestCenter.y,
          sensorSize.h);
      if (requestCenter.y < half_len) {
        cropRect_control.p.y = 0;
      } else if ((requestCenter.y + half_len) > sensorSize.h) {
        cropRect_control.p.y = sensorSize.h - cropRect_control.s.h;
      } else {
        cropRect_control.p.y = requestCenter.y - half_len;
      }
    }
    MY_LOGD("final_control" P1_RECT_STR, P1_RECT_VAR(cropRect_control));
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1ConnectLMV::processDequeFrame(
    NSCam::NSIoPipe::NSCamIOPipe::QBufInfo* pBufInfo) {
  MY_LOGD("processDequeFrame+");
  std::lock_guard<std::mutex> autoLock(mLock);
  // call LMV notify function
  if (mpLMV) {
    mpLMV->NotifyLMV(pBufInfo);
  }
  MY_LOGD("processDequeFrame-");
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1ConnectLMV::processDropFrame(std::shared_ptr<NSCam::IImageBuffer>* spBuf) {
  MY_LOGD("processDropFrame+");
  std::lock_guard<std::mutex> autoLock(mLock);
  if (*spBuf != NULL && mpLMV) {
    mpLMV->NotifyLMV(*spBuf);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1ConnectLMV::processResult(MBOOL isBinEn,
                            MBOOL isConfigEis,
                            MBOOL isConfigRrz,
                            IMetadata* pInAPP,
                            IMetadata* pInHAL,
                            NS3Av3::MetaSet_T* result3A,
                            std::shared_ptr<IHal3A_T> p3A,
                            MINT32 const currMagicNum,
                            MUINT32 const currSofIdx,
                            MUINT32 const lastSofIdx,
                            MUINT32 const uniSwitchState,
                            QBufInfo const& deqBuf,
                            MUINT32 const bufIdxEis,
                            MUINT32 const bufIdxRrz,
                            IMetadata* rOutputLMV) {
  MINT64 exposureTime = 0;
  MY_LOGD("processResult+");

#if 1  // for EISO related processing
  if ((isConfigEis && (bufIdxEis < deqBuf.mvOut.size())) && pInAPP != NULL) {
    if (1) {
      MUINT8 cap_mode = 0;

      if (!tryGetMetadata<MUINT8>(pInAPP, MTK_CONTROL_CAPTURE_INTENT,
                                  &cap_mode)) {
        MY_LOGW("no MTK_CONTROL_CAPTURE_INTENT");
      }

      if (!tryGetMetadata<MINT64>(&(result3A->appMeta),
                                  MTK_SENSOR_EXPOSURE_TIME, &exposureTime)) {
        MY_LOGW("no MTK_SENSOR_EXPOSURE_TIME");
      }

      processLMV(isBinEn, p3A, currMagicNum, currSofIdx, lastSofIdx, deqBuf,
                 bufIdxEis, cap_mode, exposureTime, rOutputLMV);
    }
  } else if ((isConfigRrz && (bufIdxRrz < deqBuf.mvOut.size())) &&
             pInAPP != NULL) {
    if (isEISOn(pInAPP) && (EIS_MODE_IS_EIS_30_ENABLED(mEisMode) ||
                            EIS_MODE_IS_EIS_25_ENABLED(mEisMode) ||
                            EIS_MODE_IS_EIS_22_ENABLED(mEisMode))) {
      MINT32 iExpTime, ihwTS, ilwTS;
      MUINT32 k;
      const MINT64 aTimestamp = deqBuf.mvOut[bufIdxRrz].mMetaData.mTimeStamp;

      if (!tryGetMetadata<MINT64>(&(result3A->appMeta),
                                  MTK_SENSOR_EXPOSURE_TIME, &exposureTime)) {
        MY_LOGW("no MTK_SENSOR_EXPOSURE_TIME");
      }
      iExpTime = exposureTime / ((MINT64)1000);  // (ns to us) << frame duration
      ihwTS = (aTimestamp >> 32) & 0xFFFFFFFF;   // High word
      ilwTS = (aTimestamp & 0xFFFFFFFF);         // Low word
      IMetadata::IEntry entry(MTK_EIS_REGION);
      for (k = 0; k < LMV_REGION_INDEX_EXPTIME; k++) {
        entry.push_back(0, Type2Type<MINT32>());
      }
      /* Store required data for Advanced EIS */
      entry.push_back(iExpTime, Type2Type<MINT32>());
      entry.push_back(ihwTS, Type2Type<MINT32>());
      entry.push_back(ilwTS, Type2Type<MINT32>());
      entry.push_back(0, Type2Type<MINT32>());  // MAX_GMV
      entry.push_back(isBinEn, Type2Type<MBOOL>());
      rOutputLMV->update(MTK_EIS_REGION, entry);
      MY_LOGD("[LMVHal] eisMode: %d, iExpTime: %d, BinEn: %d", mEisMode,
              iExpTime, isBinEn);
    }
  }
#endif

  //
  if (uniSwitchState != UNI_SWITCH_STATE_NONE) {
    IMetadata::IEntry entry(MTK_LMV_SWITCH_OUT_RESULT);
    MINT32 lmv_result = P1NODE_METADATA_INVALID_VALUE;
    switch (uniSwitchState) {
      case UNI_SWITCH_STATE_ACT_ACCEPT:
        lmv_result = MTK_LMV_RESULT_OK;
        break;
      case UNI_SWITCH_STATE_ACT_IGNORE:
        lmv_result = MTK_LMV_RESULT_FAILED;
        break;
      case UNI_SWITCH_STATE_ACT_REJECT:
        lmv_result = MTK_LMV_RESULT_SWITCHING;
        break;
      default:
        MY_LOGW("UNI SwitchOut REQ not act:%d at (%d)", currMagicNum,
                uniSwitchState);
        break;
    }
    if (lmv_result >= MTK_LMV_RESULT_OK) {
      entry.push_back(lmv_result, Type2Type<MINT32>());
      rOutputLMV->update(MTK_LMV_SWITCH_OUT_RESULT, entry);
    }
    MY_LOGD("UNI SwitchOut END (%d) state:%d lmv_result=(%d)", currMagicNum,
            uniSwitchState, lmv_result);
  }

  MY_LOGD("LMV (bin%d eis%d rrz%d) node(%d) sof(%d/%d) uni(%d) ", isBinEn,
          isConfigEis, isConfigRrz, currMagicNum, currSofIdx, lastSofIdx,
          uniSwitchState);
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1ConnectLMV::processLMV(MBOOL isBinEn,
                         std::shared_ptr<IHal3A_T> p3A,
                         MINT32 const currMagicNum,
                         MUINT32 const currSofIdx,
                         MUINT32 const lastSofIdx,
                         QBufInfo const& deqBuf,
                         MUINT32 const deqBufIdx,
                         MUINT8 captureIntent,
                         MINT64 exposureTime,
                         IMetadata* rOutputLMV) {
  std::lock_guard<std::mutex> autoLock(mLock);
  if (deqBuf.mvOut.size() == 0 || deqBuf.mvOut.size() < deqBufIdx) {
    MY_LOGW("DeQ Buf is invalid (%d<%zu), result count (%d)",
            rOutputLMV->count(), deqBuf.mvOut.size(), deqBufIdx);
    return;
  }
  if (mpLMV == NULL) {
    MY_LOGW("LMV not ready (%d)", currMagicNum);
    return;
  }
  mpLMV->DoLMVCalc(deqBuf);
  MBOOL isLastSkipped = CHECK_LAST_FRAME_SKIPPED(lastSofIdx, currSofIdx);
  MUINT32 X_INT, Y_INT, X_FLOAT, Y_FLOAT, WIDTH, HEIGHT, ISFROMRRZ;
  MINT32 GMV_X, GMV_Y, MVtoCenterX, MVtoCenterY, iExpTime, ihwTS, ilwTS;
  MUINT32 ConfX, ConfY;
  MUINT32 MAX_GMV;
  EIS_STATISTIC_STRUCT lmvData;
  memset(&lmvData, 0, sizeof(lmvData));
  const MINT64 aTimestamp = deqBuf.mvOut[deqBufIdx].mMetaData.mTimeStamp;

  MBOOL isLmvValid = MFALSE;
  if (deqBuf.mvOut[deqBufIdx].mSize > 0) {
    isLmvValid = MTRUE;
  } else {
    isLmvValid = MFALSE;
  }
  IMetadata::IEntry validityEntry(MTK_LMV_VALIDITY);
  validityEntry.push_back((isLmvValid ? 1 : 0), Type2Type<MINT32>());
  rOutputLMV->update(MTK_LMV_VALIDITY, validityEntry);

  if (isLmvValid) {
    P1_TRACE_S_BEGIN(SLG_I, "P1Connect:LMV-Result");
    mpLMV->GetLMVResult(&X_INT, &X_FLOAT, &Y_INT, &Y_FLOAT, &WIDTH, &HEIGHT,
                        &MVtoCenterX, &MVtoCenterY, &ISFROMRRZ);
    mpLMV->GetGmv(&GMV_X, &GMV_Y, &ConfX, &ConfY, &MAX_GMV);
    mpLMV->GetLMVStatistic(&lmvData);
    P1_TRACE_C_END(SLG_I);  // "P1Connect:LMV-Result"
  }

  {
    IMetadata::Memory eisStatistic;
    eisStatistic.resize(sizeof(EIS_STATISTIC_STRUCT));
    memcpy(eisStatistic.editArray(), &lmvData, sizeof(EIS_STATISTIC_STRUCT));

    IMetadata::IEntry lmvDataEntry(MTK_EIS_LMV_DATA);
    lmvDataEntry.push_back(eisStatistic, Type2Type<IMetadata::Memory>());
    rOutputLMV->update(lmvDataEntry.tag(), lmvDataEntry);
  }

  iExpTime = exposureTime / ((MINT64)1000);  // (ns to us) << frame duration
  ihwTS = (aTimestamp >> 32) & 0xFFFFFFFF;   // High word
  ilwTS = (aTimestamp & 0xFFFFFFFF);         // Low word
  /* Store required data for EIS 1.2 */
  IMetadata::IEntry entry(MTK_EIS_REGION);
  if (isLmvValid) {
    entry.push_back(X_INT, Type2Type<MINT32>());
    entry.push_back(X_FLOAT, Type2Type<MINT32>());
    entry.push_back(Y_INT, Type2Type<MINT32>());
    entry.push_back(Y_FLOAT, Type2Type<MINT32>());
    entry.push_back(WIDTH, Type2Type<MINT32>());
    entry.push_back(HEIGHT, Type2Type<MINT32>());
    entry.push_back(MVtoCenterX, Type2Type<MINT32>());
    entry.push_back(MVtoCenterY, Type2Type<MINT32>());
    entry.push_back(ISFROMRRZ, Type2Type<MINT32>());
    entry.push_back(GMV_X, Type2Type<MINT32>());
    entry.push_back(GMV_Y, Type2Type<MINT32>());
    entry.push_back(ConfX, Type2Type<MINT32>());
    entry.push_back(ConfY, Type2Type<MINT32>());
    entry.push_back(iExpTime, Type2Type<MINT32>());
    entry.push_back(ihwTS, Type2Type<MINT32>());
    entry.push_back(ilwTS, Type2Type<MINT32>());
    entry.push_back(MAX_GMV, Type2Type<MINT32>());
    entry.push_back(isBinEn, Type2Type<MBOOL>());
    MY_LOGD("EIS: %d, %d, %" PRId64
            ", %d, %p, "
            "Bin:%d, Mnum:%d, SofId:@%d,%d. "
            "%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
            mEisMode, captureIntent, exposureTime, deqBufIdx, (void*)p3A.get(),
            isBinEn, currMagicNum, currSofIdx, lastSofIdx, X_INT, X_FLOAT,
            Y_INT, Y_FLOAT, WIDTH, HEIGHT, MVtoCenterX, MVtoCenterY, GMV_X,
            GMV_Y, ConfX, ConfY, isLastSkipped, MAX_GMV, isBinEn);
    mLMVLastData =
        LMVData(X_INT, X_FLOAT, Y_INT, Y_FLOAT, WIDTH, HEIGHT, MVtoCenterX,
                MVtoCenterY, ISFROMRRZ, GMV_X, GMV_Y, ConfX, ConfY, iExpTime,
                ihwTS, ilwTS, MAX_GMV, isBinEn);
  } else {
    MY_LOGD("Invalid LMV. Use latest result");
    entry.push_back(mLMVLastData.cmv_x_int, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.cmv_x_float, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.cmv_y_int, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.cmv_y_float, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.width, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.height, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.cmv_x_center, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.cmv_y_center, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.isFromRRZ, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.gmv_x, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.gmv_y, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.conf_x, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.conf_y, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.expTime, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.hwTs, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.lwTs, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.maxGMV, Type2Type<MINT32>());
    entry.push_back(mLMVLastData.isFrontBin, Type2Type<MBOOL>());
  }
  rOutputLMV->update(MTK_EIS_REGION, entry);
}

#endif

};  // namespace NSP1Node
};  // namespace v3
};  // namespace NSCam
