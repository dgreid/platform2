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
 * @file lmv_hal_imp.h
 *
 * LMV Hal Implementation Header File
 *
 */

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_FEATURE_COMMON_LMV_LMV_HAL_IMP_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_FEATURE_COMMON_LMV_LMV_HAL_IMP_H_

#include <memory>
#include <mutex>
#include <queue>

#include <mtkcam/feature/lmv/lmv_hal.h>
#include <mtkcam/utils/imgbuf/IImageBuffer.h>

// MTKCAM/V4L2 with IPC usage
#include <mtkcam/drv/iopipe/CamIO/V4L2IIOPipe.h>
#include <mtkcam/drv/iopipe/CamIO/V4L2IHalCamIO.h>

#define MAX_LMV_MEMORY_SIZE (40)

#define TSRECORD_MAXSIZE (108000)
#define GYRO_DATA_PER_FRAME (100)

typedef std::shared_ptr<IImageBuffer> spIImageBuffer;

typedef std::queue<spIImageBuffer> vecSPIImageBuffer;

namespace NSCam {
namespace Utils {
class SensorProvider;
}
}  // namespace NSCam

/**
 *@class LmvHalImp
 *@brief Implementation of LMVHal class
 */
class LMVHalImp : public LMVHal {
 public:
  LMVHalImp(const LMVHalImp&);
  LMVHalImp& operator=(const LMVHalImp&);
  /**
   *@brief LMVHalImp constructor
   */
  explicit LMVHalImp(const MUINT32& aSensorIdx);

  /**
   *@brief LMVHalImp destructor
   */
  ~LMVHalImp() {}

 public:
  /**
   *@brief Create LMVHal object
   *@param[in] aSensorIdx : sensor index
   *@return
   *-LMVHal object
   */
  static std::shared_ptr<LMVHal> GetInstance(const MUINT32& aSensorIdx);

  /**
   *@brief Initialization function
   *@param[in] aSensorIdx : sensor index
   *@return
   *-LMV_RETURN_NO_ERROR indicates success, otherwise indicates fail
   */
  virtual MINT32 Init(const MUINT32 eisFactor,
                      NSCam::MSize sensorSize,
                      NSCam::MSize rrzoSize);

  /**
   *@brief Unitialization function
   *@return
   *-LMV_RETURN_NO_ERROR indicates success, otherwise indicates fail
   */
  virtual MINT32 Uninit();

  /**
   *@brief Configure LMV
   *@details Use this API after pass1/pass2 config and before pass1/pass2 start
   *@param[in] aLmvConfig : LMV config data
   *@return
   *-LMV_RETURN_NO_ERROR indicates success, otherwise indicates fail
   */
  virtual MINT32 ConfigLMV(const LMV_HAL_CONFIG_DATA& aLmvConfig);

  /**
   *@brief Execute LMV
   *@param[in] QBufInfo : LMV result data
   *@return
   *-LMV_RETURN_NO_ERROR indicates success, otherwise indicates fail
   */
  virtual MINT32 DoLMVCalc(QBufInfo const& pBufInfo);

  /**
   *@brief Get LMV algorithm result (CMV)
   *@param[out] a_CMV_X_Int : LMV algo result of X-direction integer part
   *@param[out] a_CMV_X_Flt  : LMV algo result of X-direction float part
   *@param[out] a_CMV_Y_Int : LMV algo result of Y-direction integer part
   *@param[out] a_CMV_Y_Flt  : LMV algo result of Y-direction float part
   *@param[out] a_TarWidth    : LMV width crop size
   *@param[out] a_TarHeight   : LMV height crop size
   */
  virtual MVOID GetLMVResult(MUINT32* a_CMV_X_Int,
                             MUINT32* a_CMV_X_Flt,
                             MUINT32* a_CMV_Y_Int,
                             MUINT32* a_CMV_Y_Flt,
                             MUINT32* a_TarWidth,
                             MUINT32* a_TarHeight,
                             MINT32* a_MVtoCenterX,
                             MINT32* a_MVtoCenterY,
                             MUINT32* a_isFromRRZ);

  /**
   *@brief Get LMV GMV
   *@details The value is 256x
   *@param[out] aGMV_X : x-direction global motion vector between two frames
   *@param[out] aGMV_Y  : y-direction global motion vector between two frames
   *@param[out] aLMVInW  : width of LMV input image (optional)
   *@param[out] aLMVInH  : height of LMV input image (optional)
   *@param[out] MAX_GMV  : max gmv search range (optional)
   */
  virtual MVOID GetGmv(MINT32* aGMV_X,
                       MINT32* aGMV_Y,
                       MUINT32* confX = NULL,
                       MUINT32* confY = NULL,
                       MUINT32* MAX_GMV = NULL);

  /**
   *@brief Get LMV HW support or not
   *@param[in] aSensorIdx : sensor index
   *@return
   *-MTRUE indicates LMV HW is supported, MFALSE indicates not supported
   */
  virtual MBOOL GetLMVSupportInfo(const MUINT32& aSensorIdx);

