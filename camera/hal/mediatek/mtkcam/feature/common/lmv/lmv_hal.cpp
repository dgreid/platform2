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

/**
 * @file lmv_hal.cpp
 *
 * LMV Hal Source File
 *
 */

#include <cstdio>
#include <memory>
#include <queue>
#include <utility>

#include <lmv_drv.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>

#include <mtkcam/utils/imgbuf/IGbmImageBufferHeap.h>

#include <mtkcam/drv/IHalSensor.h>
#include <mtkcam/feature/eis/eis_type.h>
#include <camera_custom_eis.h>
#include <lmv_hal_imp.h>

#define DP_TRACE_CALL()
#define DP_TRACE_BEGIN(name)
#define DP_TRACE_END()

/****************************************************************************
 *
 *****************************************************************************/
#undef LOG_TAG
#define LOG_TAG "LMVHal"
#include <mtkcam/utils/std/Log.h>

#define LMV_HAL_NAME "LMVHal"
#define LMV_HAL_DUMP "vendor.debug.lmv.dump"
#define LMV_HAL_GYRO_INTERVAL "vendor.debug.lmv.setinterval"

#define intPartShift (8)
#define floatPartShift (31 - intPartShift)
#define DEBUG_DUMP_FRAMW_NUM (10)

MINT32 LMVHalImp::mDebugDump = 2;

#define LMVO_BUFFER_NUM (30)

std::shared_ptr<LMVHal> LMVHal::CreateInstance(char const* userName,
                                               const MUINT32& aSensorIdx) {
  CAM_LOGD("user(%s)", userName);
  return LMVHalImp::GetInstance(aSensorIdx);
}

std::shared_ptr<LMVHal> LMVHalImp::GetInstance(const MUINT32& aSensorIdx) {
  CAM_LOGD("sensorIdx(%u)", aSensorIdx);
  return std::make_shared<LMVHalImp>(aSensorIdx);
}

LMVHalImp::LMVHalImp(const MUINT32& aSensorIdx)
    : LMVHal(), mSensorIdx(aSensorIdx) {
  memset(&mSensorStaticInfo, 0, sizeof(mSensorStaticInfo));
  memset(&mSensorDynamicInfo, 0, sizeof(mSensorDynamicInfo));
  memset(&mLmvAlgoProcData, 0, sizeof(mLmvAlgoProcData));

  while (!mLMVOBufferList.empty()) {
    mLMVOBufferList.pop();
  }
  mUsers = 0;
}

MINT32 LMVHalImp::Init(const MUINT32 eisFactor,
                       NSCam::MSize sensorSize,
                       NSCam::MSize rrzoSize) {
  DP_TRACE_CALL();

  std::lock_guard<std::mutex> _l(mLock);

  if (mUsers > 0) {
    std::atomic_fetch_add_explicit(&mUsers, 1, std::memory_order_release);
    CAM_LOGD("sensorIdx(%u) has one more users", mSensorIdx);
    return LMV_RETURN_NO_ERROR;
  }
  CAM_LOGD("(%p) mSensorIdx(%u) init", this, mSensorIdx);

  mDebugDump = ::property_get_int32(LMV_HAL_DUMP, mDebugDump);
  mEisPlusCropRatio = eisFactor > 100 ? eisFactor : EISCustom::getEIS12Factor();

  m_pHalSensorList = GET_HalSensorList();
  if (m_pHalSensorList == NULL) {
    CAM_LOGE("IHalSensorList::get fail");
    goto create_fail_exit;
  }
  if (GetSensorInfo() != LMV_RETURN_NO_ERROR) {
    CAM_LOGE("GetSensorInfo fail");
    goto create_fail_exit;
  }

  m_pLMVDrv = m_pLMVDrv->CreateInstance(mSensorIdx);
  if (m_pLMVDrv == NULL) {
    CAM_LOGE("LMVDrv::createInstance fail");
    goto create_fail_exit;
  }
  if (m_pLMVDrv->Init(sensorSize, rrzoSize) != LMV_RETURN_NO_ERROR) {
    CAM_LOGE("LMVDrv::Init fail");
    goto create_fail_exit;
  }

  CAM_LOGD("TG(%d), mEisPlusCropRatio(%u)", mSensorDynamicInfo.TgInfo,
           mEisPlusCropRatio);

  std::atomic_fetch_add_explicit(&mUsers, 1, std::memory_order_release);

  return LMV_RETURN_NO_ERROR;

create_fail_exit:

  if (m_pLMVDrv != NULL) {
    m_pLMVDrv->Uninit();
    m_pLMVDrv = NULL;
  }

  if (m_pHalSensorList != NULL) {
    m_pHalSensorList = NULL;
  }

  return LMV_RETURN_NULL_OBJ;
}

