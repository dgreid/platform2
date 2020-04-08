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

#define LOG_TAG "mHalFDVT"

#include <mutex>
#include <sys/time.h>
#include <sys/resource.h>
#include <property.h>
#include <property_lib.h>
#include <faces.h>
#include <MTKDetection.h>
#include <camera/hal/mediatek/mtkcam/feature/common/faceeffect/FaceDetection/FD4.0/fdvt_hal.h>
#include <mtkcam/utils/std/Log.h>
#include <cam_fdvt_v4l2.h>
#include <camera_custom_fd.h>

#if FDVT_DDP_SUPPORT
#include <DpBlitStream.h>
#endif

#include <mtkcam/def/PriorityDefs.h>
#include <pthread.h>
#include <sys/prctl.h>

#define DUMP_IMAGE (0)
#define MTKCAM_HWFD_MAIN_VERSION 40

//****************************//
//-------------------------------------------//
//  Global face detection related parameter  //
//-------------------------------------------//

#define USE_SW_FD_TO_DEBUG (0)
#define USE_HW_FD (1)

#if (MTKCAM_FDFT_SUB_VERSION == '1')
#define HW_FD_SUBVERSION (1)
#elif MTKCAM_FDFT_SUB_VERSION == '2'
#define HW_FD_SUBVERSION (2)
#elif MTKCAM_FDFT_SUB_VERSION == '3'
#define HW_FD_SUBVERSION (3)
#else
#define HW_FD_SUBVERSION (0)
#endif

#define SINGLE_FACE_STABLE_ENABLE (1)

#define MHAL_NO_ERROR 0
#define MHAL_INPUT_SIZE_ERROR 1
#define MHAL_UNINIT_ERROR 2
#define MHAL_REINIT_ERROR 3

#undef MAX_FACE_NUM
#define MAX_FACE_NUM 15

#define MHAL_FDVT_FTBUF_W (320)
#define MHAL_FDVT_FTBUF_H (240)

// v1 is for SD/FB default mode, v2 is for 320x240 manuel mode, v3 is for
// 400x300 manuel mode
static MUINT32 __unused image_width_array_v1[FD_SCALES] = {
    320, 256, 204, 160, 128, 102, 80, 64, 50, 40, 34, 0, 0, 0};
static MUINT32 __unused image_height_array_v1[FD_SCALES] = {
    240, 192, 152, 120, 96, 76, 60, 48, 38, 30, 25, 0, 0, 0};
static MUINT32 __unused image_width_array_v2[FD_SCALES] = {
    320, 262, 210, 168, 134, 108, 86, 70, 56, 46, 38, 0, 0, 0};
static MUINT32 __unused image_height_array_v2[FD_SCALES] = {
    240, 196, 157, 125, 100, 80, 64, 52, 41, 33, 27, 0, 0, 0};
static MUINT32 __unused image_width_array_v3[FD_SCALES] = {
    400, 328, 262, 210, 168, 134, 108, 86, 70, 56, 46, 38, 0, 0};
static MUINT32 __unused image_height_array_v3[FD_SCALES] = {
    300, 245, 196, 157, 125, 100, 80, 64, 52, 41, 33, 27, 0, 0};
static MUINT32 __unused image_width_array_v4[FD_SCALES] = {
    400, 320, 258, 214, 180, 150, 126, 104, 88, 74, 62, 52, 42, 34};
static MUINT32 __unused image_height_array_v4[FD_SCALES] = {
    300, 240, 194, 162, 136, 114, 96, 78, 66, 56, 48, 40, 32, 26};

static const MUINT32 gimage_input_width_vga = 640;
static const MUINT32 gimage_input_height_buffer = 640;

static std::mutex gLock;
static std::mutex gInitLock;
//****************************//
/*******************************************************************************
 * Utility
 ******************************************************************************/
typedef struct {
  MUINT8* srcAddr;
  MUINT32 srcWidth;
  MUINT32 srcHeight;
  MUINT8* dstAddr;
  MUINT32 dstWidth;
  MUINT32 dstHeight;
} PIPE_BILINEAR_Y_RESIZER_STRUCT, *P_PIPE_BILINEAR_Y_RESIZER_STRUCT;

#define PIPE_IUL_I_TO_X(i) \
  ((i) << 16)  ///< Convert from integer to S15.16 fixed-point
#define PIPE_IUL_X_TO_I(x) (((x) + (1 << 15)) >> 16)
///< Convert from S15.16 fixed-point to integer (round)
#define PIPE_IUL_X_TO_I_CHOP(x) \
  ((x) >> 16)  ///< Convert from S15.16 fixed-point to integer (chop)
#define PIPE_IUL_X_TO_I_CARRY(x) (((x) + 0x0000FFFF) >> 16)
///< Convert from S15.16 fixed-point to integer (carry)
#define PIPE_IUL_X_FRACTION(x) ((x)&0x0000FFFF)

#define PIPE_LINEAR_INTERPOLATION(val1, val2, weighting2)        \
  PIPE_IUL_X_TO_I((val1) * (PIPE_IUL_I_TO_X(1) - (weighting2)) + \
                  (val2) * (weighting2))