  virtual NSCam::MSize QueryMinSize(MBOOL isEISOn,
                                    NSCam::MSize sensorSize,
                                    NSCam::MSize outputSize,
                                    NSCam::MSize requestSize,
                                    NSCam::MSize FovMargin);

  virtual MINT32 GetBufLMV(std::shared_ptr<IImageBuffer>* spBuf);

  virtual MINT32 NotifyLMV(QBufInfo* pBufInfo);

  virtual MINT32 NotifyLMV(std::shared_ptr<NSCam::IImageBuffer> const& spBuf);

  /**
   *@brief Return LMV HW statistic result
   *@param[in,out] a_pLMV_Stat : EIS_STATISTIC_STRUCT
   */
  virtual MVOID GetLMVStatistic(EIS_STATISTIC_STRUCT* a_pLMV_Stat);

  /**
   *@brief  Get input width/height of LMV HW
   */
  MVOID GetLMVInputSize(MUINT32* aWidth, MUINT32* aHeight);

  MUINT32 GetLMVStatus();

 private:
  /**
   *@brief Get sensor info
   *@return
   *-LMV_RETURN_NO_ERROR indicates success, otherwise indicates fail
   */
  MINT32 GetSensorInfo();

  /**
   *@brief Get EIS customize info
   *@param[out] a_pDataOut : EIS_TUNING_PARA_STRUCT
   */
  MVOID GetEisCustomize(EIS_TUNING_PARA_STRUCT* a_pDataOut);

  /**
   *@brief Dump EIS HW statistic info
   *@param[in] aLmvStat : EIS_STATISTIC_T
   */
  MVOID DumpStatistic(const EIS_STATISTIC_STRUCT& aLmvStat);

  /**
   *@brief Prepare LMV pass1 result
   *@param[in] cmvX : LMV result
   *@param[in] cmvY : LMV result
   *@param[in] aGmvConfidX : gmvX confidence level
   *@param[in] aGmvConfidY : gmvY confidence level
   *@param[in] aTimeStamp : time stamp of pass1 image
   */
  MVOID PrepareLmvResult(const MINT32& cmvX, const MINT32& cmvY);

  /***********************************************************************/

 private:
  std::atomic_int mUsers;

  mutable std::mutex mLock;
  mutable std::mutex mP1Lock;
  mutable std::mutex mP2Lock;  // use?
  mutable std::mutex mLMVOBufferListLock;

 private:
  EIS_SET_PROC_INFO_STRUCT mLmvAlgoProcData;  // no use?

  // LMV member variable
  MUINT32 mLmvInput_W = 0;
  MUINT32 mLmvInput_H = 0;
  MUINT32 mP1ResizeIn_W = 0;
  MUINT32 mP1ResizeIn_H = 0;
  MUINT32 mP1ResizeOut_W = 0;
  MUINT32 mP1ResizeOut_H = 0;
  MUINT32 mP1Target_W = 0;
  MUINT32 mP1Target_H = 0;
  MUINT32 mVideoW = 0;
  MUINT32 mVideoH = 0;

  // LMV result
  MUINT32 mDoLmvCount = 0;
  MUINT32 mCmvX_Int = 0;
  MUINT32 mCmvX_Flt = 0;
  MUINT32 mCmvY_Int = 0;
  MUINT32 mCmvY_Flt = 0;
  MINT32 mMVtoCenterX = 0;
  MINT32 mMVtoCenterY = 0;
  MINT32 mGMV_X = 0;
  MINT32 mGMV_Y = 0;
  MINT32 mMAX_GMV = LMV_MAX_GMV_DEFAULT;
  EIS_GET_PLUS_INFO_STRUCT mLmvLastData2EisPlus;

  // member variable
  MUINT32 mFrameCnt = 0;
  MUINT32 mEisPass1Enabled = 0;
  MUINT32 mIsLmvConfig = 0;
  MUINT32 mMemAlignment = 0;
  MUINT32 mEisPlusCropRatio = 100;
  MBOOL mLmvSupport = MTRUE;

  MUINT32 mSensorIdx = 0;
  MUINT32 mSensorDev = 0;
  MUINT64 mTsForAlgoDebug = 0;
  MUINT32 mBufIndex = 0;

 private:
  IHalSensorList* m_pHalSensorList = NULL;
  IHalSensor* m_pHalSensor = NULL;
  SensorStaticInfo mSensorStaticInfo;
  SensorDynamicInfo mSensorDynamicInfo;

  std::shared_ptr<LMVDrv> m_pLMVDrv = NULL;
  std::shared_ptr<MTKEis> m_pEisAlg = NULL;

 private:
  std::shared_ptr<IImageBuffer> m_pLmvDbgBuf = NULL;
  std::shared_ptr<IImageBuffer> m_pLMVOMainBuffer = NULL;
  std::vector<std::shared_ptr<IImageBuffer>> m_pLMVOSliceBuffer;
  vecSPIImageBuffer mLMVOBufferList;

 private:
  static MINT32 mDebugDump;
};

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_FEATURE_COMMON_LMV_LMV_HAL_IMP_H_