MINT32 LMVHalImp::Uninit() {
  std::lock_guard<std::mutex> _l(mLock);

  if (mUsers <= 0) {
    CAM_LOGD("mSensorIdx(%u) has 0 user", mSensorIdx);
    return LMV_RETURN_NO_ERROR;
  }

  std::atomic_fetch_sub_explicit(&mUsers, 1, std::memory_order_release);

  if (mUsers == 0) {
    CAM_LOGD("mSensorIdx(%u) uninit, TG(%d)", mSensorIdx,
             mSensorDynamicInfo.TgInfo);

    if (m_pLMVDrv != NULL) {
      CAM_LOGD("m_pLMVDrv uninit");

      m_pLMVDrv->Uninit();
      m_pLMVDrv = NULL;
    }

    if (UNLIKELY(mDebugDump >= 2)) {
      if (mSensorDynamicInfo.TgInfo != NSCam::CAM_TG_NONE) {
        MINT32 err =
            m_pEisAlg->EisFeatureCtrl(EIS_FEATURE_SAVE_LOG, NULL, NULL);
        if (err != S_EIS_OK) {
          CAM_LOGE("EisFeatureCtrl(EIS_FEATURE_SAVE_LOG) fail(0x%x)", err);
        }
      }
    }

    if (m_pEisAlg != NULL) {
      CAM_LOGD("m_pEisAlg uninit");
      m_pEisAlg->EisReset();
      m_pEisAlg = NULL;
    }

    m_pHalSensorList = nullptr;

    for (int index = 0; index < m_pLMVOSliceBuffer.size(); index++) {
      m_pLMVOSliceBuffer[index]->unlockBuf(LMV_HAL_NAME);
      m_pLMVOSliceBuffer[index] = nullptr;
    }

    mLmvInput_W = 0;
    mLmvInput_H = 0;
    mP1Target_W = 0;
    mP1Target_H = 0;
    mFrameCnt = 0;
    mEisPass1Enabled = 0;
    mIsLmvConfig = 0;
    mCmvX_Int = 0;
    mDoLmvCount = 0;
    mCmvX_Flt = 0;
    mCmvY_Int = 0;
    mMVtoCenterX = 0;
    mMVtoCenterY = 0;
    mCmvY_Flt = 0;
    mGMV_X = 0;
    mGMV_Y = 0;
    mMAX_GMV = LMV_MAX_GMV_DEFAULT;
    mVideoW = 0;
    mVideoH = 0;
    mMemAlignment = 0;
    mBufIndex = 0;
    mDebugDump = 0;

    {
      std::lock_guard<std::mutex> _l(mLMVOBufferListLock);
      while (!mLMVOBufferList.empty()) {
        mLMVOBufferList.pop();
      }
    }

  } else {
    CAM_LOGD("mSensorIdx(%u) has one more users", mSensorIdx);
  }
  return LMV_RETURN_NO_ERROR;
}

MINT32 LMVHalImp::GetSensorInfo() {
  CAM_LOGD("mSensorIdx(%u)", mSensorIdx);

  mSensorDev = m_pHalSensorList->querySensorDevIdx(mSensorIdx);
  m_pHalSensorList->querySensorStaticInfo(mSensorDev, &mSensorStaticInfo);
  m_pHalSensor = m_pHalSensorList->createSensor(LMV_HAL_NAME, 1, &mSensorIdx);
  if (m_pHalSensor == NULL) {
    CAM_LOGE("m_pHalSensorList->createSensor fail");
    return LMV_RETURN_API_FAIL;
  }
  if (m_pHalSensor->querySensorDynamicInfo(mSensorDev, &mSensorDynamicInfo) ==
      MFALSE) {
    CAM_LOGE("querySensorDynamicInfo fail");
    return LMV_RETURN_API_FAIL;
  }

  m_pHalSensor->destroyInstance(LMV_HAL_NAME);
  m_pHalSensor = NULL;

  return LMV_RETURN_NO_ERROR;
}

