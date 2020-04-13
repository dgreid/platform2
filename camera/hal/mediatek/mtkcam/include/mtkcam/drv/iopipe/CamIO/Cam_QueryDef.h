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
#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_DRV_IOPIPE_CAMIO_CAM_QUERYDEF_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_DRV_IOPIPE_CAMIO_CAM_QUERYDEF_H_

#include <mtkcam/drv/iopipe/CamIO/V4L2IHalCamIO.h>
#include <vector>

using std::vector;

namespace NSCam {
namespace NSIoPipe {
namespace NSCamIOPipe {

typedef struct sCAM_QUERY_X_PIX {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_X_PIX;
  typedef struct QUERY_INPUT {
    MUINT32 portId;
    EImageFormat format;
    MUINT32 width;
    E_CamPixelMode pixelMode;
  } QUERY_INPUT;

  QUERY_INPUT QueryInput;
  MUINT32 QueryOutput;
} sCAM_QUERY_X_PIX;

typedef struct sCAM_QUERY_CROP_X_PIX {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_CROP_X_PIX;
  typedef struct QUERY_INPUT {
    MUINT32 portId;
    EImageFormat format;
    MUINT32 width;
    E_CamPixelMode pixelMode;
  } QUERY_INPUT;

  QUERY_INPUT QueryInput;
  MUINT32 QueryOutput;
} sCAM_QUERY_CROP_X_PIX;

typedef struct sCAM_QUERY_X_BYTE {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_X_BYTE;

  typedef struct QUERY_INPUT {
    MUINT32 portId;
    EImageFormat format;
    MUINT32 width;
    E_CamPixelMode pixelMode;
  } QUERY_INPUT;

  QUERY_INPUT QueryInput;
  MUINT32 QueryOutput;
} sCAM_QUERY_X_BYTE;

typedef struct sCAM_QUERY_CROP_X_BYTE {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_CROP_X_BYTE;

  typedef struct QUERY_INPUT {
    MUINT32 portId;
    EImageFormat format;
    MUINT32 width;
    E_CamPixelMode pixelMode;
  } QUERY_INPUT;

  QUERY_INPUT QueryInput;
  MUINT32 QueryOutput;
} sCAM_QUERY_CROP_X_BYTE;

typedef struct sCAM_QUERY_CROP_START_X {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_CROP_START_X;

  typedef struct QUERY_INPUT {
    MUINT32 portId;
    EImageFormat format;
    MUINT32 width;
    E_CamPixelMode pixelMode;
  } QUERY_INPUT;

  QUERY_INPUT QueryInput;
  MUINT32 QueryOutput;
} sCAM_QUERY_CROP_START_X;

typedef struct sCAM_QUERY_STRIDE_PIX {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_STRIDE_PIX;

  typedef struct QUERY_INPUT {
    MUINT32 portId;
    EImageFormat format;
    MUINT32 width;
    E_CamPixelMode pixelMode;
  } QUERY_INPUT;

  QUERY_INPUT QueryInput;
  MUINT32 QueryOutput;
} sCAM_QUERY_STRIDE_PIX;

typedef struct sCAM_QUERY_STRIDE_BYTE {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_STRIDE_BYTE;

  typedef struct QUERY_INPUT {
    MUINT32 portId;
    EImageFormat format;
    MUINT32 width;
    E_CamPixelMode pixelMode;
  } QUERY_INPUT;

  QUERY_INPUT QueryInput;
  MUINT32 QueryOutput;
} sCAM_QUERY_STRIDE_BYTE;

typedef struct sCAM_QUERY_MAX_SEN_NUM {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_MAX_SEN_NUM;

  MUINT32 QueryOutput;
} sCAM_QUERY_MAX_SEN_NUM;

typedef struct sCAM_QUERY_PIPELINE_BITDEPTH {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_PIPELINE_BITDEPTH;

  MUINT32 QueryOutput;
} sCAM_QUERY_PIPELINE_BITDEPTH;

typedef struct sCAM_QUERY_BURST_NUM {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_BURST_NUM;

  MUINT32 QueryOutput;
} sCAM_QUERY_BURST_NUM;

typedef struct sCAM_QUERY_SUPPORT_PATTERN {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_SUPPORT_PATTERN;

  MUINT32 QueryOutput;
} sCAM_QUERY_SUPPORT_PATTERN;

typedef struct sCAM_QUERY_QUERY_FMT {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_QUERY_FMT;
  typedef struct QUERY_INPUT {
    MUINT32 portId;
  } QUERY_INPUT;

  QUERY_INPUT QueryInput;
  vector<EImageFormat> QueryOutput;
} sCAM_QUERY_QUERY_FMT;

typedef struct sCAM_QUERY_BS_RATIO {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_BS_RATIO;
  typedef struct QUERY_INPUT {
    MUINT32 portId;
  } QUERY_INPUT;

  QUERY_INPUT QueryInput;
  MUINT32 QueryOutput;
} sCAM_QUERY_BS_RATIO;

typedef struct sCAM_QUERY_D_Twin {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_D_Twin;

  MBOOL QueryOutput;
} sCAM_QUERY_D_Twin;

typedef struct sCAM_QUERY_D_BAYERENC {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_D_BayerEnc;

  MUINT32 QueryOutput;
} sCAM_QUERY_D_BAYERENC;

typedef struct sCAM_QUERY_IQ_LEVEL {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_IQ_LEVEL;
  typedef struct QUERY_INPUT {
    vector<QueryInData_t> vInData;
    vector<vector<QueryOutData_t>> vOutData;
  } QUERY_INPUT;

  QUERY_INPUT QueryInput;
  MBOOL QueryOutput;
} sCAM_QUERY_IQ_LEVEL;

typedef struct sCAM_QUERY_ISP_RES {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_ISP_RES;
  typedef struct QUERY_INPUT {
    MUINT32 sensorIdx;
    MUINT32 scenarioId;
    MUINT32 rrz_out_w;
    E_CamPattern pattern;
    MBOOL bin_off;
  } QUERY_INPUT;

  QUERY_INPUT QueryInput;
  MBOOL QueryOutput;
} sCAM_QUERY_ISP_RES;

typedef struct sCAM_QUERY_HW_RES_MGR {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_HW_RES_MGR;

  vector<SEN_INFO> QueryInput;
  vector<PIPE_SEL> QueryOutput;
} sCAM_QUERY_HW_RES_MGR;

typedef struct sCAM_QUERY_PDO_AVAILABLE {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_PDO_AVAILABLE;

  MBOOL QueryOutput;
} sCAM_QUERY_PDO_AVAILABLE;

typedef struct sCAM_QUERY_DYNAMIC_PAK {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_DYNAMIC_PAK;

  MBOOL QueryOutput;
} sCAM_QUERY_DYNAMIC_PAK;

typedef struct sCAM_QUERY_MAX_PREVIEW_SIZE {
  ENPipeQueryCmd const Cmd = ENPipeQueryCmd_MAX_PREVIEW_SIZE;

  MUINT32 QueryOutput;
} sCAM_QUERY_MAX_PREVIEW_SIZE;

}  // namespace NSCamIOPipe
}  // namespace NSIoPipe
}  // namespace NSCam

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_DRV_IOPIPE_CAMIO_CAM_QUERYDEF_H_
