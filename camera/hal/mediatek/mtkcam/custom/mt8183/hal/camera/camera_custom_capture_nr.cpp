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

#define LOG_TAG "custom_capture_nr"

#include <camera_custom_capture_nr.h>
#include <mtkcam/def/BuiltinTypes.h>
#include <mtkcam/def/Modes.h>
#include <mtkcam/drv/IHalSensor.h>
// #include <camera_custom_nvram.h>
// #include "isp_tuning/isp_tuning_custom_swnr.h"
#include "mtkcam/utils/std/Log.h"

bool get_capture_nr_th(MUINT32 const sensorDev,
                       MUINT32 const shotmode,
                       MBOOL const isMfll,
                       int* hwth,
                       int* swth) {
  if (sensorDev == NSCam::SENSOR_DEV_MAIN ||
      sensorDev == NSCam::SENSOR_DEV_SUB ||
      sensorDev == NSCam::SENSOR_DEV_MAIN_2) {
    if (!isMfll) {
      switch (shotmode) {
        case NSCam::eShotMode_NormalShot:
          *hwth = 400;
          *swth = 400;
          break;
        case NSCam::eShotMode_ContinuousShot:
        case NSCam::eShotMode_ContinuousShotCc:
          *hwth = DISABLE_CAPTURE_NR;
          *swth = DISABLE_CAPTURE_NR;
          break;
        case NSCam::eShotMode_HdrShot:
          *hwth = 400;
          *swth = 400;
          break;
        case NSCam::eShotMode_ZsdShot:
          *hwth = 400;
          *swth = 400;
          break;
        case NSCam::eShotMode_FaceBeautyShot:
          *hwth = 400;
          *swth = 400;
          break;
        case NSCam::eShotMode_VideoSnapShot:
          *hwth = 400;
          *swth = 400;
          break;
        default:
          *hwth = DISABLE_CAPTURE_NR;
          *swth = DISABLE_CAPTURE_NR;
          break;
          // note: special case
          //  eShotMode_SmileShot, eShotMode_AsdShot
          //      --> NormalShot or ZsdShot
      }
    } else {
      switch (shotmode) {
        case NSCam::eShotMode_NormalShot:
          *hwth = 400;
          *swth = 400;
          break;
        case NSCam::eShotMode_FaceBeautyShot:
          *hwth = 400;
          *swth = 400;
          break;
        default:
          *hwth = DISABLE_CAPTURE_NR;
          *swth = DISABLE_CAPTURE_NR;
          break;
          // note: special case
          //  eShotMode_SmileShot, eShotMode_AsdShot
          //      --> NormalShot or ZsdShot
      }
    }
  } else {
    *hwth = DISABLE_CAPTURE_NR;
    *swth = DISABLE_CAPTURE_NR;
  }

  return MTRUE;
}

// return value: performance 2 > 1 > 0, -1: default
MINT32 get_performance_level(MUINT32 const /*sensorDev*/,
                             MUINT32 const /*shotmode*/,
                             MBOOL const /*isMfll*/,
                             MBOOL const /*isMultiOpen*/
) {
  return eSWNRPerf_Default;
}

MBOOL
is_to_invoke_swnr_interpolation(MUINT32 scenario, MUINT32 /*u4Iso*/) {
  char scenario_MFNR[] = "MFNR";
  char scenario_DUAL[] = "DUAL";
  char scenario_NORM[] = "NORM";
  if (scenario == *reinterpret_cast<MUINT32*>(scenario_MFNR)) {
    return MTRUE;
  } else if (scenario == *reinterpret_cast<MUINT32*>(scenario_DUAL)) {
    return MTRUE;
  } else if (scenario == *reinterpret_cast<MUINT32*>(scenario_NORM)) {
    return MTRUE;
  } else {
    ALOGW("Undefined scenario!!");
    return MTRUE;
  }
}

MINT32 get_swnr_type(MUINT32 const sensorDev) {
  switch (sensorDev) {
    case NSCam::SENSOR_DEV_MAIN:
    case NSCam::SENSOR_DEV_SUB:
    case NSCam::SENSOR_DEV_MAIN_2:
    default:
      return eSWNRType_SW2_VPU;
      // return eSWNRType_SW;
  }
}