MINT32 LMVHalImp::ConfigLMV(const LMV_HAL_CONFIG_DATA& aLmvConfig) {
  if (mLmvSupport == MFALSE) {
    CAM_LOGD("mSensorIdx(%u) not support LMV", mSensorIdx);
    return LMV_RETURN_NO_ERROR;
  }

  MINT32 err = LMV_RETURN_NO_ERROR;

  static EIS_SET_ENV_INFO_STRUCT eisAlgoInitData;

  MUINT const usage =
      (GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN |
       GRALLOC_USAGE_HW_CAMERA_READ | GRALLOC_USAGE_HW_CAMERA_WRITE);

  m_pLMVOSliceBuffer = aLmvConfig.lmvBuffers;
  {
    std::lock_guard<std::mutex> _l(mLMVOBufferListLock);
    for (int index = 0; index < m_pLMVOSliceBuffer.size(); index++) {
      m_pLMVOSliceBuffer[index]->lockBuf(LMV_HAL_NAME, usage);
      mLMVOBufferList.push(m_pLMVOSliceBuffer[index]);
    }
  }

  LMV_SENSOR_ENUM sensorType;
  switch (aLmvConfig.sensorType) {
    case NSCam::NSSensorType::eRAW:
      sensorType = LMV_RAW_SENSOR;
      break;
    case NSCam::NSSensorType::eYUV:
      sensorType = LMV_YUV_SENSOR;
      break;
    default:
      CAM_LOGE("not support sensor type(%u), use RAW setting",
               aLmvConfig.sensorType);
      sensorType = LMV_RAW_SENSOR;
      break;
  }
  CAM_LOGD("mIsLmvConfig(%u)", mIsLmvConfig);

  if (UNLIKELY(mDebugDump >= 1)) {
    CAM_LOGD("mIsLmvConfig(%u)", mIsLmvConfig);
  }

  if (mIsLmvConfig == 0) {
    GetEisCustomize(&eisAlgoInitData.eis_tuning_data);
    eisAlgoInitData.Eis_Input_Path = EIS_PATH_RAW_DOMAIN;  // RAW domain

    if (mSensorDynamicInfo.TgInfo == NSCam::CAM_TG_NONE) {
      // Reget sensor information
      if (GetSensorInfo() != LMV_RETURN_NO_ERROR) {
        CAM_LOGE("GetSensorInfo fail");
      }
      CAM_LOGD("TG(%d)", mSensorDynamicInfo.TgInfo);
    }

    if (m_pEisAlg == NULL) {
      m_pEisAlg.reset(MAKE_3DNR_IPC(mSensorIdx), [](MTKEis* p) {
        p->destroyInstance();
        delete p;
      });
      if (m_pEisAlg == NULL) {
        CAM_LOGE("MTKEis::createInstance fail");
        return LMV_RETURN_UNKNOWN_ERROR;
      }
    }
    if (m_pEisAlg->EisInit(&eisAlgoInitData) != S_EIS_OK) {
      CAM_LOGE("EisInit fail(0x%x)", err);
      return LMV_RETURN_API_FAIL;
    }

    mTsForAlgoDebug = 0;

    if (m_pLMVDrv->ConfigLMVReg(mSensorDynamicInfo.TgInfo) !=
        LMV_RETURN_NO_ERROR) {
      CAM_LOGE("ConfigLMVReg fail(0x%x)", err);
      return LMV_RETURN_API_FAIL;
    }

    mIsLmvConfig = 1;
    {
      std::lock_guard<std::mutex> _l(mLock);
      mEisPass1Enabled = 1;
    }
  }
  return LMV_RETURN_NO_ERROR;
}