/*******************************************************************************
 * Public API
 ******************************************************************************/
halFDBase* halFDVT::getInstance(int openId) {
  std::lock_guard<std::mutex> _l(gLock);
  halFDBase* pHalFD;

  pHalFD = new halFDVT(openId);

  return pHalFD;
}

void halFDVT::destroyInstance() {
  std::lock_guard<std::mutex> _l(gLock);
  delete this;
}

halFDVT::halFDVT(int openId) {
  mpMTKFDVTObj = NULL;

  mFDW = 0;
  mFDH = 0;
  mInited = 0;
  mDoDumpImage = 0;
  mFDRefresh = 3;

  mFDModelVer = 0;
  sem_init(&mSemFTLock, 0, 1);
#if (USE_HW_FD)
  mpMTKFDVTObj = FDIpcClientAdapter::createInstance(DRV_FD_OBJ_HW, openId);
#else
  MY_LOGD("use software FD3.5");
  mpMTKFDVTObj = MTKDetection::createInstance(DRV_FD_OBJ_FDFT_SW);
#endif
}

halFDVT::~halFDVT() {
  mFDW = 0;
  mFDH = 0;
  if (mpMTKFDVTObj) {
    mpMTKFDVTObj->destroyInstance();
  }
  MY_LOGD("[Destroy] mpMTKFDVTObj->destroyInstance");
  sem_destroy(&mSemFTLock);
  MY_LOGD("[Destroy] mSemFTLock");
  mpMTKFDVTObj = NULL;
}

