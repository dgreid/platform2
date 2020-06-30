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

#define LOG_TAG "MtkCam/fdNodeImp"
#define FD_PIPE (1)
#include <cutils/compiler.h>

//
#include <mtkcam/pipeline/hwnode/FDNode.h>
#include <mtkcam/pipeline/pipeline/IPipelineNode.h>
#include <mtkcam/utils/std/Log.h>
#include "../BaseNode.h"
#if (FD_PIPE)
#include <faces.h>
#include <mtkcam/feature/FaceDetection/fd_hal_base.h>
#endif
#include <mtkcam/utils/hw/IFDContainer.h>
//
#include <sys/prctl.h>

// for sensor
#include <math.h>
#include <mtkcam/utils/sys/SensorProvider.h>
//
#include <mtkcam/aaa/IHal3A.h>
#include <mtkcam/utils/metadata/client/mtk_metadata_tag.h>
#include <mtkcam/utils/metadata/hal/mtk_platform_metadata_tag.h>
#include <mtkcam/utils/metastore/IMetadataProvider.h>

#include "../MyUtils.h"

#include <ctime>
#include <list>
#include <memory>
#include <thread>

#include <sys/syscall.h>

#include <mtkcam/drv/IHalSensor.h>
#include <property_service/property.h>
#include <property_service/property_lib.h>

#include <semaphore.h>
#define FD_SKIP_NUM (0)
#define DUMP_SENSOR_LOG (0)
#define DUMP_IMAGE (0)

#define MAX_DETECTED_FACES (15)
#define FD_BUFFER_SIZE (640 * 480)

using NSCam::IFDContainer;
using NSCam::IImageBuffer;
using NSCam::IImageBufferHeap;
using NSCam::IMetadata;
using NSCam::IMetadataProvider;
using NSCam::Type2Type;

struct CameraFacesDeleter {
  inline void operator()(MtkCameraFaceMetadata* faces) {
    // Delete face metadata buffer
    if (faces != nullptr) {
      if (faces->faces != nullptr) {
        delete[] faces->faces;
      }
      if (faces->posInfo != nullptr) {
        delete[] faces->posInfo;
      }
      delete faces;
    }
  }
};

typedef std::unique_ptr<MtkCameraFaceMetadata, struct CameraFacesDeleter>
    CameraFacesUniquePtr;

struct FDImage {
  int w;
  int h;
  MUINT8* AddrY;
  MUINT8* AddrU;
  MUINT8* AddrV;
  MUINT8* PAddrY;
  MINT32 format;
  MINT32 planes;
  MINT64 timestamp;
  MINT32 memFd;
  std::shared_ptr<IImageBuffer> pImg;
};

static timespec gUpdateTime;
#define MAX_DETECTION_NUM 15

/******************************************************************************
 *
 ******************************************************************************/
//
//  [Input]
//      Image/Yuv
//      Meta/Request
//
//  [Output]
//      Meta/Result
//
namespace {
class FdNodeImp : public NSCam::v3::BaseNode, public NSCam::v3::FdNode {
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  Implementations.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////                                            Definitions.
  typedef std::shared_ptr<NSCam::v3::IPipelineFrame> QueNode_T;
  typedef std::list<QueNode_T> Que_T;

 protected:  ////                    Data Members. (Config)
  mutable pthread_rwlock_t mConfigRWLock;
  std::shared_ptr<NSCam::v3::IMetaStreamInfo> mpOutMetaStreamInfo_Result;
  std::shared_ptr<NSCam::v3::IMetaStreamInfo> mpInMetaStreamInfo_Request;
  std::shared_ptr<NSCam::v3::IMetaStreamInfo> mpInMetaStreamInfo_P2Result;
  std::shared_ptr<NSCam::v3::IImageStreamInfo> mpInImageStreamInfo_Yuv;

 protected:  ////                    Data Members. (Request Queue)
  mutable std::mutex mRequestQueueLock;
  mutable std::mutex mResultLock;
  mutable std::mutex mFDRunningLock;
  std::condition_variable mRequestQueueCond;
  MBOOL mbRequestDrained;
  std::condition_variable mbRequestDrainedCond;
  Que_T mRequestQueue;

  MINT32 mLogLevel;

 protected:  ////                    Data Members. (Request Queue)
  std::shared_ptr<NS3Av3::IHal3A> mp3AHal;

 protected:  ////                    Operations.
  MERROR onDequeRequest(std::shared_ptr<NSCam::v3::IPipelineFrame>* rpFrame);
  MVOID onProcessFrame(
      std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame);

  MVOID waitForRequestDrained();

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  Operations in base class Thread
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////
  virtual void requestExit();

  virtual status_t readyToRun();

 private:
  virtual bool threadLoop();

  virtual bool _threadLoop();
  //
  MVOID ReturnFDResult(IMetadata* pOutMetadataResult,
                       IMetadata* pInpMetadataRequest,
                       IMetadata* pInpMetadataP2Result,
                       int img_w,
                       int img_h);

  MVOID resetFDNode();

  MVOID RunFaceDetection();
  static MVOID FDHalThreadLoop(MVOID*);

  MVOID setFDLock(MBOOL val);
  MINT32 onInitFDProc();

  MINT32 convertResult();

  MVOID prepareFDParams(IMetadata* pInpMetadataRequest,
                        IMetadata* pInpMetadataP2Result,
                        MSize imgSize);

  MINT32 tryToUpdateOldData(IMetadata* pOutMetadataResult, MINT32 FDMode);

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  Interface.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////                    Operations.
  FdNodeImp();

  ~FdNodeImp();

  virtual MERROR config(ConfigParams const& rParams);

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  IPipelineNode Interface.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////                    Operations.
  virtual MERROR init(InitParams const& rParams);

  virtual MERROR uninit();

  virtual MERROR flush();

  virtual MERROR flush(
      std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame);

  virtual MERROR queue(std::shared_ptr<NSCam::v3::IPipelineFrame> pFrame);

 private:
  // crop-related
  MRect mActiveArray;
  CameraFacesUniquePtr mpDetectedFaces;
  CameraFacesUniquePtr mpDetectedGestures;
  MBOOL mbInited;
  mutable std::mutex mInitLock;
  std::shared_ptr<halFDBase> mpFDHalObj;
  MINT32 mImageWidth;
  MINT32 mImageHeight;
  struct FDImage mDupImage;
  MINT32 mFDStopped;
  MINT32 mSensorRot;
  MINT32 mSensorFacing;
  // SD
  MINT32 mSD_Result;
  MINT32 mSDEnable;
  MINT32 mPrevSD;
  MINT32 mFDProcInited;
  MINT32 mFirstUpdate = 0;

  MINT32 mprvDegree = 0;
  MRect mcropRegion;
  std::thread mFDHalThread;
  sem_t semFD;
  mutable std::mutex mFDLock;
  MBOOL mIsFDBusy;
  MBOOL mStopFD;

  NSCam::IImageBufferAllocator* mAllocator;

  MBOOL mExitPending;
  std::thread mThread;
};
};  // namespace

/******************************************************************************
 *
 ******************************************************************************/