MINT32 LMVHalImp::DoLMVCalc(QBufInfo const& pBufInfo) {
  MINT32 err = LMV_RETURN_NO_ERROR;
  const MUINT64 aTimeStamp =
      pBufInfo.mvOut[0].mMetaData.mTimeStamp;  // Maybe framedone

  if (mLmvSupport == MFALSE) {
    CAM_LOGD("mSensorIdx(%u) not support LMV", mSensorIdx);
    return LMV_RETURN_EISO_MISS;
  }

  if (UNLIKELY(mTsForAlgoDebug == 0)) {
    mTsForAlgoDebug = aTimeStamp;
  }

  if (UNLIKELY(mDebugDump >= 1)) {
    CAM_LOGD("mSensorIdx=%u,mEisPass1Enabled(%u)", mSensorIdx,
             mEisPass1Enabled);
  }

  if (aTimeStamp <= 0) {
    CAM_LOGD("DoP1Eis aTimeStamp is not reasonable(%" PRIi64 ")", aTimeStamp);
  } else {
    EIS_RESULT_INFO_STRUCT eisCMVResult;
    MINTPTR lmvoBufferVA = 0;
    for (size_t i = 0; i < pBufInfo.mvOut.size(); i++) {
      if (pBufInfo.mvOut[i].mPortID.index == PORT_RRZO.index) {
        // crop region
        mP1ResizeIn_W = mLmvInput_W = pBufInfo.mvOut.at(i).mMetaData.mDstSize.w;
        mP1ResizeIn_H = mLmvInput_H = pBufInfo.mvOut.at(i).mMetaData.mDstSize.h;
      }
      if (pBufInfo.mvOut[i].mPortID.index == PORT_EISO.index) {
        lmvoBufferVA = pBufInfo.mvOut.at(i).mBuffer->getBufVA(0);
      }
    }

    {
      std::lock_guard<std::mutex> _l(mP1Lock);

      if (m_pLMVDrv->Get2PixelMode() == 1) {
        mLmvInput_W >>= 1;
      } else if (m_pLMVDrv->Get2PixelMode() == 2) {
        mLmvInput_W >>= 2;
      }

      mLmvInput_W -= 4;  // for algo need
      mLmvInput_H -= 4;  // for algo need

      mP1Target_W = (mLmvInput_W / (mEisPlusCropRatio / 100.0));
      mP1Target_H = (mLmvInput_H / (mEisPlusCropRatio / 100.0));

      mP1ResizeOut_W = (mP1ResizeIn_W / (mEisPlusCropRatio / 100.0));
      mP1ResizeOut_H = (mP1ResizeIn_H / (mEisPlusCropRatio / 100.0));

      mLmvAlgoProcData.eis_image_size_config.InputWidth = mLmvInput_W;
      mLmvAlgoProcData.eis_image_size_config.InputHeight = mLmvInput_H;
      mLmvAlgoProcData.eis_image_size_config.TargetWidth = mP1Target_W;
      mLmvAlgoProcData.eis_image_size_config.TargetHeight = mP1Target_H;
    }

    if (UNLIKELY(mDebugDump >= 0)) {
      CAM_LOGD("mEisPlusCropRatio(%u),mSensorIdx=%u,EisIn(%u,%u),P1T(%u,%u)",
               mEisPlusCropRatio, mSensorIdx, mLmvInput_W, mLmvInput_H,
               mP1Target_W, mP1Target_H);
    }
    //> get LMV HW statistic
    if (m_pLMVDrv->GetLmvHwStatistic(lmvoBufferVA,
                                     &mLmvAlgoProcData.eis_state) ==
        LMV_RETURN_EISO_MISS) {
      CAM_LOGW("EISO data miss");
      return LMV_RETURN_NO_ERROR;
    }
    if (UNLIKELY(mDebugDump == 3)) {
      DumpStatistic(mLmvAlgoProcData.eis_state);
    }

    mLmvAlgoProcData.DivH = m_pLMVDrv->GetLMVDivH();
    mLmvAlgoProcData.DivV = m_pLMVDrv->GetLMVDivV();
    mLmvAlgoProcData.EisWinNum = m_pLMVDrv->GetLMVMbNum();

    MBOOL gyroValid = false;
    MBOOL accValid = false;

    {
      std::lock_guard<std::mutex> _l(mP1Lock);

      mLmvAlgoProcData.sensor_info.GyroValid = gyroValid;
      mLmvAlgoProcData.sensor_info.Gvalid = accValid;
    }

    if (UNLIKELY(mDebugDump >= 1)) {
      CAM_LOGD("EN:(Acc,Gyro)=(%d,%d)/Acc(%f,%f,%f)/Gyro(%f,%f,%f)", accValid,
               gyroValid, mLmvAlgoProcData.sensor_info.AcceInfo[0],
               mLmvAlgoProcData.sensor_info.AcceInfo[1],
               mLmvAlgoProcData.sensor_info.AcceInfo[2],
               mLmvAlgoProcData.sensor_info.GyroInfo[0],
               mLmvAlgoProcData.sensor_info.GyroInfo[1],
               mLmvAlgoProcData.sensor_info.GyroInfo[2]);
    }

    if (m_pEisAlg->EisFeatureCtrl(EIS_FEATURE_SET_PROC_INFO, &mLmvAlgoProcData,
                                  NULL) != S_EIS_OK) {
      CAM_LOGE("EisAlg:LMV_FEATURE_SET_PROC_INFO fail(0x%x)", err);

      return LMV_RETURN_API_FAIL;
    }
    if (m_pEisAlg->EisMain(&eisCMVResult) != S_EIS_OK) {
      CAM_LOGE("EisAlg:EisMain fail(0x%x), mSensorIdx=%u", err, mSensorIdx);
      return LMV_RETURN_API_FAIL;
    }

    EIS_GET_PLUS_INFO_STRUCT eisData2EisPlus;
    if (m_pEisAlg->EisFeatureCtrl(EIS_FEATURE_GET_EIS_PLUS_DATA, NULL,
                                  &eisData2EisPlus) != S_EIS_OK) {
      CAM_LOGE("EisAlg:LMV_FEATURE_GET_LMV_PLUS_DATA fail(0x%x)", err);
      return LMV_RETURN_API_FAIL;
    }

    {
      std::lock_guard<std::mutex> _l(mP2Lock);

      if (m_pLMVDrv->Get2PixelMode() == 1) {
        if (mDebugDump > 0) {
          CAM_LOGD("eisData2EisPlus.GMVx *= 2");
        }
        eisData2EisPlus.GMVx *= 2.0f;
      } else if (m_pLMVDrv->Get2PixelMode() == 2) {
        if (mDebugDump > 0) {
          CAM_LOGD("eisData2EisPlus.GMVx *= 4");
        }
        eisData2EisPlus.GMVx *= 4.0f;
      }

      mLmvLastData2EisPlus.GMVx = eisData2EisPlus.GMVx;
      mLmvLastData2EisPlus.GMVy = eisData2EisPlus.GMVy;
      mLmvLastData2EisPlus.ConfX = eisData2EisPlus.ConfX;
      mLmvLastData2EisPlus.ConfY = eisData2EisPlus.ConfY;
    }

    EIS_GMV_INFO_STRUCT lmvGMVResult;

    if (m_pEisAlg->EisFeatureCtrl(EIS_FEATURE_GET_ORI_GMV, NULL,
                                  &lmvGMVResult) != S_EIS_OK) {
      CAM_LOGE("EisAlg:LMV_FEATURE_GET_ORI_GMV fail(0x%x)", err);
      return LMV_RETURN_API_FAIL;
    }

    if (m_pLMVDrv->Get2PixelMode() == 1) {
      if (mDebugDump > 0) {
        CAM_LOGD("eisGMVResult.LMV_GMVx *= 2");
      }

      lmvGMVResult.EIS_GMVx *= 2.0f;
    } else if (m_pLMVDrv->Get2PixelMode() == 2) {
      if (mDebugDump > 0) {
        CAM_LOGD("eisGMVResult.LMV_GMVx *= 4");
      }

      lmvGMVResult.EIS_GMVx *= 4.0f;
    }

    mGMV_X = lmvGMVResult.EIS_GMVx;
    mGMV_Y = lmvGMVResult.EIS_GMVy;
    mMAX_GMV = m_pLMVDrv->GetLMVMaxGmv();

    PrepareLmvResult(eisCMVResult.CMV_X, eisCMVResult.CMV_Y);

    mFrameCnt = m_pLMVDrv->GetFirstFrameInfo();

    if (UNLIKELY(mDebugDump >= 1)) {
      CAM_LOGD("mFrameCnt(%u)", mFrameCnt);
    }
    if (mFrameCnt == 0) {
      CAM_LOGD("not first frame");
      mFrameCnt = 1;
    }
  }

  mDoLmvCount++;

  return LMV_RETURN_NO_ERROR;
}