MINT32
halFDVT::halFDInit(MUINT32 fdW,
                   MUINT32 fdH,
                   MBOOL SWResizerEnable,
                   MUINT8 Current_mode,  // 0:FD, 1:SD, 2:vFB 3:CFB 4:VSDOF
                   MINT32 FldNum __unused) {
  std::lock_guard<std::mutex> _l(gInitLock);
  MUINT32 i;
  MINT32 err = MHAL_NO_ERROR;
  MTKFDFTInitInfo FDFTInitInfo;
  FD_Customize_PARA FDCustomData;

  if (mInited) {
    MY_LOGW("Warning!!! FDVT HAL OBJ is already inited!!!!");
    MY_LOGW("Old Width/Height : %d/%d, Parameter Width/Height : %d/%d", mFDW,
            mFDH, fdW, fdH);
    return MHAL_REINIT_ERROR;
  }
  {
    char cLogLevel[PROPERTY_VALUE_MAX];
    property_get("vendor.debug.camera.fd.dumpimage", cLogLevel, "0");
    mDoDumpImage = atoi(cLogLevel);
  }
  // Start initial FD
  mCurrentMode = Current_mode;
#if (0 == SMILE_DETECT_SUPPORT)
  // If Smile detection is not support, change mode to FD mode
  if (mCurrentMode == HAL_FD_MODE_SD) {
    mCurrentMode = HAL_FD_MODE_FD;
  }
#endif
  MY_LOGD("[mHalFDInit] Current_mode:%d, SrcW:%d, SrcH:%d, ", Current_mode, fdW,
          fdH);

  if (Current_mode == HAL_FD_MODE_FD || Current_mode == HAL_FD_MODE_MANUAL) {
    for (i = 0; i < FD_SCALES; i++) {
      mImage_width_array[i] = image_width_array_v4[i];
      mImage_height_array[i] = image_height_array_v4[i];
    }
    mUserScaleNum = 14;
    if (Current_mode == HAL_FD_MODE_MANUAL) {
      mUseUserScale = 1;
    } else {
      mUseUserScale = 0;
    }
  } else {
    for (i = 0; i < FD_SCALES; i++) {
      mImage_width_array[i] = image_width_array_v1[i];
      mImage_height_array[i] = image_height_array_v1[i];
    }
    mUserScaleNum = 11;
    mUseUserScale = 0;
  }

  get_fd_CustomizeData(&FDCustomData);

  // Set FD/FT buffer resolution
#if (SINGLE_FACE_STABLE_ENABLE == 1)
  // force enable adaptive scale table
  mUseUserScale = 1;
#else
  if (Current_mode != HAL_FD_MODE_MANUAL) {
    mUseUserScale = FDCustomData.UseCustomScale;
  }
#endif
  mFDW = fdW;
  mFDH = fdH;

  mFTWidth = fdW;
  mFTHeight = fdH;

  for (int j = 0; j < FD_SCALES; j++) {
    mImage_height_array[j] = mImage_width_array[j] * mFDH / mFDW;
    mUserScaleNum = j;
    if (mImage_height_array[j] <= 25 || mImage_width_array[j] <= 25) {
      break;
    }
    // HW limit
    if ((mFDW / mImage_width_array[j]) < 7) {
      mMustScaleIndex = j;
    }
  }
  MY_LOGD("mMustScaleIndex : %d", mMustScaleIndex);
  FDFTInitInfo.FDBufWidth = mImage_width_array[0];
  FDFTInitInfo.FDBufHeight = mImage_height_array[0];
  FDFTInitInfo.FDTBufWidth = mFTWidth;
  FDFTInitInfo.FDTBufHeight = mFTHeight;
  FDFTInitInfo.FDSrcWidth = mFDW;
  FDFTInitInfo.FDSrcHeight = mFDH;

  // Set FD/FT initial parameters
  mFDFilterRatio = FDCustomData.FDSizeRatio;
  FDFTInitInfo.WorkingBufAddr = nullptr;
  FDFTInitInfo.WorkingBufSize = 0;
  FDFTInitInfo.FDThreadNum = FDCustomData.FDThreadNum;
#if (USE_SW_FD_TO_DEBUG)
  FDFTInitInfo.FDThreshold = 256;
#else
  FDFTInitInfo.FDThreshold = FDCustomData.FDThreshold;
#endif
  FDFTInitInfo.MajorFaceDecision = FDCustomData.MajorFaceDecision;
  FDFTInitInfo.OTRatio = FDCustomData.OTRatio;
  FDFTInitInfo.SmoothLevel = FDCustomData.SmoothLevel;
  FDFTInitInfo.Momentum = FDCustomData.Momentum;
  FDFTInitInfo.MaxTrackCount = FDCustomData.MaxTrackCount;
  if (mCurrentMode == HAL_FD_MODE_VFB) {
    FDFTInitInfo.FDSkipStep = 1;  // FB mode
  } else {
    FDFTInitInfo.FDSkipStep = FDCustomData.FDSkipStep;
  }

  FDFTInitInfo.FDRectify = FDCustomData.FDRectify;

  FDFTInitInfo.OTFlow = FDCustomData.OTFlow;
  if (mCurrentMode == HAL_FD_MODE_VFB) {
    FDFTInitInfo.OTFlow = 1;
    if (FDFTInitInfo.OTFlow == 0) {
      FDFTInitInfo.FDRefresh = 90;  // FB mode
    } else {
      FDFTInitInfo.FDRefresh = FDCustomData.FDRefresh;  // FB mode
    }
  } else {
    FDFTInitInfo.FDRefresh = FDCustomData.FDRefresh;
  }
  mFDRefresh = FDFTInitInfo.FDRefresh;
  FDFTInitInfo.FDImageArrayNum = 14;
  FDFTInitInfo.FDImageWidthArray = mImage_width_array;
  FDFTInitInfo.FDImageHeightArray = mImage_height_array;
  FDFTInitInfo.FDCurrent_mode = mCurrentMode;
  FDFTInitInfo.FDModel = FDCustomData.FDModel;
  FDFTInitInfo.FDMinFaceLevel = 0;
  FDFTInitInfo.FDMaxFaceLevel = 13;
  FDFTInitInfo.FDImgFmtCH1 = FACEDETECT_IMG_Y_SINGLE;
  FDFTInitInfo.FDImgFmtCH2 = FACEDETECT_IMG_RGB565;
  FDFTInitInfo.SDImgFmtCH1 = FACEDETECT_IMG_Y_SCALES;
  FDFTInitInfo.SDImgFmtCH2 = FACEDETECT_IMG_Y_SINGLE;
  FDFTInitInfo.SDThreshold = FDCustomData.SDThreshold;
  FDFTInitInfo.SDMainFaceMust = FDCustomData.SDMainFaceMust;
  FDFTInitInfo.GSensor = FDCustomData.GSensor;
  FDFTInitInfo.GenScaleImageBySw = 1;
  FDFTInitInfo.ParallelRGB565Conversion = true;
  FDFTInitInfo.LockOtBufferFunc = LockFTBuffer;
  FDFTInitInfo.UnlockOtBufferFunc = UnlockFTBuffer;
  FDFTInitInfo.lockAgent = this;
  FDFTInitInfo.DisLimit = 0;
  FDFTInitInfo.DecreaseStep = 0;
  FDFTInitInfo.OTBndOverlap = 8;
  FDFTInitInfo.OTds = 2;
  FDFTInitInfo.OTtype = 1;
  if (halFDGetVersion() < HAL_FD_VER_HW40) {
    FDFTInitInfo.DelayThreshold = 127;  // 127 is default value for FD3.5
    FDFTInitInfo.DelayCount = 3;        // 2 is default value
    FDFTInitInfo.DisLimit = 4;
    FDFTInitInfo.DecreaseStep = 384;
  } else {
    FDFTInitInfo.DelayThreshold = 75;  // 83 is default value for FD4.0
    FDFTInitInfo.DelayCount = 2;       // 2 is default value
  }
  FDFTInitInfo.FDManualMode = mUseUserScale;

#if USE_HW_FD
  FDFTInitInfo.LandmarkEnableCnt = FldNum;
  FDFTInitInfo.SilentModeFDSkipNum = 2;
#else
  FDFTInitInfo.LandmarkEnableCnt = 0;
  FDFTInitInfo.SilentModeFDSkipNum = 6;
#endif
  FDFTInitInfo.FDVersion = MTKCAM_HWFD_MAIN_VERSION;
#if (USE_HW_FD) && (HW_FD_SUBVERSION >= 2)
  FDFTInitInfo.FDMINSZ = 0;
#endif

  FDFTInitInfo.FLDAttribConfig = 1;
#if USE_HW_FD
  FDVT_OpenDriverWithUserCount(FDFTInitInfo.FDModel - 1);
  mFDModelVer = FDVT_GetModelVersion();
  MY_LOGD("FD4.0 model ver : %d", mFDModelVer);
  FDFTInitInfo.ModelVersion = mFDModelVer;
#endif
  // dump initial info
  dumpFDParam(FDFTInitInfo);
  // Set initial info to FD algo
  mpMTKFDVTObj->FDVTInit(&FDFTInitInfo);

  mEnableSWResizerFlag = SWResizerEnable;
  if (mEnableSWResizerFlag) {
    mImageScaleTotalSize = 0;
    for (i = 0; i < FD_SCALES; i++) {
      mImageScaleTotalSize += mImage_width_array[i] * mImage_height_array[i];
    }
    mpImageScaleBuffer = new unsigned char[mImageScaleTotalSize];
  }

  mpImageVGABuffer =
      new unsigned char[gimage_input_width_vga * gimage_input_height_buffer];
  memset(mpImageVGABuffer, 0,
         gimage_input_width_vga * gimage_input_height_buffer);

  MY_LOGD("[%s] End", __FUNCTION__);
  mFrameCount = 0;
  mDetectedFaceNum = 0;
  mInited = 1;
  return err;
}

