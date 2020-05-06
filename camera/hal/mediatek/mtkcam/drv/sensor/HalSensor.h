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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_DRV_SENSOR_HALSENSOR_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_DRV_SENSOR_HALSENSOR_H_
//
#include <mtkcam/drv/IHalSensor.h>
#include <map>
#include <memory>
#include <seninf_drv_def.h>
#include <string>
#include <vector>
#include "mtkcam/drv/sensor/img_sensor.h"
#include "mtkcam/drv/sensor/imgsensor_drv.h"
#include <mtkcam/utils/exif/IBaseCamExif.h>

#define V4L2_CTRL_CLASS_IMAGE_PROC 0x009f0000
#define V4L2_CID_IMAGE_PROC_CLASS_BASE (V4L2_CTRL_CLASS_IMAGE_PROC | 0x900)
#define V4L2_CID_TEST_PATTERN (V4L2_CID_IMAGE_PROC_CLASS_BASE + 3)
#define V4L2_CTRL_CLASS_USER 0x00980000
#define V4L2_CID_BASE (V4L2_CTRL_CLASS_USER | 0x900)
#define V4L2_CID_EXPOSURE (V4L2_CID_BASE + 17)
#define V4L2_CID_GAIN (V4L2_CID_BASE + 19)
#define V4L2_CTRL_CLASS_IMAGE_SOURCE 0x009e0000
#define V4L2_CID_IMAGE_SOURCE_CLASS_BASE (V4L2_CTRL_CLASS_IMAGE_SOURCE | 0x900)
#define V4L2_CID_VBLANK (V4L2_CID_IMAGE_SOURCE_CLASS_BASE + 1)
#define V4L2_CID_ANALOGUE_GAIN (V4L2_CID_IMAGE_SOURCE_CLASS_BASE + 3)

namespace NSCam {
namespace NSHalSensor {
using std::atomic_int_least32_t;
using std::string;
using std::vector;

class HalSensorList;

/******************************************************************************
 *  Hal Sensor.
 ******************************************************************************/
class HalSensor : public IHalSensor {
  typedef struct {
    IMGSENSOR_SENSOR_IDX sensorIdx;
    SENINF_CSI_INFO* pCsiInfo;
    ACDK_SENSOR_INFO_STRUCT* pInfo;
    const ConfigParam* pConfigParam;
  } HALSENSOR_SENINF_CSI;

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  IHalSensor Interfaces.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////                    Instantiation.
  /**
   * Destroy this instance created from IHalSensorList::createSensor.
   */
  virtual MVOID destroyInstance(char const* szCallerName);

 public:  ////                    Operations.
  /**
   * Turn on/off the power of sensor(s).
   */
  virtual MBOOL setupLink(int sensorIdx, int flag);

  virtual MBOOL powerOn(char const* szCallerName,
                        MUINT const uCountOfIndex,
                        MUINT const* pArrayOfIndex);
  virtual MBOOL powerOff(char const* szCallerName,
                         MUINT const uCountOfIndex,
                         MUINT const* pArrayOfIndex);

  /**
   * Configure the sensor(s).
   */
  virtual MBOOL configure(MUINT const uCountOfParam,
                          ConfigParam const* pConfigParam);
  /**
   * Configure the sensor(s).
   */
  virtual MINT sendCommand(MUINT indexDual,
                           MUINTPTR cmd,
                           MUINTPTR arg1,
                           MUINT arg1_size,
                           MUINTPTR arg2,
                           MUINT arg2_size,
                           MUINTPTR arg3,
                           MUINT arg3_size);

  /**
   * Query sensorDynamic information after calling configure
   */
  virtual MBOOL querySensorDynamicInfo(MUINT32 indexDual,
                                       SensorDynamicInfo* pSensorDynamicInfo);
  virtual MINT32 setDebugInfo(IBaseCamExif* pIBaseCamExif);
  virtual MINT32 reset();

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  HalSensor Interfaces.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////                    Instantiation.
  virtual ~HalSensor();
  HalSensor();

 public:  ////                    Operations.
  virtual MBOOL isMatch(std::vector<MUINT> const& vSensorIndex) const;

  virtual MVOID onDestroy();
  virtual MBOOL onCreate(std::vector<MUINT> const& vSensorIndex);

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  Implementation.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

 protected:
  mutable std::mutex mMutex;
  std::vector<int> mSensorData;
  SensorDynamicInfo mSensorDynamicInfo;
  IMGSENSOR_SENSOR_IDX mSensorIdx;

  MUINT32 mScenarioId;
  MUINT32 mHdrMode;
  MUINT32 mPdafMode;
  MUINT32 mDgainRatio;
  MUINT32 mFramerate;  // max frame rate: unit linelength
  MUINT32 m_LineTimeInus;
  MUINT32 m_vblank;
  MUINT32 m_pixClk;
  MUINT32 m_linelength;
  MUINT32 m_margin;
  MUINT32 m_minShutter;
  MUINT32 m_maxFramelength;
  MUINT32 m_framelength;
  MUINT32 m_SensorGainBase;
  MUINT32 m_SensorGainFactor;
  MUINT32 m_SensorGainMapSize;
  struct imgsensor_agc_param_struct* m_SensorAgcParam;
};
};      // namespace NSHalSensor
};      // namespace NSCam
#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_DRV_SENSOR_HALSENSOR_H_