MVOID LMVHalImp::PrepareLmvResult(const MINT32& cmvX, const MINT32& cmvY) {
  if (UNLIKELY(mDebugDump >= 1)) {
    CAM_LOGD("cmvX(%d),cmvY(%d)", cmvX, cmvY);
  }

  std::lock_guard<std::mutex> _l(mP1Lock);

  //====== Boundary Checking ======
  if (cmvX < 0) {
    CAM_LOGE("cmvX should not be negative(%u), fix to 0", cmvX);
    mCmvX_Int = mCmvX_Flt = 0;
  } else {
    MFLOAT tempCMV_X = cmvX / 256.0;
    MINT32 tempFinalCmvX = cmvX;
    mMVtoCenterX = cmvX;

    if ((tempCMV_X + (MFLOAT)mP1ResizeOut_W) > (MFLOAT)mP1ResizeIn_W) {
      CAM_LOGD("cmvX too large(%u), fix to %u", cmvX,
               (mP1ResizeIn_W - mP1ResizeOut_W));
      tempFinalCmvX = (mP1ResizeIn_W - mP1ResizeOut_W);
    }

    mMVtoCenterX -=
        ((mLmvInput_W - mP1Target_W)
         << (intPartShift - 1));  // Make mv for the top-left of center

    if (m_pLMVDrv->Get2PixelMode() == 1) {
      if (mDebugDump > 0) {
        CAM_LOGD("tempFinalCmvX *= 2");
      }

      tempFinalCmvX *= 2;
      mMVtoCenterX *= 2;
    } else if (m_pLMVDrv->Get2PixelMode() == 2) {
      if (mDebugDump > 0) {
        CAM_LOGD("tempFinalCmvX *= 4");
      }

      tempFinalCmvX *= 4;
      mMVtoCenterX *= 4;
    }

    mCmvX_Int = (tempFinalCmvX & (~0xFF)) >> intPartShift;
    mCmvX_Flt = (tempFinalCmvX & (0xFF)) << floatPartShift;
  }

  if (cmvY < 0) {
    CAM_LOGE("cmvY should not be negative(%u), fix to 0", cmvY);

    mCmvY_Int = mCmvY_Flt = 0;
  } else {
    MFLOAT tempCMV_Y = cmvY / 256.0;
    MINT32 tempFinalCmvY = cmvY;
    mMVtoCenterY = cmvY;

    if ((tempCMV_Y + (MFLOAT)mP1ResizeOut_H) > (MFLOAT)mP1ResizeIn_H) {
      CAM_LOGD("cmvY too large(%u), fix to %u", cmvY,
               (mP1ResizeIn_H - mP1ResizeOut_H));

      tempFinalCmvY = (mP1ResizeIn_H - mP1ResizeOut_H);
    }
    mMVtoCenterY -=
        ((mP1ResizeIn_H - mP1ResizeOut_H)
         << (intPartShift - 1));  // Make mv for the top-left of center

    mCmvY_Int = (tempFinalCmvY & (~0xFF)) >> intPartShift;
    mCmvY_Flt = (tempFinalCmvY & (0xFF)) << floatPartShift;
  }

  if (mDebugDump > 0) {
    CAM_LOGD("X(%u,%u),Y(%u,%u),MVtoCenter (%d,%d)", mCmvX_Int, mCmvX_Flt,
             mCmvY_Int, mCmvY_Flt, mMVtoCenterX, mMVtoCenterY);
  }
}

MVOID LMVHalImp::GetLMVResult(MUINT32* a_CMV_X_Int,
                              MUINT32* a_CMV_X_Flt,
                              MUINT32* a_CMV_Y_Int,
                              MUINT32* a_CMV_Y_Flt,
                              MUINT32* a_TarWidth,
                              MUINT32* a_TarHeight,
                              MINT32* a_MVtoCenterX,
                              MINT32* a_MVtoCenterY,
                              MUINT32* a_isFromRRZ) {
  if (mLmvSupport == MFALSE) {
    CAM_LOGD("mSensorIdx(%u) not support LMV", mSensorIdx);
    *a_CMV_X_Int = 0;
    *a_CMV_X_Flt = 0;
    *a_CMV_Y_Int = 0;
    *a_CMV_Y_Flt = 0;
    *a_TarWidth = 0;
    *a_TarHeight = 0;
    *a_MVtoCenterX = 0;
    *a_MVtoCenterY = 0;
    *a_isFromRRZ = 0;
    return;
  }

  {
    std::lock_guard<std::mutex> _l(mP1Lock);

    *a_CMV_X_Int = mCmvX_Int;
    *a_CMV_X_Flt = mCmvX_Flt;
    *a_CMV_Y_Int = mCmvY_Int;
    *a_CMV_Y_Flt = mCmvY_Flt;
    *a_TarWidth = mP1ResizeOut_W;
    *a_TarHeight = mP1ResizeOut_H;
    *a_MVtoCenterX = mMVtoCenterX;
    *a_MVtoCenterY = mMVtoCenterY;
    *a_isFromRRZ = 1;  // Hardcode MUST be fix later!!!!
  }

  if (mDebugDump >= 1) {
    CAM_LOGD("X(%u,%u),Y(%u,%u)", *a_CMV_X_Int, *a_CMV_X_Flt, *a_CMV_Y_Int,
             *a_CMV_Y_Flt);
  }
}

