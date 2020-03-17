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
 * @file lmv_type.h
 *
 * LMV Type Header File
 */

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_FEATURE_LMV_LMV_TYPE_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_FEATURE_LMV_LMV_TYPE_H_

#include <mtkcam/def/common.h>
#include <mtkcam/drv/IHalSensor.h>

#include <mtkcam/feature/lmv/lmv_ext.h>
#include <mtkcam/utils/imgbuf/IImageBuffer.h>

#include <vector>

/*********************************************************************
 * Define Value
 *********************************************************************/

#include <libeis/MTKEis.h>

#define LMV_MAX_WIN_NUM EIS_WIN_NUM

#define CIF_WIDTH 352
#define CIF_HEIGHT 320

#define D1_WIDTH 792
#define D1_HEIGHT 528

#define HD_720P_WIDTH 1536
#define HD_720P_HEIGHT 864

#define HD_1080P_WIDTH 2112
#define HD_1080P_HEIGHT 1188

#define HD_8M_WIDTH 3264
#define HD_8M_HEIGHT 2200

#define HD_12M_WIDTH 4000
#define HD_12M_HEIGHT 3000

#define HD_16M_WIDTH 4608
#define HD_16M_HEIGHT 3456

#define HD_20M_WIDTH 5164
#define HD_20M_HEIGHT 3872

#define ALIGN_SIZE(in, align) (in & ~(align - 1))

#define LMVO_MEMORY_SIZE (256)  // 32 * 64 (bits) = 256 bytes

/*********************************************************************
 * ENUM
 *********************************************************************/

typedef enum {
  LMV_RETURN_NO_ERROR = 0,             //! The function work successfully
  LMV_RETURN_UNKNOWN_ERROR = 0x0001,   //! Unknown error
  LMV_RETURN_INVALID_DRIVER = 0x0002,  //! invalid driver object
  LMV_RETURN_API_FAIL = 0x0003,        //! api fail
  LMV_RETURN_INVALID_PARA = 0x0004,    //! invalid parameter
  LMV_RETURN_NULL_OBJ = 0x0005,        //! null object
  LMV_RETURN_MEMORY_ERROR = 0x0006,    //! memory error
  LMV_RETURN_EISO_MISS = 0x0007        //! EISO data missed
} LMV_ERROR_ENUM;

/**
 *@brief Sensor type
 */
typedef enum {
  LMV_RAW_SENSOR,
  LMV_YUV_SENSOR,
  LMV_JPEG_SENSOR,
  LMV_NULL_SENSOR
} LMV_SENSOR_ENUM;

/*********************************************************************
 * Struct
 *********************************************************************/

/**
 *@brief  LMV statistic data structure
 */
typedef struct {
  MINT32 i4LMV_X[LMV_MAX_WIN_NUM];
  MINT32 i4LMV_Y[LMV_MAX_WIN_NUM];

  MINT32 i4LMV_X2[LMV_MAX_WIN_NUM];
  MINT32 i4LMV_Y2[LMV_MAX_WIN_NUM];

  MUINT32 NewTrust_X[LMV_MAX_WIN_NUM];
  MUINT32 NewTrust_Y[LMV_MAX_WIN_NUM];

  MUINT32 SAD[LMV_MAX_WIN_NUM];
} LMV_STATISTIC_T;

typedef struct LMV_HAL_CONFIG {
  MUINT32 sensorType;  // use in pass1
  MUINT32 p1ImgW;      // use in pass1
  MUINT32 p1ImgH;      // use in pass1
  LMV_STATISTIC_T lmvData;
  std::vector<std::shared_ptr<NSCam::IImageBuffer> > lmvBuffers;

  //
  LMV_HAL_CONFIG() : sensorType(0), p1ImgW(0), p1ImgH(0) {
    for (MINT32 i = 0; i < LMV_MAX_WIN_NUM; i++) {
      lmvData.i4LMV_X[i] = 0;
      lmvData.i4LMV_Y[i] = 0;
      lmvData.i4LMV_X2[i] = 0;
      lmvData.i4LMV_Y2[i] = 0;
      lmvData.NewTrust_X[i] = 0;
      lmvData.NewTrust_Y[i] = 0;
      lmvData.SAD[i] = 0;
    }
  }
} LMV_HAL_CONFIG_DATA;

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_FEATURE_LMV_LMV_TYPE_H_