MINT32
halFDVT::halFDGetVersion() {
#if (USE_HW_FD)
  return HAL_FD_VER_HW40 + HW_FD_SUBVERSION;
#endif  // end USE_HW_FD
}

MINT32
halFDVT::halFDDo(struct FD_Frame_Parameters const& Param) {
  std::lock_guard<std::mutex> _l(gInitLock);
  FdOptions FDOps;
  MINT32 StartPos = 0;
  MINT32 ScaleNum = mUserScaleNum;
  FDVT_OPERATION_MODE_ENUM Mode = FDVT_IDLE_MODE;
  fd_cal_struct pfd_cal_data;

  if (!mInited) {
    return MHAL_UNINIT_ERROR;
  }

  FACEDETECT_GSENSOR_DIRECTION direction;
  if (Param.Rotation_Info == 0) {
    direction = FACEDETECT_GSENSOR_DIRECTION_0;
  } else if (Param.Rotation_Info == 90) {
    direction = FACEDETECT_GSENSOR_DIRECTION_270;
  } else if (Param.Rotation_Info == 270) {
    direction = FACEDETECT_GSENSOR_DIRECTION_90;
  } else if (Param.Rotation_Info == 180) {
    direction = FACEDETECT_GSENSOR_DIRECTION_180;
  } else {
    direction = FACEDETECT_GSENSOR_DIRECTION_NO_SENSOR;
  }

  FDOps.gfd_fast_mode = 0;
#if (SINGLE_FACE_STABLE_ENABLE == 1)
  // Dynamic scaler
  if (mDetectedFaceNum != 0 && (mFrameCount % mFDRefresh) != 0) {
    MINT32 i;
    float WidthSize = static_cast<float>(2000.0 * mFDW) /
                      static_cast<float>(mFDW - (Param.padding_w * 2));
    float FaceRatio;
    MINT32 smallidx = 256, largeidx = -1;
    for (int j = 0; j < mDetectedFaceNum; j++) {
      FaceRatio = (static_cast<float>(mFaceResult[j].rect[2] -
                                      mFaceResult[j].rect[0])) /
                  WidthSize;
      MY_LOGD("FGFD Ratio : %f, Normalized width : %f", FaceRatio, WidthSize);
      for (i = 0; i < mUserScaleNum; i++) {
        if (FaceRatio <= (static_cast<float>(24) /
                          static_cast<float>(mImage_width_array[i]))) {
          break;
        }
      }
      MY_LOGD("WillDBG stop loop : %d", i);
      if (i != 0) {
        float leftdiff, rightdiff;
        leftdiff = FaceRatio - (static_cast<float>(24) /
                                static_cast<float>(mImage_width_array[i - 1]));
        rightdiff = (static_cast<float>(24) /
                     static_cast<float>(mImage_width_array[i])) -
                    FaceRatio;
        if (leftdiff < rightdiff) {
          i--;
        }
      }
      if (i < smallidx) {
        smallidx = i;
      }
      if (i > largeidx) {
        largeidx = i;
      }
    }
    if (largeidx <= 1) {
      StartPos = 0;
      ScaleNum = 3 + largeidx;
    } else if (smallidx >= (mUserScaleNum - 2)) {
      ScaleNum = 3 + (mUserScaleNum - smallidx - 1);
      StartPos = mUserScaleNum - ScaleNum;
    } else {
      StartPos = (smallidx >= 2) ? (smallidx - 2) : 0;
      ScaleNum = largeidx - smallidx + 5;
      if ((ScaleNum + StartPos) > mUserScaleNum) {
        ScaleNum = mUserScaleNum - StartPos;
      }
    }
    if (StartPos > mMustScaleIndex) {
      StartPos = mMustScaleIndex;
      ScaleNum = mUserScaleNum - StartPos;
    }
    FDOps.gfd_fast_mode = 1;
    MY_LOGD("WillDBG Start pos : %d, Scalenum : %d", StartPos, ScaleNum);
  }
  if (mDetectedFaceNum != 0) {
    Mode = FDVT_GFD_MODE;
  }
  mFrameCount++;
#endif

  // Set FD operation
  FDOps.fd_state = FDVT_GFD_MODE;
  FDOps.direction = direction;
  FDOps.fd_scale_count = ScaleNum;
  FDOps.fd_scale_start_position = StartPos;
  FDOps.ae_stable = Param.AEStable;
  FDOps.ForceFDMode = Mode;
  if (Param.pImageBufferPhyP2 != NULL) {
    FDOps.inputPlaneCount = 3;
  } else if (Param.pImageBufferPhyP1 != NULL) {
    FDOps.inputPlaneCount = 2;
  } else {
    FDOps.inputPlaneCount = 1;
  }
  FDOps.ImageBufferPhyPlane1 = Param.pImageBufferPhyP0;
  FDOps.ImageBufferPhyPlane2 = Param.pImageBufferPhyP1;
  FDOps.ImageBufferPhyPlane3 = Param.pImageBufferPhyP2;
  FDOps.ImageScaleBuffer = mpImageScaleBuffer;
  FDOps.ImageBufferRGB565 = Param.pRGB565Image;
  FDOps.ImageBufferSrcVirtual =
      reinterpret_cast<MUINT8*>(Param.pImageBufferVirtual);
  FDOps.startW = Param.padding_w;
  FDOps.startH = Param.padding_h;
  FDOps.model_version = mFDModelVer;

  if (mEnableSWResizerFlag) {
    mpMTKFDVTObj->FDVTMain(&FDOps, Param.mem_fd);
    if (FDOps.doPhase2) {
      mpMTKFDVTObj->FDGetCalData(&pfd_cal_data);  // Get From Algo
      // Add Driver Control
      //
      doHWFaceDetection(&pfd_cal_data, Param.mem_fd, Param.pImageBufferVirtual);
      mpMTKFDVTObj->FDSetCalData(&pfd_cal_data);
      mpMTKFDVTObj->FDVTMainPhase2();  // call To Algo
    }
  } else {
    FDOps.ImageScaleBuffer = Param.pScaleImages;
    mpMTKFDVTObj->FDVTMain(&FDOps, Param.mem_fd);
  }

  return MHAL_NO_ERROR;
}