MVOID LMVHalImp::GetGmv(MINT32* aGMV_X,
                        MINT32* aGMV_Y,
                        MUINT32* confX,
                        MUINT32* confY,
                        MUINT32* MAX_GMV) {
  if (mLmvSupport == MFALSE) {
    CAM_LOGD("mSensorIdx(%u) not support LMV", mSensorIdx);
    return;
  }

  *aGMV_X = mGMV_X;
  *aGMV_Y = mGMV_Y;

  if (MAX_GMV != NULL) {
    *MAX_GMV = mMAX_GMV;
  }

  {
    std::lock_guard<std::mutex> _l(mP1Lock);

    if (confX != NULL) {
      *confX = mLmvLastData2EisPlus.ConfX;
    }

    if (confY != NULL) {
      *confY = mLmvLastData2EisPlus.ConfY;
    }
  }

  if (UNLIKELY(mDebugDump >= 1)) {
    if (confX && confY) {
      CAM_LOGD("GMV(%d,%d),Conf(%d,%d)", *aGMV_X, *aGMV_Y, *confX, *confY);
    } else {
      CAM_LOGD("GMV(%d,%d)", *aGMV_X, *aGMV_Y);
    }
  }
}

MBOOL LMVHalImp::GetLMVSupportInfo(const MUINT32& aSensorIdx) {
  CAM_LOGD("GetLMVSupportInfo+");
  mLmvSupport = m_pLMVDrv->GetLMVSupportInfo(aSensorIdx);
  CAM_LOGD("GetLMVSupportInfo-");
  return mLmvSupport;
}

MVOID LMVHalImp::GetLMVInputSize(MUINT32* aWidth, MUINT32* aHeight) {
  if (NULL != m_pLMVDrv) {
    m_pLMVDrv->GetLMVInputSize(aWidth, aHeight);
  } else {
    CAM_LOGE("m_pLMVDrv is NULL");
  }
}

MUINT32 LMVHalImp::GetLMVStatus() {
  std::lock_guard<std::mutex> _l(mLock);

  MUINT32 retVal = mEisPass1Enabled;
  return retVal;
}

MINT32 LMVHalImp::GetBufLMV(std::shared_ptr<IImageBuffer>* spBuf) {
  std::lock_guard<std::mutex> _l(mLMVOBufferListLock);

  if (!mLMVOBufferList.empty()) {
    *spBuf = mLMVOBufferList.front();
    mLMVOBufferList.pop();
    //  if( UNLIKELY(mDebugDump >= 1) )
    { CAM_LOGD("GetBufLMV : %zu", mLMVOBufferList.size()); }
  } else {
    CAM_LOGW("GetBufEis empty!!");
    return LMV_RETURN_EISO_MISS;
  }
  return LMV_RETURN_NO_ERROR;
}