FdNodeImp::FdNodeImp()
    : BaseNode()
      //
      ,
      mp3AHal(nullptr),
      mbInited(MFALSE),
      mpFDHalObj(nullptr),
      mImageWidth(0),
      mImageHeight(0),
      mSD_Result(0),
      mSDEnable(0),
      mPrevSD(0),
      mFDProcInited(0),
      mAllocator(nullptr),
      mExitPending(MFALSE) {
  pthread_rwlock_init(&mConfigRWLock, NULL);
  mNodeName = "FdNode";  // default name
  mDupImage.pImg = nullptr;
  char cLogLevel[PROPERTY_VALUE_MAX];
  property_get("vendor.debug.camera.log", cLogLevel, "0");
  mLogLevel = atoi(cLogLevel);
  if (mLogLevel == 0) {
    property_get("vendor.debug.camera.log.FDNode", cLogLevel, "0");
    mLogLevel = atoi(cLogLevel);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
FdNodeImp::~FdNodeImp() {
  MY_LOGD("FDNode -");
  pthread_rwlock_destroy(&mConfigRWLock);
}

MINT32
FdNodeImp::onInitFDProc() {
  return 0;
}

MINT32
FdNodeImp::convertResult() {
#define CONVERT_X(_val) \
  _val = ((_val + 1000) * mcropRegion.s.w / 2000) + mcropRegion.p.x
#define CONVERT_Y(_val) \
  _val = ((_val + 1000) * mcropRegion.s.h / 2000) + mcropRegion.p.y
  for (int i = 0; i < mpDetectedFaces->number_of_faces; i++) {
    CONVERT_X(mpDetectedFaces->faces[i].rect[0]);  // Left
    CONVERT_Y(mpDetectedFaces->faces[i].rect[1]);  // Top
    CONVERT_X(mpDetectedFaces->faces[i].rect[2]);  // Right
    CONVERT_Y(mpDetectedFaces->faces[i].rect[3]);  // Bottom
    //
    CONVERT_X(mpDetectedFaces->leyex0[i]);
    CONVERT_Y(mpDetectedFaces->leyey0[i]);
    CONVERT_X(mpDetectedFaces->leyex1[i]);
    CONVERT_Y(mpDetectedFaces->leyey1[i]);
    CONVERT_X(mpDetectedFaces->reyex0[i]);
    CONVERT_Y(mpDetectedFaces->reyey0[i]);
    CONVERT_X(mpDetectedFaces->reyex1[i]);
    CONVERT_Y(mpDetectedFaces->reyey1[i]);
    CONVERT_X(mpDetectedFaces->mouthx0[i]);
    CONVERT_Y(mpDetectedFaces->mouthy0[i]);
    CONVERT_X(mpDetectedFaces->mouthx1[i]);
    CONVERT_Y(mpDetectedFaces->mouthy1[i]);
    CONVERT_X(mpDetectedFaces->nosex[i]);
    CONVERT_Y(mpDetectedFaces->nosey[i]);
    //
    CONVERT_X(mpDetectedFaces->leyeux[i]);
    CONVERT_Y(mpDetectedFaces->leyeuy[i]);
    CONVERT_X(mpDetectedFaces->leyedx[i]);
    CONVERT_Y(mpDetectedFaces->leyedy[i]);
    CONVERT_X(mpDetectedFaces->reyeux[i]);
    CONVERT_Y(mpDetectedFaces->reyeuy[i]);
    CONVERT_X(mpDetectedFaces->reyedx[i]);
    CONVERT_Y(mpDetectedFaces->reyedy[i]);
  }
#undef CONVERT_X
#undef CONVERT_Y
  // set hal 3A
  if (mp3AHal) {
    MY_LOGD_IF(mLogLevel, "set 3A fd info");
    mp3AHal->setFDInfoOnActiveArray(mpDetectedFaces.get());
  }
  // copy fd info to fd container
  {
    auto fdWriter = IFDContainer::createInstance(
        LOG_TAG, IFDContainer::eFDContainer_Opt_Write);
    auto fddata = fdWriter->editLock(mpDetectedFaces->timestamp);
    MY_LOGD_IF(mLogLevel, "store to fd container");
    clock_gettime(CLOCK_MONOTONIC, &gUpdateTime);
    MY_LOGD_IF(mLogLevel, "update: system time : %ld, %ld", gUpdateTime.tv_sec,
               gUpdateTime.tv_nsec);
    if (fddata != nullptr) {
      auto faces = fddata->facedata.faces;
      auto posinfo = fddata->facedata.posInfo;
      memcpy(faces, mpDetectedFaces->faces,
             sizeof(MtkCameraFace) * mpDetectedFaces->number_of_faces);
      memcpy(posinfo, mpDetectedFaces->posInfo,
             sizeof(MtkFaceInfo) * mpDetectedFaces->number_of_faces);
      memcpy(&(fddata->facedata), mpDetectedFaces.get(),
             sizeof(MtkCameraFaceMetadata));
      fddata->facedata.faces = faces;
      fddata->facedata.posInfo = posinfo;
      fdWriter->editUnlock(fddata);
    } else {
      MY_LOGW("get container FD buffer null");
    }
  }
  return 0;
}

MVOID
FdNodeImp::RunFaceDetection() {
  std::lock_guard<std::mutex> _l(mFDRunningLock);
  int srcWidth = mDupImage.w;
  int srcHeight = mDupImage.h;
  int numFace = 0;
  int FDRet = 0;
  int GSRet = 0;
  MINT32 AEStable = 1;

  if (mFDStopped) {
    return;
  }
  if (mImageWidth == 0 || mImageHeight == 0) {
    FDRet |= mpFDHalObj->halFDInit(srcWidth, srcHeight, 1, mSDEnable);
  } else if (mImageWidth != srcWidth || mImageHeight != srcHeight ||
             mSDEnable != mPrevSD) {
    FDRet |= mpFDHalObj->halFDUninit();
    FDRet |= mpFDHalObj->halFDInit(srcWidth, srcHeight, 1, mSDEnable);
  }
  if (FDRet || GSRet) {
    MY_LOGW("Init Failed!! FD status : %d, GS status : %d", FDRet, GSRet);
    return;
  }
  mPrevSD = mSDEnable;
  mImageWidth = srcWidth;
  mImageHeight = srcHeight;

  MY_LOGD_IF(mLogLevel, "halFDDo In.");
  if (mp3AHal) {
    mp3AHal->send3ACtrl(NS3Av3::E3ACtrl_GetIsAEStable, (MINTPTR)&AEStable, 0);
    MY_LOGD_IF(mLogLevel, "AE Stable : %d", AEStable);
  }

  // Do FD
  struct FD_Frame_Parameters Param;
  Param.pScaleImages = nullptr;
  Param.pRGB565Image = mDupImage.AddrY;
  Param.pPureYImage = nullptr;
  Param.pImageBufferVirtual = mDupImage.AddrY;
  Param.pImageBufferPhyP0 = reinterpret_cast<MUINT8*>(mDupImage.PAddrY);
  Param.pImageBufferPhyP1 = nullptr;
  Param.pImageBufferPhyP2 = nullptr;
  Param.Rotation_Info = mprvDegree;
  Param.SDEnable = mSDEnable;
  Param.AEStable = AEStable;
  Param.padding_w = 0;
  Param.padding_h = 0;
  Param.mem_fd = mDupImage.memFd;
  FDRet = mpFDHalObj->halFDDo(Param);
  if (FDRet) {
    MY_LOGW("halFDDo Failed!! FD status : %d", FDRet);
    mpFDHalObj->halFDUninit();
    mImageWidth = 0;
    mImageHeight = 0;
    return;
  }

  MY_LOGD_IF(mLogLevel, "halFDDo Out.");

  {
    std::lock_guard<std::mutex> _l(mResultLock);
    // reset face number
    mpDetectedFaces->number_of_faces = 0;

    numFace = mpFDHalObj->halFDGetFaceResult(mpDetectedFaces.get());

    MY_LOGD("NumFace = %d, ", numFace);
    mpDetectedFaces->ImgWidth = srcWidth;
    mpDetectedFaces->ImgHeight = srcHeight;
    mpDetectedFaces->timestamp = mDupImage.timestamp;
    // convert FD result to meet hal3 type and send to 3A
    convertResult();

    mFirstUpdate = 1;
  }
}

MVOID
FdNodeImp::setFDLock(MBOOL val) {
  std::lock_guard<std::mutex> _l(mFDLock);

  mIsFDBusy = val;
  return;
}

MVOID FdNodeImp::FDHalThreadLoop(MVOID* arg) {
  FdNodeImp* _FDNode = reinterpret_cast<FdNodeImp*>(arg);
  while (1) {
    sem_wait(&_FDNode->semFD);
    if (_FDNode->mStopFD) {
      break;
    }
    _FDNode->RunFaceDetection();
    _FDNode->setFDLock(MFALSE);
  }
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
FdNodeImp::init(InitParams const& rParams) {
  MY_LOGD("FdNode Initial !!!");
  {
    pthread_rwlock_wrlock(&mConfigRWLock);
    //
    mOpenId = rParams.openId;
    mNodeId = rParams.nodeId;
    mNodeName = rParams.nodeName;
    pthread_rwlock_unlock(&mConfigRWLock);
  }

  MAKE_Hal3A(
      mp3AHal, [](NS3Av3::IHal3A* p) { p->destroyInstance(LOG_TAG); },
      getOpenId(), LOG_TAG);

  mFDStopped = MFALSE;

  MtkCameraFaceMetadata* facesInput = new MtkCameraFaceMetadata;
  if (nullptr != facesInput) {
    MtkCameraFace* faces = new MtkCameraFace[MAX_DETECTED_FACES];
    MtkFaceInfo* posInfo = new MtkFaceInfo[MAX_DETECTED_FACES];

    if (nullptr != faces && nullptr != posInfo) {
      facesInput->faces = faces;
      facesInput->posInfo = posInfo;
      facesInput->number_of_faces = 0;
    } else {
      MY_LOGE("Fail to allocate Faceinfo buffer");
    }
  } else {
    MY_LOGE("Fail to allocate FaceMetadata buffer");
  }
  mpDetectedFaces.reset(facesInput);

  mpFDHalObj = halFDBase::createInstance(HAL_FD_OBJ_FDFT_SW);
  if (mpFDHalObj == nullptr) {
    MY_LOGE("Fail to create mpFDHalObj");
    return UNKNOWN_ERROR;
  }
  mImageWidth = 0;
  mImageHeight = 0;

  MtkCameraFaceMetadata* gesturesInput = new MtkCameraFaceMetadata;
  if (nullptr != gesturesInput) {
    MtkCameraFace* faces = new MtkCameraFace[MAX_DETECTED_FACES];
    MtkFaceInfo* posInfo = new MtkFaceInfo[MAX_DETECTED_FACES];

    if (nullptr != faces && nullptr != posInfo) {
      gesturesInput->faces = faces;
      gesturesInput->posInfo = posInfo;
      gesturesInput->number_of_faces = 0;
    } else {
      MY_LOGE("Fail to allocate Faceinfo buffer");
    }
  } else {
    MY_LOGE("Fail to allocate FaceMetadata buffer");
  }
  mpDetectedGestures.reset(gesturesInput);

  mFDProcInited = MFALSE;

  mFirstUpdate = 0;

  mIsFDBusy = MFALSE;
  mStopFD = MFALSE;

  mAllocator = NSCam::getImageBufferAllocator();
  NSCam::IImageBufferAllocator::ImgParam imgParam(FD_BUFFER_SIZE * 2, 0);
  NSCam::IImageBufferAllocator::ExtraParam extraParam(GRALLOC_USAGE_HW_TEXTURE);
  mDupImage.pImg = mAllocator->alloc("FDTempBuf", imgParam, extraParam);
  if (mDupImage.pImg == nullptr) {
    MY_LOGE("NULL Buffer");
    return MFALSE;
  }
  if (!mDupImage.pImg->lockBuf(
          "FDTempBuf",
          NSCam::eBUFFER_USAGE_HW_CAMERA_READ | NSCam::eBUFFER_USAGE_SW_MASK)) {
    mAllocator->free(mDupImage.pImg);
    MY_LOGE("lock Buffer failed");
    return MFALSE;
  }
  MY_LOGD("allocator buffer : %" PRIXPTR "", mDupImage.pImg->getBufVA(0));
  mDupImage.AddrY = reinterpret_cast<MUINT8*>(mDupImage.pImg->getBufVA(0));
  mDupImage.AddrU = mDupImage.AddrY + FD_BUFFER_SIZE;
  mDupImage.AddrV = mDupImage.AddrU + (FD_BUFFER_SIZE >> 2);
  mDupImage.memFd = mDupImage.pImg->getFD();

  sem_init(&semFD, 0, 0);
  mFDHalThread = std::thread(FDHalThreadLoop, this);

  mprvDegree = 360;
  //
  {
    std::shared_ptr<IMetadataProvider> pMetadataProvider =
        NSCam::NSMetadataProviderManager::valueFor(getOpenId());
    if (!pMetadataProvider) {
      MY_LOGE(" ! pMetadataProvider.get() ");
      return DEAD_OBJECT;
    }

    IMetadata static_meta = pMetadataProvider->getMtkStaticCharacteristics();
    {
      IMetadata::IEntry active_array_entry =
          static_meta.entryFor(MTK_SENSOR_INFO_ACTIVE_ARRAY_REGION);
      if (!active_array_entry.isEmpty()) {
        mActiveArray = active_array_entry.itemAt(0, Type2Type<MRect>());
        MY_LOGD_IF(1, "FD Node: active array(%d, %d, %dx%d)", mActiveArray.p.x,
                   mActiveArray.p.y, mActiveArray.s.w, mActiveArray.s.h);
      } else {
        MY_LOGE("no static info: MTK_SENSOR_INFO_ACTIVE_ARRAY_REGION");
        return UNKNOWN_ERROR;
      }
    }
    {
      IMetadata::IEntry facing_entry =
          static_meta.entryFor(MTK_SENSOR_INFO_FACING);
      mSensorFacing = -1;
      if (!facing_entry.isEmpty()) {
        mSensorFacing = facing_entry.itemAt(0, Type2Type<MUINT8>());
        MY_LOGD_IF(1, "FD Node: sensor facing : %d", mSensorFacing);
      } else {
        MY_LOGE("no static info: MTK_SENSOR_INFO_FACING");
        return UNKNOWN_ERROR;
      }
    }
    {
      IMetadata::IEntry rot_entry =
          static_meta.entryFor(MTK_SENSOR_INFO_ORIENTATION);
      mSensorRot = 0;
      if (!rot_entry.isEmpty()) {
        mSensorRot = rot_entry.itemAt(0, Type2Type<MINT32>());
        MY_LOGD_IF(1, "FD Node: sensor orientation : %d", mSensorRot);
        if (mSensorFacing == MTK_LENS_FACING_BACK) {
          mSensorRot -= 90;
        } else if (mSensorFacing == MTK_LENS_FACING_FRONT) {
          mSensorRot -= 270;
        } else {
          mSensorRot = 0;
        }
      } else {
        MY_LOGE("no static info: MTK_SENSOR_INFO_ORIENTATION");
        return UNKNOWN_ERROR;
      }
    }
  }
  //
  {
    std::lock_guard<std::mutex> _l(mInitLock);
    mbInited = MTRUE;
  }

  mThread = std::thread(std::bind(&FdNodeImp::threadLoop, this));
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
FdNodeImp::uninit() {
  flush();
  //
  {
    pthread_rwlock_wrlock(&mConfigRWLock);
    //
    mpOutMetaStreamInfo_Result = 0;
    mpInMetaStreamInfo_Request = 0;
    mpInImageStreamInfo_Yuv = 0;
    pthread_rwlock_unlock(&mConfigRWLock);
  }

  requestExit();

  mThread.join();

  mStopFD = MTRUE;
  sem_post(&semFD);
  mFDHalThread.join();
  sem_destroy(&semFD);
  if (mDupImage.pImg != nullptr && mAllocator != nullptr) {
    mDupImage.pImg->unlockBuf("FDTempBuf");
    mAllocator->free(mDupImage.pImg);
    mDupImage.pImg = nullptr;
  }

  if (mpFDHalObj != nullptr) {
    mpFDHalObj->halFDUninit();
    mpFDHalObj = nullptr;
  }

  mImageWidth = 0;
  mImageHeight = 0;

  mFDProcInited = MFALSE;
  mp3AHal = nullptr;
  {
    std::lock_guard<std::mutex> _l(mInitLock);
    mbInited = MFALSE;
  }
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
FdNodeImp::config(ConfigParams const& rParams) {
  if (!rParams.pInAppMeta) {
    return BAD_VALUE;
  }

  if (!rParams.pOutAppMeta) {
    return BAD_VALUE;
  }

  //
  pthread_rwlock_wrlock(&mConfigRWLock);
  //
  mpInMetaStreamInfo_Request = rParams.pInAppMeta;
  mpInMetaStreamInfo_P2Result = rParams.pInHalMeta;
  mpInImageStreamInfo_Yuv = rParams.vInImage;
  mpOutMetaStreamInfo_Result = rParams.pOutAppMeta;
  pthread_rwlock_unlock(&mConfigRWLock);
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
FdNodeImp::queue(std::shared_ptr<NSCam::v3::IPipelineFrame> pFrame) {
  std::lock_guard<std::mutex> _l(mRequestQueueLock);

  if (pFrame == nullptr) {
    MY_LOGE("pFrame is null");
  }
  MY_LOGD_IF(mLogLevel, "[queue] In frameNo : %d", pFrame->getFrameNo());
  //  Make sure the request with a smaller frame number has a higher priority.
  Que_T::iterator it = mRequestQueue.end();
  for (; it != mRequestQueue.begin();) {
    --it;
    if (pFrame->getFrameNo() >= (*it)->getFrameNo()) {
      ++it;  // insert(): insert before the current node
      break;
    }
  }
  mRequestQueue.insert(it, pFrame);
  mRequestQueueCond.notify_all();
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
FdNodeImp::waitForRequestDrained() {
  //
  std::unique_lock<std::mutex> _l(mRequestQueueLock);
  if (!mbRequestDrained) {
    MY_LOGD("wait for request drained");
    mbRequestDrainedCond.wait(_l);
  }
  //
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
FdNodeImp::flush(std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame) {
  return BaseNode::flush(pFrame);
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
FdNodeImp::flush() {
  MY_LOGD("+");
  //
  // 1. clear requests
  {
    std::lock_guard<std::mutex> _l(mRequestQueueLock);
    //
    Que_T::iterator it = mRequestQueue.begin();
    while (it != mRequestQueue.end()) {
      BaseNode::flush(*it);
      it = mRequestQueue.erase(it);
    }
  }
  //
  // 2. wait enque thread
  waitForRequestDrained();
  //
  // 3. clear working buffer

  MY_LOGD("-");

  // return INVALID_OPERATION;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
FdNodeImp::onDequeRequest(std::shared_ptr<NSCam::v3::IPipelineFrame>* rpFrame) {
  std::unique_lock<std::mutex> _l(mRequestQueueLock);

  MY_LOGD_IF(mLogLevel, "[onDequeRequest] In++");
  //
  //  Wait until the queue is not empty or this thread will exit.

  while (mRequestQueue.empty() && !mExitPending) {
    // set dained flag
    mbRequestDrained = MTRUE;
    mbRequestDrainedCond.notify_one();
    mRequestQueueCond.wait(_l);
    MY_LOGD_IF(mLogLevel, "[onDequeRequest] In_1");
  }

  //
  if (mExitPending) {
    MY_LOGI("[exitPending] mRequestQueue.size:%zu", mRequestQueue.size());
    return DEAD_OBJECT;
  }
  //
  //  Here the queue is not empty, take the first request from the queue.
  MY_LOGD_IF(mLogLevel, "[onDequeRequest] In_3 RequestQueue Size = %zu",
             mRequestQueue.size());
  mbRequestDrained = MFALSE;
  *rpFrame = *mRequestQueue.begin();
  mRequestQueue.erase(mRequestQueue.begin());
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
// Ask this object's thread to exit. This function is asynchronous, when the
// function returns the thread might still be running. Of course, this
// function can be called from a different thread.
void FdNodeImp::requestExit() {
  MY_LOGD("+");
  std::lock_guard<std::mutex> _l(mRequestQueueLock);

  mExitPending = MTRUE;

  mRequestQueueCond.notify_one();
  MY_LOGD("-");
}

/******************************************************************************
 *
 ******************************************************************************/
// Good place to do one-time initializations
status_t FdNodeImp::readyToRun() {
  ::prctl(PR_SET_NAME, (MUINT64) "Cam@FdNodeImp", 0, 0, 0);

  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
bool FdNodeImp::threadLoop() {
  while (this->_threadLoop() == true) {
  }
  MY_LOGI("threadLoop exit");
  return true;
}

/******************************************************************************
 *
 ******************************************************************************/
bool FdNodeImp::_threadLoop() {
  MY_LOGD("ThreadLoop In !!!");

  std::shared_ptr<NSCam::v3::IPipelineFrame> pFrame;
  if (OK == onDequeRequest(&pFrame) && pFrame != nullptr) {
    onProcessFrame(pFrame);
    return true;
  }

  MY_LOGD("FDnode exit threadloop");

  return false;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
FdNodeImp::resetFDNode() {
  MY_LOGD("FdNode Reset +++");
  std::lock_guard<std::mutex> _l(mFDRunningLock);
  mImageWidth = 0;
  mImageHeight = 0;
  mpFDHalObj->halFDUninit();
  mpDetectedFaces->number_of_faces = 0;
  mpDetectedGestures->number_of_faces = 0;
  mSD_Result = 0;
  mFDStopped = MTRUE;
  mFirstUpdate = 0;

  MY_LOGD("FdNode Reset ---");
}

/******************************************************************************
 *
 ******************************************************************************/
MINT32
FdNodeImp::tryToUpdateOldData(IMetadata* pOutMetadataResult, MINT32 FDMode) {
#define FDTOLERENCE (600)  // ms
  timespec now;
  MINT32 diffms = 0;
  clock_gettime(CLOCK_MONOTONIC, &now);
  diffms = ((now.tv_sec - gUpdateTime.tv_sec) * 1000) +
           ((now.tv_nsec - gUpdateTime.tv_nsec) / 1000000);
  MY_LOGD_IF(mLogLevel, "now timestamp : %ld, %ld", now.tv_sec, now.tv_nsec);
  if ((gUpdateTime.tv_sec == 0 && gUpdateTime.tv_nsec == 0) ||
      diffms > FDTOLERENCE) {
    MY_LOGD("time diffms is large : %d", diffms);
    return -1;
  }
  auto fdReader = IFDContainer::createInstance(
      LOG_TAG, IFDContainer::eFDContainer_Opt_Read);
  auto fdData = fdReader->queryLock();
  MY_LOGD_IF(mLogLevel, "get FD data : %d", fdData.size());
  if (fdData.size() > 0) {
    auto fdChunk = fdData.back();
    if (CC_LIKELY(fdChunk != nullptr) &&
        fdChunk->facedata.number_of_faces > 0) {
      MY_LOGD("Number_of_faces: %d", fdChunk->facedata.number_of_faces);
      // face,  15 is the max number of faces
      IMetadata::IEntry face_rect_tag(MTK_STATISTICS_FACE_RECTANGLES);
      for (MINT32 i = 0; i < fdChunk->facedata.number_of_faces; i++) {
        MRect face_rect;
        face_rect.p.x = fdChunk->faces[i].rect[0];  // Left
        face_rect.p.y = fdChunk->faces[i].rect[1];  // Top
        face_rect.s.w = fdChunk->faces[i].rect[2];  // Right
        face_rect.s.h = fdChunk->faces[i].rect[3];  // Bottom
        face_rect_tag.push_back(face_rect, Type2Type<MRect>());
      }
      pOutMetadataResult->update(MTK_STATISTICS_FACE_RECTANGLES, face_rect_tag);

      IMetadata::IEntry face_landmark_tag(MTK_STATISTICS_FACE_LANDMARKS);
      MINT32 face_landmark;
      MINT32 face_id;
      MUINT8 face_score;
      for (MINT32 i = 0; i < fdChunk->facedata.number_of_faces; i++) {
        face_landmark = (FDMode == MTK_STATISTICS_FACE_DETECT_MODE_FULL)
                            ? (fdChunk->faces[i].left_eye[0])
                            : 0;  // left_eye_x
        face_landmark_tag.push_back(face_landmark, Type2Type<MINT32>());
        face_landmark = (FDMode == MTK_STATISTICS_FACE_DETECT_MODE_FULL)
                            ? (fdChunk->faces[i].left_eye[1])
                            : 0;  // left_eye_y
        face_landmark_tag.push_back(face_landmark, Type2Type<MINT32>());
        face_landmark = (FDMode == MTK_STATISTICS_FACE_DETECT_MODE_FULL)
                            ? (fdChunk->faces[i].right_eye[0])
                            : 0;  // right_eye_x
        face_landmark_tag.push_back(face_landmark, Type2Type<MINT32>());
        face_landmark = (FDMode == MTK_STATISTICS_FACE_DETECT_MODE_FULL)
                            ? (fdChunk->faces[i].right_eye[1])
                            : 0;  // right_eye_y
        face_landmark_tag.push_back(face_landmark, Type2Type<MINT32>());
        face_landmark = (FDMode == MTK_STATISTICS_FACE_DETECT_MODE_FULL)
                            ? (fdChunk->faces[i].mouth[0])
                            : 0;  // mouth_x
        face_landmark_tag.push_back(face_landmark, Type2Type<MINT32>());
        face_landmark = (FDMode == MTK_STATISTICS_FACE_DETECT_MODE_FULL)
                            ? (fdChunk->faces[i].mouth[1])
                            : 0;  // mouth_y
        face_landmark_tag.push_back(face_landmark, Type2Type<MINT32>());
      }
      pOutMetadataResult->update(MTK_STATISTICS_FACE_LANDMARKS,
                                 face_landmark_tag);

      IMetadata::IEntry face_id_tag(MTK_STATISTICS_FACE_IDS);
      for (MINT32 i = 0; i < fdChunk->facedata.number_of_faces; i++) {
        face_id = (FDMode == MTK_STATISTICS_FACE_DETECT_MODE_FULL)
                      ? fdChunk->faces[i].id
                      : -1;
        face_id_tag.push_back(face_id, Type2Type<MINT32>());
      }
      pOutMetadataResult->update(MTK_STATISTICS_FACE_IDS, face_id_tag);

      IMetadata::IEntry face_score_tag(MTK_STATISTICS_FACE_SCORES);
      for (MINT32 i = 0; i < fdChunk->facedata.number_of_faces; i++) {
        face_score = fdChunk->faces[i].score;
        face_score_tag.push_back(face_score, Type2Type<MUINT8>());
      }
      pOutMetadataResult->update(MTK_STATISTICS_FACE_SCORES, face_score_tag);
      fdReader->queryUnlock(fdData);
      return 0;
    }
  }
  fdReader->queryUnlock(fdData);
  return -1;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
FdNodeImp::ReturnFDResult(IMetadata* pOutMetadataResult,
                          IMetadata* pInpMetadataRequest,
                          IMetadata* pInpMetadataP2Result,
                          int img_w,
                          int img_h) {
  MRect cropRegion;
  MRect face_rect;
  MINT32 face_landmark;
  MINT32 face_id;
  MUINT8 face_score;
  MINT32 FDEn = 0;
  MINT32 SDEn = 0;
  MUINT8 FDMode = MTK_STATISTICS_FACE_DETECT_MODE_OFF;

  int fake_face = (mLogLevel >= 2);

  std::lock_guard<std::mutex> _l(mResultLock);
  {
    {
      IMetadata::IEntry const& entryMode =
          pInpMetadataRequest->entryFor(MTK_STATISTICS_FACE_DETECT_MODE);
      if (!entryMode.isEmpty() &&
          MTK_STATISTICS_FACE_DETECT_MODE_OFF !=
              entryMode.itemAt(0, Type2Type<MUINT8>())) {
        FDEn = 1;
        FDMode = entryMode.itemAt(0, Type2Type<MUINT8>());
        pOutMetadataResult->update(MTK_STATISTICS_FACE_DETECT_MODE, entryMode);
      }
    }
    {
      IMetadata::IEntry const& entryMode =
          pInpMetadataRequest->entryFor(MTK_FACE_FEATURE_SMILE_DETECT_MODE);
      if (!entryMode.isEmpty() &&
          MTK_FACE_FEATURE_SMILE_DETECT_MODE_OFF !=
              entryMode.itemAt(0, Type2Type<MINT32>())) {
        SDEn = 1;
      }
    }
  }
  if (fake_face) {
    mpDetectedFaces->number_of_faces = 2;
    mpDetectedFaces->faces[0].rect[0] = -100;
    mpDetectedFaces->faces[0].rect[1] = -100;
    mpDetectedFaces->faces[0].rect[2] = 100;
    mpDetectedFaces->faces[0].rect[3] = 100;
  }

  if (!mFirstUpdate) {
    MINT32 ret = 0;
    ret = tryToUpdateOldData(pOutMetadataResult, FDMode);
    if (!ret) {
      return;
    }
  }

  if (mpDetectedFaces->number_of_faces == 0) {
    return;
  }

  if (FDEn) {
    // Push_back Rectangle (face_rect)
    IMetadata::IEntry face_rect_tag(MTK_STATISTICS_FACE_RECTANGLES);
    for (int i = 0; i < mpDetectedFaces->number_of_faces; i++) {
      face_rect.p.x = mpDetectedFaces->faces[i].rect[0];  // Left
      face_rect.p.y = mpDetectedFaces->faces[i].rect[1];  // Top
      face_rect.s.w = mpDetectedFaces->faces[i].rect[2];  // Right
      face_rect.s.h = mpDetectedFaces->faces[i].rect[3];  // Bottom
      if (fake_face) {
        // Add fix center face box for debug.
        if (i == 1) {
          face_rect.p.x = (mActiveArray.s.w / 2) - 100;
          face_rect.p.y = (mActiveArray.s.h / 2) - 100;
          face_rect.s.w = (mActiveArray.s.w / 2) + 100;
          face_rect.s.h = (mActiveArray.s.h / 2) + 100;
        }
        mpDetectedFaces->faces[i].score = 100;
        MY_LOGD("face num : %d, position : (%d, %d) , (%d, %d)", i,
                face_rect.p.x, face_rect.p.y, face_rect.s.w, face_rect.s.h);
      }

      if (FDMode != MTK_STATISTICS_FACE_DETECT_MODE_FULL) {
        // Only available if android.statistics.faceDetectMode == FULL
        mpDetectedFaces->faces[i].id = -1;
        mpDetectedFaces->faces[i].left_eye[0] = 0;
        mpDetectedFaces->faces[i].left_eye[1] = 0;
        mpDetectedFaces->faces[i].right_eye[0] = 0;
        mpDetectedFaces->faces[i].right_eye[1] = 0;
        mpDetectedFaces->faces[i].mouth[0] = 0;
        mpDetectedFaces->faces[i].mouth[1] = 0;
      }
      if (mpDetectedFaces->faces[i].score > 100) {
        mpDetectedFaces->faces[i].score = 100;
      }
      face_rect_tag.push_back(face_rect, Type2Type<MRect>());
    }
    pOutMetadataResult->update(MTK_STATISTICS_FACE_RECTANGLES, face_rect_tag);

    // Push_back Landmark (face_landmark)
    IMetadata::IEntry face_landmark_tag(MTK_STATISTICS_FACE_LANDMARKS);
    for (int i = 0; i < mpDetectedFaces->number_of_faces; i++) {
      face_landmark = (mpDetectedFaces->faces[i].left_eye[0]);  // left_eye_x
      face_landmark_tag.push_back(face_landmark, Type2Type<MINT32>());
      face_landmark = (mpDetectedFaces->faces[i].left_eye[1]);  // left_eye_y
      face_landmark_tag.push_back(face_landmark, Type2Type<MINT32>());
      face_landmark = (mpDetectedFaces->faces[i].right_eye[0]);  // right_eye_x
      face_landmark_tag.push_back(face_landmark, Type2Type<MINT32>());
      face_landmark = (mpDetectedFaces->faces[i].right_eye[1]);  // right_eye_y
      face_landmark_tag.push_back(face_landmark, Type2Type<MINT32>());
      face_landmark = (mpDetectedFaces->faces[i].mouth[0]);  // mouth_x
      face_landmark_tag.push_back(face_landmark, Type2Type<MINT32>());
      face_landmark = (mpDetectedFaces->faces[i].mouth[1]);  // mouth_y
      face_landmark_tag.push_back(face_landmark, Type2Type<MINT32>());
    }
    pOutMetadataResult->update(MTK_STATISTICS_FACE_LANDMARKS,
                               face_landmark_tag);

    // Push_back IDs
    IMetadata::IEntry face_id_tag(MTK_STATISTICS_FACE_IDS);
    for (int i = 0; i < mpDetectedFaces->number_of_faces; i++) {
      face_id = mpDetectedFaces->faces[i].id;
      face_id_tag.push_back(face_id, Type2Type<MINT32>());
    }
    pOutMetadataResult->update(MTK_STATISTICS_FACE_IDS, face_id_tag);

    // Push_back Score
    IMetadata::IEntry face_score_tag(MTK_STATISTICS_FACE_SCORES);
    for (int i = 0; i < mpDetectedFaces->number_of_faces; i++) {
      face_score = mpDetectedFaces->faces[i].score;
      face_score_tag.push_back(face_score, Type2Type<MUINT8>());
    }
    pOutMetadataResult->update(MTK_STATISTICS_FACE_SCORES, face_score_tag);

    // Check Metadata Content
    // IMetadata::IEntry entry_check =
    // pOutMetadataResult->entryFor(MTK_STATISTICS_FACE_RECTANGLES); MRect
    // checkRect = entry_check.itemAt(0, Type2Type<MRect>());
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
FdNodeImp::prepareFDParams(IMetadata* pInpMetadataRequest,
                           IMetadata* pInpMetadataP2Result,
                           MSize imgSize) {
  MINT32 oldCropW, oldCropH;
  IMetadata::IEntry FDCrop =
      pInpMetadataP2Result->entryFor(MTK_P2NODE_FD_CROP_REGION);
  if (!FDCrop.isEmpty()) {
    mcropRegion = FDCrop.itemAt(0, Type2Type<MRect>());
  } else {
    MY_LOGD("no FDCrop from P2, use App scaler crop");
    IMetadata::IEntry entry =
        pInpMetadataRequest->entryFor(MTK_SCALER_CROP_REGION);
    if (!entry.isEmpty()) {
      mcropRegion = entry.itemAt(0, Type2Type<MRect>());
    } else {
      MY_LOGW("GetCropRegion Fail!");
    }
  }
  IMetadata::IEntry FDTime =
      pInpMetadataP2Result->entryFor(MTK_P1NODE_FRAME_START_TIMESTAMP);
  if (!FDTime.isEmpty()) {
    mDupImage.timestamp = FDTime.itemAt(0, Type2Type<MINT64>());
  } else {
    MY_LOGW("Get timestamp fail!!!");
  }
  MY_LOGD_IF(mLogLevel, "frame start time : %" PRId64 " ", mDupImage.timestamp);

  oldCropW = mcropRegion.s.w;
  oldCropH = mcropRegion.s.h;
  MY_LOGD_IF(mLogLevel, "old CropRegion: p.x:%d, p.y:%d, s.w:%d, s.h:%d, ",
             mcropRegion.p.x, mcropRegion.p.y, mcropRegion.s.w,
             mcropRegion.s.h);
  if ((mcropRegion.s.w * imgSize.h) >
      (mcropRegion.s.h * imgSize.w)) {  // pillarbox
    // MY_LOGD("pillarbox");
    mcropRegion.s.w =
        NSCam::v3::div_round(mcropRegion.s.h * imgSize.w, imgSize.h);
    mcropRegion.s.h = mcropRegion.s.h;
    mcropRegion.p.x = mcropRegion.p.x + ((oldCropW - mcropRegion.s.w) >> 1);
    mcropRegion.p.y = mcropRegion.p.y;
  } else if ((mcropRegion.s.w * imgSize.h) <
             (mcropRegion.s.h * imgSize.w)) {  // letterbox
    // MY_LOGD("letterbox");
    mcropRegion.s.w = mcropRegion.s.w;
    mcropRegion.s.h =
        NSCam::v3::div_round(mcropRegion.s.w * imgSize.h, imgSize.w);
    mcropRegion.p.x = mcropRegion.p.x;
    mcropRegion.p.y = mcropRegion.p.y + ((oldCropH - mcropRegion.s.h) >> 1);
  }

  MY_LOGD_IF(mLogLevel, "new CropRegion: p.x:%d, p.y:%d, s.w:%d, s.h:%d, ",
             mcropRegion.p.x, mcropRegion.p.y, mcropRegion.s.w,
             mcropRegion.s.h);
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
FdNodeImp::onProcessFrame(
    std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame) {
  MY_LOGD_IF(mLogLevel, "[onProcessFrame] In FrameNo : %d",
             pFrame->getFrameNo());
  NSCam::v3::StreamId_T const streamIdOutMetaResult =
      mpOutMetaStreamInfo_Result->getStreamId();
  std::shared_ptr<NSCam::v3::IMetaStreamBuffer> pOutMetaStreamBufferResult =
      nullptr;
  IMetadata* pOutMetadataResult = nullptr;

  NSCam::v3::StreamId_T const streamIdInpMetaRequest =
      mpInMetaStreamInfo_Request->getStreamId();
  std::shared_ptr<NSCam::v3::IMetaStreamBuffer> pInpMetaStreamBufferRequest =
      nullptr;
  IMetadata* pInpMetadataRequest = nullptr;

  NSCam::v3::StreamId_T const streamIdInpMetaP2Result =
      mpInMetaStreamInfo_P2Result->getStreamId();
  std::shared_ptr<NSCam::v3::IMetaStreamBuffer> pInpMetaStreamBufferP2Result =
      nullptr;
  IMetadata* pInpMetadataP2Result = nullptr;

  NSCam::v3::StreamId_T const streamIdInpImageYuv =
      mpInImageStreamInfo_Yuv->getStreamId();
  std::shared_ptr<NSCam::v3::IImageStreamBuffer> pInpImageStreamBufferYuv =
      nullptr;
  std::shared_ptr<IImageBufferHeap> pInpImageBufferHeapYuv = nullptr;
  std::shared_ptr<IImageBuffer> pInpImageBufferYuv = nullptr;
  NSCam::v3::IStreamBufferSet& rStreamBufferSet = pFrame->getStreamBufferSet();

  MINT32 success = 0;
  MINT32 SDEn = 0;
  MINT32 FDEn = 0;
  MINT32 RunFD = 0;

  auto getMetaBuffer =
      [&](NSCam::v3::StreamId_T const streamId,
          std::shared_ptr<NSCam::v3::IMetaStreamBuffer>& metaBuffer) -> int {
    MERROR const err = ensureMetaBufferAvailable(
        pFrame->getFrameNo(), streamId, &rStreamBufferSet, &metaBuffer);
    if (err != OK) {
      MY_LOGW("cannot get meta: streamId %#" PRIx64 " of frame %d", streamId,
              pFrame->getFrameNo());
      return false;
    }
    return true;
  };

  {
    std::lock_guard<std::mutex> _l(mInitLock);
    if (!mbInited) {
      goto lbExit;
    }
  }

  if (!mFDProcInited) {
    onInitFDProc();
    mFDProcInited = MTRUE;
  }
  //
  ////////////////////////////////////////////////////////////////////////////
  //  Ensure buffers available.
  ////////////////////////////////////////////////////////////////////////////
  //  Output Meta Stream: Result
  if (!getMetaBuffer(streamIdOutMetaResult, pOutMetaStreamBufferResult)) {
    goto lbExit;
  }
  //  Input Meta Stream: Request
  if (!getMetaBuffer(streamIdInpMetaRequest, pInpMetaStreamBufferRequest)) {
    goto lbExit;
  }
  //  Input Meta Stream: P2 hal result
  if (!getMetaBuffer(streamIdInpMetaP2Result, pInpMetaStreamBufferP2Result)) {
    goto lbExit;
  }
  //
  //  Input Image Stream: YUV
  {
    NSCam::v3::StreamId_T const streamId = streamIdInpImageYuv;
    MERROR const err = ensureImageBufferAvailable(pFrame->getFrameNo(),
                                                  streamId, &rStreamBufferSet,
                                                  &pInpImageStreamBufferYuv);
    // Should check the returned error code!!!
    if (err != OK) {
      MY_LOGW("cannot get input YUV: streamId %#" PRIx64 " of frame %d",
              streamId, pFrame->getFrameNo());
      goto lbExit;
    }
  }

  success = 1;

  ////////////////////////////////////////////////////////////////////////////
  //  Prepare buffers before using.
  ////////////////////////////////////////////////////////////////////////////
  // Output Meta Stream: Result
  {{pOutMetadataResult =
        pOutMetaStreamBufferResult->tryWriteLock(getNodeName());
}
//
//  Input Meta Stream: Request
{
  pInpMetadataRequest = pInpMetaStreamBufferRequest->tryReadLock(getNodeName());
}
//  Input Meta Stream: P2 hal result
{
  pInpMetadataP2Result =
      pInpMetaStreamBufferP2Result->tryReadLock(getNodeName());
}
//
//  Input Image Stream: YUV
{
  pInpImageBufferHeapYuv.reset(
      pInpImageStreamBufferYuv->tryReadLock(getNodeName()),
      [](IImageBufferHeap* p) { MY_LOGI("release implement"); });
  pInpImageBufferYuv = pInpImageBufferHeapYuv->createImageBuffer();
  // pInpImageBufferYuv->lockBuf(getNodeName(), groupUsage);

  MUINT const usage = NSCam::eBUFFER_USAGE_SW_READ_OFTEN |
                      NSCam::eBUFFER_USAGE_HW_CAMERA_READWRITE;
  pInpImageBufferYuv->lockBuf(getNodeName(), usage);
}
}

//*********************************************************************//

//****************************************************//
// Test
// SDEn =  1; //Pass
// GDEn =  1; //Pass
// ASDEn = 1; //Pass
//****************************************************//
{
  {
    IMetadata::IEntry const& entryMode =
        pInpMetadataRequest->entryFor(MTK_STATISTICS_FACE_DETECT_MODE);
    if (!entryMode.isEmpty() && MTK_STATISTICS_FACE_DETECT_MODE_OFF !=
                                    entryMode.itemAt(0, Type2Type<MUINT8>())) {
      FDEn = 1;
    }
  }
  {
    IMetadata::IEntry const& entryMode =
        pInpMetadataRequest->entryFor(MTK_CONTROL_SCENE_MODE);
    if (!entryMode.isEmpty() && MTK_CONTROL_SCENE_MODE_FACE_PRIORITY ==
                                    entryMode.itemAt(0, Type2Type<MUINT8>())) {
      FDEn = 1;
    }
  }
  {
    IMetadata::IEntry const& entryMode =
        pInpMetadataRequest->entryFor(MTK_FACE_FEATURE_SMILE_DETECT_MODE);
    if (!entryMode.isEmpty() && MTK_FACE_FEATURE_SMILE_DETECT_MODE_OFF !=
                                    entryMode.itemAt(0, Type2Type<MINT32>())) {
      SDEn = 1;
    }
  }
}
MY_LOGD_IF(mLogLevel, "FD_DEBUG : FDEn : %d, SDEn : %d", FDEn, SDEn);
RunFD = FDEn | SDEn;
if (!RunFD) {
  MY_LOGD("FD node go to suspend....Reset FD node");
  resetFDNode();
  goto lbExit;
}
mFDStopped = MFALSE;

if (!mIsFDBusy) {
  mDupImage.w = pInpImageBufferYuv->getImgSize().w;
  mDupImage.h = pInpImageBufferYuv->getImgSize().h;
  mDupImage.planes = pInpImageBufferYuv->getPlaneCount();
  if (mDupImage.planes == 3) {
    memcpy(mDupImage.AddrY,
           reinterpret_cast<void*>(pInpImageBufferYuv->getBufVA(0)),
           pInpImageBufferYuv->getImgSize().w *
               pInpImageBufferYuv->getImgSize().h);
    memcpy(mDupImage.AddrU,
           reinterpret_cast<void*>(pInpImageBufferYuv->getBufVA(1)),
           (pInpImageBufferYuv->getImgSize().w *
            pInpImageBufferYuv->getImgSize().h) >>
               2);
    memcpy(mDupImage.AddrV,
           reinterpret_cast<void*>(pInpImageBufferYuv->getBufVA(2)),
           (pInpImageBufferYuv->getImgSize().w *
            pInpImageBufferYuv->getImgSize().h) >>
               2);
  } else if (mDupImage.planes == 1) {
    memcpy(mDupImage.AddrY,
           reinterpret_cast<void*>(pInpImageBufferYuv->getBufVA(0)),
           pInpImageBufferYuv->getImgSize().w *
               pInpImageBufferYuv->getImgSize().h * 2);

  } else if (mDupImage.planes == 2) {
    MY_LOGW("FD node could not be here, not support buffer plane == 2");
  }
  mDupImage.PAddrY = reinterpret_cast<MUINT8*>(mDupImage.pImg->getBufPA(0));
  mDupImage.pImg->syncCache(NSCam::eCACHECTRL_FLUSH);
  mSDEnable = SDEn;

  prepareFDParams(pInpMetadataRequest, pInpMetadataP2Result,
                  pInpImageBufferYuv->getImgSize());
  setFDLock(MTRUE);
  sem_post(&semFD);
}

ReturnFDResult(pOutMetadataResult,
               pInpMetadataRequest,
               pInpMetadataP2Result,
               pInpImageBufferYuv->getImgSize().w,
               pInpImageBufferYuv->getImgSize().h);

////////////////////////////////////////////////////////////////////////////
//  Release buffers after using.
////////////////////////////////////////////////////////////////////////////
lbExit :
//
//  Output Meta Stream: Result
{
  NSCam::v3::StreamId_T const streamId = streamIdOutMetaResult;
  //
  if (pOutMetaStreamBufferResult != nullptr) {
    // Buffer Producer must set this status.
    pOutMetaStreamBufferResult->markStatus(
        success ? NSCam::v3::STREAM_BUFFER_STATUS::WRITE_OK
                : NSCam::v3::STREAM_BUFFER_STATUS::WRITE_ERROR);
    if (pOutMetadataResult != nullptr) {
      pOutMetaStreamBufferResult->unlock(getNodeName(), pOutMetadataResult);
    }
    //
    rStreamBufferSet.markUserStatus(
        streamId, getNodeId(),
        NSCam::v3::IUsersManager::UserStatus::USED |
            NSCam::v3::IUsersManager::UserStatus::RELEASE);
  } else {
    rStreamBufferSet.markUserStatus(
        streamId, getNodeId(), NSCam::v3::IUsersManager::UserStatus::RELEASE);
  }
}
//
//  Input Meta Stream: Request
{
  NSCam::v3::StreamId_T const streamId = streamIdInpMetaRequest;
  //
  if (pInpMetaStreamBufferRequest != nullptr) {
    if (pInpMetadataRequest != nullptr) {
      pInpMetaStreamBufferRequest->unlock(getNodeName(), pInpMetadataRequest);
    }
    //
    rStreamBufferSet.markUserStatus(
        streamId, getNodeId(),
        NSCam::v3::IUsersManager::UserStatus::USED |
            NSCam::v3::IUsersManager::UserStatus::RELEASE);
  } else {
    rStreamBufferSet.markUserStatus(
        streamId, getNodeId(), NSCam::v3::IUsersManager::UserStatus::RELEASE);
  }
}
//  Input Meta Stream: P2 hal
{
  NSCam::v3::StreamId_T const streamId = streamIdInpMetaP2Result;
  //
  if (pInpMetaStreamBufferP2Result != nullptr) {
    if (pInpMetadataP2Result != nullptr) {
      pInpMetaStreamBufferP2Result->unlock(getNodeName(), pInpMetadataP2Result);
    }
    //
    rStreamBufferSet.markUserStatus(
        streamId, getNodeId(),
        NSCam::v3::IUsersManager::UserStatus::USED |
            NSCam::v3::IUsersManager::UserStatus::RELEASE);
  } else {
    rStreamBufferSet.markUserStatus(
        streamId, getNodeId(), NSCam::v3::IUsersManager::UserStatus::RELEASE);
  }
}
//
//  Input Image Stream: YUV
{
  NSCam::v3::StreamId_T const streamId = streamIdInpImageYuv;
  //
  if (pInpImageStreamBufferYuv != nullptr) {
    if (pInpImageBufferYuv != nullptr) {
      pInpImageBufferYuv->unlockBuf(getNodeName());
    }
    if (pInpImageBufferHeapYuv != nullptr) {
      pInpImageStreamBufferYuv->unlock(getNodeName(),
                                       pInpImageBufferHeapYuv.get());
    }
    //
    rStreamBufferSet.markUserStatus(
        streamId, getNodeId(),
        NSCam::v3::IUsersManager::UserStatus::USED |
            NSCam::v3::IUsersManager::UserStatus::RELEASE);
  } else {
    rStreamBufferSet.markUserStatus(
        streamId, getNodeId(), NSCam::v3::IUsersManager::UserStatus::RELEASE);
  }
}

////////////////////////////////////////////////////////////////////////////
//  Apply buffers to release.
////////////////////////////////////////////////////////////////////////////
rStreamBufferSet.applyRelease(getNodeId());

////////////////////////////////////////////////////////////////////////////
//  Dispatch
////////////////////////////////////////////////////////////////////////////
onDispatchFrame(pFrame);
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<NSCam::v3::FdNode> NSCam::v3::FdNode::createInstance() {
  return std::make_shared<FdNodeImp>();
}