// internal function
bool halFDVT::doHWFaceDetection(void* pCal_Data __unused,
                                int mem_fd,
                                MUINT8* va) {
#if USE_HW_FD
  fd_cal_struct* pfd_cal_data = reinterpret_cast<fd_cal_struct*>(pCal_Data);
  int i;
  FdDrv_input_struct mFDDrvInput;
  FdDrv_output_struct mFDDrvOutput;
  FdDrv_input_struct* FDDrvInput = &mFDDrvInput;
  FdDrv_output_struct* FDDrvOutput = &mFDDrvOutput;
  int width = pfd_cal_data->img_width_array[0];
  int start_pos = pfd_cal_data->fd_scale_start_position;
  FDDrvInput->fd_mode = 1;
  if (pfd_cal_data->inputPlaneCount == 1) {
    FDDrvInput->source_img_fmt = FMT_YUYV;
  } else {
    MY_LOGD("Warning!!!! the plane count : %d is not supported",
            pfd_cal_data->inputPlaneCount);
    FDDrvInput->source_img_fmt = FMT_YUYV;
  }
  FDDrvInput->scale_manual_mode = pfd_cal_data->fd_manual_mode;
  if (pfd_cal_data->fd_manual_mode) {
    FDDrvInput->source_img_width[0] = pfd_cal_data->fd_img_src_width;
    FDDrvInput->source_img_height[0] = pfd_cal_data->fd_img_src_height;
  } else {
    FDDrvInput->source_img_width[0] = 640;
    FDDrvInput->source_img_height[0] = 480;
  }
  int j;
  for (j = 0; j < 18; j++) {
    FDDrvInput->dynamic_change_model[j] = 0;
  }
  FDDrvInput->scale_num_from_user = pfd_cal_data->fd_scale_count;
  memcpy(
      &(FDDrvInput->source_img_width[1]),
      &(pfd_cal_data->img_width_array[start_pos]),
      sizeof(pfd_cal_data->img_width_array[0]) * pfd_cal_data->fd_scale_count);
  memcpy(
      &(FDDrvInput->source_img_height[1]),
      &(pfd_cal_data->img_height_array[start_pos]),
      sizeof(pfd_cal_data->img_height_array[0]) * pfd_cal_data->fd_scale_count);
  FDDrvInput->feature_threshold = 0;
  if (pfd_cal_data->scale_frame_division[0]) {
    FDDrvInput->GFD_skip = 1;
  } else {
    FDDrvInput->GFD_skip = 0;
  }
  FDDrvInput->GFD_skip_V = 0;
  FDDrvInput->RIP_feature = pfd_cal_data->current_feature_index;

  FDDrvInput->scale_from_original = 0;
  FDDrvInput->source_img_address = reinterpret_cast<MUINT64*>(va);
  FDDrvInput->source_img_address_UV = NULL;
  FDDrvInput->memFd = mem_fd;
  for (i = 0; i < MAX_FACE_SEL_NUM; i++) {
    pfd_cal_data->display_flag[i] = (kal_bool)0;
  }

  FDVT_Enque(FDDrvInput);
  FDVT_Deque(FDDrvOutput);

  for (i = 0; i < FDDrvOutput->new_face_number; i++) {
    pfd_cal_data->face_candi_pos_x0[i] =
        FDDrvOutput->face_candi_pos_x0[i] * width / 640;
    pfd_cal_data->face_candi_pos_y0[i] =
        FDDrvOutput->face_candi_pos_y0[i] * width / 640;
    pfd_cal_data->face_candi_pos_x1[i] =
        FDDrvOutput->face_candi_pos_x1[i] * width / 640;
    pfd_cal_data->face_candi_pos_y1[i] =
        FDDrvOutput->face_candi_pos_y1[i] * width / 640;
    pfd_cal_data->face_reliabiliy_value[i] =
        FDDrvOutput->face_reliabiliy_value[i];
    pfd_cal_data->display_flag[i] = (kal_bool)1;
    pfd_cal_data->face_feature_set_index[i] =
        FDDrvOutput->face_feature_set_index[i];
    pfd_cal_data->rip_dir[i] = FDDrvOutput->rip_dir[i];
    pfd_cal_data->rop_dir[i] = FDDrvOutput->rop_dir[i];
    pfd_cal_data->rop_dir[i] = FDDrvOutput->rop_dir[i];
    pfd_cal_data->result_type[i] = GFD_RST_TYPE;
    pfd_cal_data->face_candi_model[i] = 0;
  }
  if (halFDGetVersion() >= HAL_FD_VER_HW43) {
    return true;
  }
  // limit total result couldn't larger than 1024
  if (pfd_cal_data->current_feature_index >= 4 &&
      pfd_cal_data->current_feature_index <= 9) {
    int offset = FDDrvOutput->new_face_number;
    for (j = 0; j < 18; j++) {
      FDDrvInput->dynamic_change_model[j] = 1;
    }
    FDVT_Enque(FDDrvInput);
    FDVT_Deque(FDDrvOutput);
    if ((FDDrvOutput->new_face_number + offset) > MAX_FACE_SEL_NUM) {
      FDDrvOutput->new_face_number = MAX_FACE_SEL_NUM - offset;
    }
    for (i = 0; i < FDDrvOutput->new_face_number; i++) {
      pfd_cal_data->face_candi_pos_x0[i + offset] =
          FDDrvOutput->face_candi_pos_x0[i] * width / 640;
      pfd_cal_data->face_candi_pos_y0[i + offset] =
          FDDrvOutput->face_candi_pos_y0[i] * width / 640;
      pfd_cal_data->face_candi_pos_x1[i + offset] =
          FDDrvOutput->face_candi_pos_x1[i] * width / 640;
      pfd_cal_data->face_candi_pos_y1[i + offset] =
          FDDrvOutput->face_candi_pos_y1[i] * width / 640;
      pfd_cal_data->face_reliabiliy_value[i + offset] =
          FDDrvOutput->face_reliabiliy_value[i];
      pfd_cal_data->display_flag[i + offset] = (kal_bool)1;
      pfd_cal_data->face_feature_set_index[i + offset] =
          FDDrvOutput->face_feature_set_index[i];
      pfd_cal_data->rip_dir[i + offset] = FDDrvOutput->rip_dir[i];
      pfd_cal_data->rop_dir[i + offset] = FDDrvOutput->rop_dir[i];
      pfd_cal_data->rop_dir[i + offset] = FDDrvOutput->rop_dir[i];
      pfd_cal_data->result_type[i + offset] = GFD_RST_TYPE;
      pfd_cal_data->face_candi_model[i + offset] = 1;
    }
  }
#endif
  return true;
}
//