MSize LMVHalImp::QueryMinSize(MBOOL isEISOn,
                              MSize sensorSize,
                              NSCam::MSize outputSize,
                              MSize requestSize,
                              MSize FovMargin) {
  MSize retSize;
  MINT32 out_width = 0;
  MINT32 out_height = 0;
  if (isEISOn == MFALSE) {
    out_width = (requestSize.w <= 160) ? 160 : requestSize.w;
    out_height = (requestSize.h <= 160) ? 160 : requestSize.h;
  } else {
    if (mVideoW == 0 && outputSize.w != 0) {
      mVideoW = outputSize.w;
    }

    if (mVideoH == 0 && outputSize.h != 0) {
      mVideoH = outputSize.h;
    }

    if ((mVideoW < VR_UHD_W) && (mVideoH < VR_UHD_H)) {
      if (EISCustom::isEnabledLosslessMode()) {
        out_width = (requestSize.w <= (EIS_FE_MAX_INPUT_W + FovMargin.w))
                        ? (EIS_FE_MAX_INPUT_W + FovMargin.w)
                        : requestSize.w;
        out_height = (requestSize.h <= (EIS_FE_MAX_INPUT_H + FovMargin.h))
                         ? (EIS_FE_MAX_INPUT_H + FovMargin.h)
                         : requestSize.h;
      } else {
        out_width = (requestSize.w <= (VR_1080P_W + FovMargin.w))
                        ? (VR_1080P_W + FovMargin.w)
                        : requestSize.w;
        out_height = (requestSize.h <= (VR_1080P_H + FovMargin.h))
                         ? (VR_1080P_H + FovMargin.h)
                         : requestSize.h;
      }
    } else {
      MSize EISPlusFOV;
      if (EISCustom::isEnabledLosslessMode()) {
        EISPlusFOV.w = (VR_UHD_W * mEisPlusCropRatio / 100.0f) + FovMargin.w;
        EISPlusFOV.h = (VR_UHD_H * mEisPlusCropRatio / 100.0f) + FovMargin.h;
      } else {
        EISPlusFOV.w = (VR_UHD_W) + FovMargin.w;
        EISPlusFOV.h = (VR_UHD_H) + FovMargin.h;
      }
      out_width =
          (requestSize.w <= EISPlusFOV.w) ? EISPlusFOV.w : requestSize.w;
      out_height =
          (requestSize.h <= EISPlusFOV.h) ? EISPlusFOV.h : requestSize.h;
      out_width = (out_width <= sensorSize.w) ? out_width : sensorSize.w;
      out_height = (out_height <= sensorSize.h) ? out_height : sensorSize.h;
      if (((out_width * 9) >> 4) < out_height) {
        // Align video view angle
        out_height = (out_width * 9) >> 4;
      }
    }

    if (UNLIKELY(mDebugDump >= 1)) {
      CAM_LOGD(
          "eis(%d), sensor: %d/%d, outputSize: %d/%d, videoSize: %d/%d, ret: "
          "%d/%d, crop %d",
          isEISOn, sensorSize.w, sensorSize.h, outputSize.w, outputSize.h,
          mVideoW, mVideoH, out_width, out_height, mEisPlusCropRatio);
    }
  }
  retSize = MSize(out_width, out_height);

  return retSize;
}

MINT32 LMVHalImp::NotifyLMV(QBufInfo* pBufInfo) {
  std::shared_ptr<IImageBuffer> retBuf;
  for (size_t i = 0; i < pBufInfo->mvOut.size(); i++) {
    if (pBufInfo->mvOut[i].mPortID.index == PORT_EISO.index) {
      std::lock_guard<std::mutex> _l(mLMVOBufferListLock);

      retBuf.reset(pBufInfo->mvOut.at(i).mBuffer,
                   [](IImageBuffer* p) { MY_LOGI("release implement"); });
      mLMVOBufferList.push(retBuf);
      if (UNLIKELY(mDebugDump >= 1)) {
        CAM_LOGD("NotifyLMV : %zu", mLMVOBufferList.size());
      }
    }
  }

  return LMV_RETURN_NO_ERROR;
}

MINT32 LMVHalImp::NotifyLMV(std::shared_ptr<NSCam::IImageBuffer> const& spBuf) {
  std::lock_guard<std::mutex> _l(mLMVOBufferListLock);

  if (spBuf != 0) {
    mLMVOBufferList.push(spBuf);
    if (UNLIKELY(mDebugDump >= 1)) {
      CAM_LOGD("NotifyLMV : %zu - Drop", mLMVOBufferList.size());
    }
  }

  return LMV_RETURN_NO_ERROR;
}

MVOID LMVHalImp::GetLMVStatistic(EIS_STATISTIC_STRUCT* a_pLMV_Stat) {
  for (MINT32 i = 0; i < LMV_MAX_WIN_NUM; ++i) {
    a_pLMV_Stat->i4LMV_X[i] = mLmvAlgoProcData.eis_state.i4LMV_X[i];
    a_pLMV_Stat->i4LMV_Y[i] = mLmvAlgoProcData.eis_state.i4LMV_Y[i];
    a_pLMV_Stat->i4LMV_X2[i] = mLmvAlgoProcData.eis_state.i4LMV_X2[i];
    a_pLMV_Stat->i4LMV_Y2[i] = mLmvAlgoProcData.eis_state.i4LMV_Y2[i];
    a_pLMV_Stat->NewTrust_X[i] = mLmvAlgoProcData.eis_state.NewTrust_X[i];
    a_pLMV_Stat->NewTrust_Y[i] = mLmvAlgoProcData.eis_state.NewTrust_Y[i];
    a_pLMV_Stat->SAD[i] = mLmvAlgoProcData.eis_state.SAD[i];
    a_pLMV_Stat->SAD2[i] = mLmvAlgoProcData.eis_state.SAD2[i];
    a_pLMV_Stat->AVG_SAD[i] = mLmvAlgoProcData.eis_state.AVG_SAD[i];
  }
}

