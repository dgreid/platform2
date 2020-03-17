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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_P1_P1CONNECTLMV_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_P1_P1CONNECTLMV_H_

#include "P1Common.h"
//
#include <camera_custom_eis.h>
#include <memory>
#include <mtkcam/feature/eis/gis_calibration.h>
#include <mtkcam/feature/lmv/lmv_ext.h>
#include <mtkcam/feature/lmv/lmv_hal.h>
//

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

class VISIBILITY_PUBLIC P1ConnectLMV {
 public:
  P1ConnectLMV(MINT32 nOpenId,
               MINT32 nLogLevel,
               MINT32 nLogLevelI,
               MINT32 nSysLevel)
      : mOpenId(nOpenId),
        mSysLevel(nSysLevel),
        mpLMV(NULL),
        mEisMode(0),
        mIsCalibration(MFALSE) {}

  virtual ~P1ConnectLMV() {}

  MBOOL support(void);

  MINT32 getOpenId(void);

  MBOOL init(MUINT32 eisMode,
             const MUINT32 eisFactor,
             MSize sensorSize,
             MSize rrzoSize);

  MBOOL uninit(void);

  MVOID config(std::vector<std::shared_ptr<IImageBuffer>> buffers);

  MVOID enableSensor(void);

  MVOID enableOIS(std::shared_ptr<IHal3A_T> p3A);

  MVOID getBuf(std::shared_ptr<IImageBuffer>* rEISOBuf);

  MBOOL isEISOn(IMetadata* const inApp);

  MBOOL is3DNROn(IMetadata* const inApp, IMetadata* const inHal);

  MBOOL checkSwitchOut(IMetadata* const inHAL);

  MVOID adjustCropInfo(IMetadata* pAppMetadata,
                       IMetadata* pHalMetadata,
                       MRect* p_cropRect_control,
                       MSize sensorParam_Size,
                       MBOOL bEnableFrameSync,
                       MBOOL bIsStereoCamMode);

  MVOID processDequeFrame(NSCam::NSIoPipe::NSCamIOPipe::QBufInfo* pBufInfo);

  MVOID processDropFrame(std::shared_ptr<NSCam::IImageBuffer>* spBuf);

  MVOID processResult(MBOOL isBinEn,
                      MBOOL isConfigEis,
                      MBOOL isConfigRrz,
                      IMetadata* pInAPP,  // inAPP
                      IMetadata* pInHAL,  // inHAL
                      NS3Av3::MetaSet_T* result3A,
                      std::shared_ptr<IHal3A_T> p3A,
                      MINT32 const currMagicNum,
                      MUINT32 const currSofIdx,
                      MUINT32 const lastSofIdx,
                      MUINT32 const uniSwitchState,  // UNI_SWITCH_STATE
                      QBufInfo const& deqBuf,
                      MUINT32 const bufIdxEis,
                      MUINT32 const bufIdxRrz,
                      IMetadata* rOutputLMV);

 private:
  MVOID processLMV(MBOOL isBinEn,
                   std::shared_ptr<IHal3A_T> p3A,
                   MINT32 const currMagicNum,
                   MUINT32 const currSofIdx,
                   MUINT32 const lastSofIdx,
                   QBufInfo const& deqBuf,
                   MUINT32 const deqBufIdx,
                   MUINT8 captureIntent,
                   MINT64 exposureTime,
                   IMetadata* rOutputLMV);

 private:
  mutable std::mutex mLock;
  MINT32 mOpenId;

 private:
  MINT32 mSysLevel;
  std::shared_ptr<LMVHal> mpLMV;
  LMVData mLMVLastData;
  MINT32 mEisMode;
  MBOOL mIsCalibration;
  LMV_HAL_CONFIG_DATA
  mConfigData;
};

#define IS_LMV(ConnectLMV_Ptr) \
  ((ConnectLMV_Ptr != NULL) && (ConnectLMV_Ptr->support()))

};  // namespace NSP1Node
};  // namespace v3
};  // namespace NSCam

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_P1_P1CONNECTLMV_H_