MINT32
halFDVT::halFDUninit() {
  std::lock_guard<std::mutex> _l(gInitLock);

  if (!mInited) {
    MY_LOGI("FD HAL Object is already uninited...");
    return MHAL_NO_ERROR;
  }

#if USE_HW_FD
  FDVT_CloseDriverWithUserCount();
#endif

  mpMTKFDVTObj->FDVTReset();

  if (mEnableSWResizerFlag) {
    delete[] mpImageScaleBuffer;
  }
  delete[] mpImageVGABuffer;
  mInited = 0;

  return MHAL_NO_ERROR;
}

MINT32
halFDVT::halFDGetFaceInfo(MtkCameraFaceMetadata* fd_info_result) {
  MUINT8 i;
  MY_LOGD("[GetFaceInfo] NUM_Face:%d,", mFdResult_Num);

  if ((mFdResult_Num < 0) || (mFdResult_Num > 15)) {
    mFdResult_Num = 0;
  }

  fd_info_result->number_of_faces = mFdResult_Num;

  for (i = 0; i < mFdResult_Num; i++) {
    fd_info_result->faces[i].rect[0] = mFdResult[i].rect[0];
    fd_info_result->faces[i].rect[1] = mFdResult[i].rect[1];
    fd_info_result->faces[i].rect[2] = mFdResult[i].rect[2];
    fd_info_result->faces[i].rect[3] = mFdResult[i].rect[3];
    fd_info_result->faces[i].score = mFdResult[i].score;
    fd_info_result->posInfo[i].rop_dir = mFdResult[i].rop_dir;
    fd_info_result->posInfo[i].rip_dir = mFdResult[i].rip_dir;
  }

  return MHAL_NO_ERROR;
}