MVOID LMVHalImp::GetEisCustomize(EIS_TUNING_PARA_STRUCT* a_pDataOut) {
  EIS_Customize_Para_t customSetting;

  EISCustom::getEISData(&customSetting);

  a_pDataOut->sensitivity = (EIS_SENSITIVITY_ENUM)customSetting.sensitivity;
  a_pDataOut->filter_small_motion = customSetting.filter_small_motion;
  a_pDataOut->adv_shake_ext = customSetting.adv_shake_ext;  // 0 or 1
  a_pDataOut->stabilization_strength =
      customSetting.stabilization_strength;  // 0.5~0.95

  a_pDataOut->advtuning_data.new_tru_th = customSetting.new_tru_th;  // 0~100
  a_pDataOut->advtuning_data.vot_th = customSetting.vot_th;          // 1~16
  a_pDataOut->advtuning_data.votb_enlarge_size =
      customSetting.votb_enlarge_size;                           // 0~1280
  a_pDataOut->advtuning_data.min_s_th = customSetting.min_s_th;  // 10~100
  a_pDataOut->advtuning_data.vec_th =
      customSetting.vec_th;  // 0~11   should be even
  a_pDataOut->advtuning_data.spr_offset =
      customSetting.spr_offset;  // 0 ~ MarginX/2
  a_pDataOut->advtuning_data.spr_gain1 = customSetting.spr_gain1;  // 0~127
  a_pDataOut->advtuning_data.spr_gain2 = customSetting.spr_gain2;  // 0~127

  a_pDataOut->advtuning_data.gmv_pan_array[0] =
      customSetting.gmv_pan_array[0];  // 0~5
  a_pDataOut->advtuning_data.gmv_pan_array[1] =
      customSetting.gmv_pan_array[1];  // 0~5
  a_pDataOut->advtuning_data.gmv_pan_array[2] =
      customSetting.gmv_pan_array[2];  // 0~5
  a_pDataOut->advtuning_data.gmv_pan_array[3] =
      customSetting.gmv_pan_array[3];  // 0~5

  a_pDataOut->advtuning_data.gmv_sm_array[0] =
      customSetting.gmv_sm_array[0];  // 0~5
  a_pDataOut->advtuning_data.gmv_sm_array[1] =
      customSetting.gmv_sm_array[1];  // 0~5
  a_pDataOut->advtuning_data.gmv_sm_array[2] =
      customSetting.gmv_sm_array[2];  // 0~5
  a_pDataOut->advtuning_data.gmv_sm_array[3] =
      customSetting.gmv_sm_array[3];  // 0~5

  a_pDataOut->advtuning_data.cmv_pan_array[0] =
      customSetting.cmv_pan_array[0];  // 0~5
  a_pDataOut->advtuning_data.cmv_pan_array[1] =
      customSetting.cmv_pan_array[1];  // 0~5
  a_pDataOut->advtuning_data.cmv_pan_array[2] =
      customSetting.cmv_pan_array[2];  // 0~5
  a_pDataOut->advtuning_data.cmv_pan_array[3] =
      customSetting.cmv_pan_array[3];  // 0~5

  a_pDataOut->advtuning_data.cmv_sm_array[0] =
      customSetting.cmv_sm_array[0];  // 0~5
  a_pDataOut->advtuning_data.cmv_sm_array[1] =
      customSetting.cmv_sm_array[1];  // 0~5
  a_pDataOut->advtuning_data.cmv_sm_array[2] =
      customSetting.cmv_sm_array[2];  // 0~5
  a_pDataOut->advtuning_data.cmv_sm_array[3] =
      customSetting.cmv_sm_array[3];  // 0~5

  a_pDataOut->advtuning_data.vot_his_method =
      (EIS_VOTE_METHOD_ENUM)customSetting.vot_his_method;  // 0 or 1
  a_pDataOut->advtuning_data.smooth_his_step =
      customSetting.smooth_his_step;  // 2~6

  a_pDataOut->advtuning_data.eis_debug = customSetting.eis_debug;
}

MVOID LMVHalImp::DumpStatistic(const EIS_STATISTIC_STRUCT& aLmvStat) {
  for (int i = 0; i < LMV_MAX_WIN_NUM; ++i) {
    CAM_LOGI("MB%d%d,(LMV_X,LMV_Y)=(%d,%d)", (i / 4), (i % 4),
             aLmvStat.i4LMV_X[i], aLmvStat.i4LMV_Y[i]);
  }

  for (int i = 0; i < LMV_MAX_WIN_NUM; ++i) {
    CAM_LOGI("MB%d%d,(LMV_X2,LMV_Y2)=(%d,%d)", (i / 4), (i % 4),
             aLmvStat.i4LMV_X2[i], aLmvStat.i4LMV_Y2[i]);
  }

  for (int i = 0; i < LMV_MAX_WIN_NUM; ++i) {
    CAM_LOGI("MB%d%d,MinSAD(%u)", (i / 4), (i % 4), aLmvStat.SAD[i]);
  }
  for (int i = 0; i < LMV_MAX_WIN_NUM; ++i) {
    CAM_LOGI("MB%d%d,(NewTrust_X,NewTrust_Y)=(%u,%u)", (i / 4), (i % 4),
             aLmvStat.NewTrust_X[i], aLmvStat.NewTrust_Y[i]);
  }

  for (int i = 0; i < LMV_MAX_WIN_NUM; ++i) {
    CAM_LOGI("MB%d%d,MinSAD2(%u)", (i / 4), (i % 4), aLmvStat.SAD2[i]);
  }

  for (int i = 0; i < LMV_MAX_WIN_NUM; ++i) {
    CAM_LOGI("MB%d%d,AvgSAD(%u)", (i / 4), (i % 4), aLmvStat.AVG_SAD[i]);
  }
}