MINT32
halFDVT::halFDGetFaceResult(MtkCameraFaceMetadata* fd_result,
                            MINT32 /*ResultMode*/
) {
  if (fd_result == nullptr) {
    MY_LOGE("fd_result is null");
    return -1;
  }
  MINT32 faceCnt = 0;
  MUINT8 i;
  MINT8 DrawMode = 0;
  MY_LOGD("[%s]first scale W(%d) H(%d)", __FUNCTION__, mImage_width_array[0],
          mImage_height_array[0]);
  mpMTKFDVTObj->FDVTGetResult(reinterpret_cast<MUINT8*>(fd_result),
                              mImage_width_array[0], mImage_height_array[0], 0,
                              0, 0, DrawMode);
  faceCnt = fd_result->number_of_faces;
  mDetectedFaceNum = mFdResult_Num = fd_result->number_of_faces;

  fd_result->CNNFaces.PortEnable = 0;
  fd_result->CNNFaces.IsTrueFace = 0;

  for (i = 0; i < mFdResult_Num; i++) {
    mFaceResult[i].rect[0] = fd_result->faces[i].rect[0];
    mFaceResult[i].rect[1] = fd_result->faces[i].rect[1];
    mFaceResult[i].rect[2] = fd_result->faces[i].rect[2];
    mFaceResult[i].rect[3] = fd_result->faces[i].rect[3];
  }

  // Facial Size Filter
  for (i = 0; i < mFdResult_Num; i++) {
    int diff = fd_result->faces[i].rect[3] - fd_result->faces[i].rect[1];
    float ratio = static_cast<float>(diff) / 2000.0;
    if (ratio < mFDFilterRatio) {
      int j;
      for (j = i; j < (mFdResult_Num - 1); j++) {
        fd_result->faces[j].rect[0] = fd_result->faces[j + 1].rect[0];
        fd_result->faces[j].rect[1] = fd_result->faces[j + 1].rect[1];
        fd_result->faces[j].rect[2] = fd_result->faces[j + 1].rect[2];
        fd_result->faces[j].rect[3] = fd_result->faces[j + 1].rect[3];
        fd_result->faces[j].score = fd_result->faces[j + 1].score;
        fd_result->faces[j].id = fd_result->faces[j + 1].id;
        fd_result->posInfo[j].rop_dir = fd_result->posInfo[j + 1].rop_dir;
        fd_result->posInfo[j].rip_dir = fd_result->posInfo[j + 1].rip_dir;
        fd_result->faces_type[j] = fd_result->faces_type[j + 1];
      }
      fd_result->faces[j].rect[0] = 0;
      fd_result->faces[j].rect[1] = 0;
      fd_result->faces[j].rect[2] = 0;
      fd_result->faces[j].rect[3] = 0;
      fd_result->faces[j].score = 0;
      fd_result->posInfo[j].rop_dir = 0;
      fd_result->posInfo[j].rip_dir = 0;
      fd_result->number_of_faces--;
      mFdResult_Num--;
      faceCnt--;
      i--;
    }
  }

  for (i = 0; i < mFdResult_Num; i++) {
    mFdResult[i].rect[0] = fd_result->faces[i].rect[0];
    mFdResult[i].rect[1] = fd_result->faces[i].rect[1];
    mFdResult[i].rect[2] = fd_result->faces[i].rect[2];
    mFdResult[i].rect[3] = fd_result->faces[i].rect[3];
    mFdResult[i].score = fd_result->faces[i].score;
    mFdResult[i].rop_dir = fd_result->posInfo[i].rop_dir;
    mFdResult[i].rip_dir = fd_result->posInfo[i].rip_dir;
  }
  for (i = mFdResult_Num; i < MAX_FACE_NUM; i++) {
    mFdResult[i].rect[0] = 0;
    mFdResult[i].rect[1] = 0;
    mFdResult[i].rect[2] = 0;
    mFdResult[i].rect[3] = 0;
    mFdResult[i].score = 0;
    mFdResult[i].rop_dir = 0;
    mFdResult[i].rip_dir = 0;
  }

  return faceCnt;
}

MINT32
halFDVT::halFDYUYV2ExtractY(MUINT8* dstAddr,
                            MUINT8* srcAddr,
                            MUINT32 src_width,
                            MUINT32 src_height) {
  MY_LOGD("DO Extract Y In");
  int i;

  int length = src_width * src_height * 2;

  for (i = length; i != 0; i -= 2) {
    *dstAddr++ = *srcAddr;
    srcAddr += 2;
  }

  MY_LOGD("DO Extract Y Out");

  return MHAL_NO_ERROR;
}

void halFDVT::LockFTBuffer(void* arg) {
  halFDVT* _this = reinterpret_cast<halFDVT*>(arg);
  ::sem_wait(&(_this->mSemFTLock));  // lock FT Buffer
}
void halFDVT::UnlockFTBuffer(void* arg) {
  halFDVT* _this = reinterpret_cast<halFDVT*>(arg);
  ::sem_post(&(_this->mSemFTLock));  // lock FT Buffer
}
void halFDVT::dumpFDParam(MTKFDFTInitInfo const& FDFTInitInfo) {
  MY_LOGD("WorkingBufAddr = %p", FDFTInitInfo.WorkingBufAddr);
  MY_LOGD("WorkingBufSize = %d", FDFTInitInfo.WorkingBufSize);
  MY_LOGD("FDThreadNum = %d", FDFTInitInfo.FDThreadNum);
  MY_LOGD("FDThreshold = %d", FDFTInitInfo.FDThreshold);
  MY_LOGD("DelayThreshold = %d", FDFTInitInfo.DelayThreshold);
  MY_LOGD("MajorFaceDecision = %d", FDFTInitInfo.MajorFaceDecision);
  MY_LOGD("OTRatio = %d", FDFTInitInfo.OTRatio);
  MY_LOGD("SmoothLevel = %d", FDFTInitInfo.SmoothLevel);
  MY_LOGD("FDSkipStep = %d", FDFTInitInfo.FDSkipStep);
  MY_LOGD("FDRectify = %d", FDFTInitInfo.FDRectify);
  MY_LOGD("FDRefresh = %d", FDFTInitInfo.FDRefresh);
  MY_LOGD("FDBufWidth = %d", FDFTInitInfo.FDBufWidth);
  MY_LOGD("FDBufHeight = %d", FDFTInitInfo.FDBufHeight);
  MY_LOGD("FDTBufWidth = %d", FDFTInitInfo.FDTBufWidth);
  MY_LOGD("FDTBufHeight = %d", FDFTInitInfo.FDTBufHeight);
  MY_LOGD("FDImageArrayNum = %d", FDFTInitInfo.FDImageArrayNum);
  MY_LOGD("FDImageWidthArray = ");
  for (int i = 0; i < FD_SCALES; i++) {
    MY_LOGD("%d, ", FDFTInitInfo.FDImageWidthArray[i]);
  }
  MY_LOGD("");
  MY_LOGD("FDImageHeightArray = ");
  for (int i = 0; i < FD_SCALES; i++) {
    MY_LOGD("%d, ", FDFTInitInfo.FDImageHeightArray[i]);
  }
  MY_LOGD("");
  MY_LOGD("FDMinFaceLevel = %d", FDFTInitInfo.FDMinFaceLevel);
  MY_LOGD("FDMaxFaceLevel = %d", FDFTInitInfo.FDMaxFaceLevel);
  MY_LOGD("FDImgFmtCH1 = %d", FDFTInitInfo.FDImgFmtCH1);
  MY_LOGD("FDImgFmtCH2 = %d", FDFTInitInfo.FDImgFmtCH2);
  MY_LOGD("SDImgFmtCH1 = %d", FDFTInitInfo.SDImgFmtCH1);
  MY_LOGD("SDImgFmtCH2 = %d", FDFTInitInfo.SDImgFmtCH2);
  MY_LOGD("SDThreshold = %d", FDFTInitInfo.SDThreshold);
  MY_LOGD("SDMainFaceMust = %d", FDFTInitInfo.SDMainFaceMust);
  MY_LOGD("GSensor = %d", FDFTInitInfo.GSensor);
  MY_LOGD("GenScaleImageBySw = %d", FDFTInitInfo.GenScaleImageBySw);
  MY_LOGD("FDManualMode = %d", FDFTInitInfo.FDManualMode);
  MY_LOGD("mUserScaleNum = %d", mUserScaleNum);
  MY_LOGD("FDVersion = %d", FDFTInitInfo.FDVersion);
#if (USE_HW_FD) && (HW_FD_SUBVERSION >= 2)
  MY_LOGD("FDMINSZ = %d", FDFTInitInfo.FDMINSZ);
#endif
  MY_LOGD("Version = %d", halFDGetVersion());
  MY_LOGD("DisLimit = %d", FDFTInitInfo.DisLimit);
  MY_LOGD("DecreaseStep = %d", FDFTInitInfo.DecreaseStep);
}
