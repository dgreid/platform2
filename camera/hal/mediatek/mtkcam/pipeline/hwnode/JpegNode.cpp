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

#define LOG_TAG "MtkCam/JpegNode"
//
#include "BaseNode.h"
#include <list>
#include <mtkcam/pipeline/hwnode/JpegNode.h>
#include <mtkcam/utils/std/Log.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <vector>
#include <mtkcam/utils/metastore/IMetadataProvider.h>
#include <mtkcam/utils/metadata/client/mtk_metadata_tag.h>
#include <mtkcam/utils/metadata/hal/mtk_platform_metadata_tag.h>
#include <mtkcam/custom/ExifFactory.h>
#include <mtkcam/utils/exif/DebugExifUtils.h>
#include <mtkcam/utils/exif/IBaseCamExif.h>
#include <mtkcam/utils/exif/StdExif.h>
#include <mtkcam/utils/std/Sync.h>
#include <mtkcam/utils/std/Trace.h>
#include <mtkcam/utils/std/Misc.h>
#include <mtkcam/utils/std/Mojo.h>
#include <mtkcam/utils/std/Format.h>
#include <mtkcam/def/common.h>
#include <mtkcam/utils/std/common.h>

#include <map>
#include <memory>
#include <mtkcam/utils/imgbuf/ImageBufferHeap.h>
#include <thread>
#include <unordered_map>
#include <utility>
#include <mtkcam/utils/TuningUtils/FileDumpNamingRule.h>

#include <sys/syscall.h>

using NSCam::DebugExifUtils;
using NSCam::IImageBuffer;
using NSCam::IImageBufferHeap;
using NSCam::IMetadata;
using NSCam::IMetadataProvider;
using NSCam::PortBufInfo_v1;
using NSCam::Type2Type;

/******************************************************************************
 *
 ******************************************************************************/
#define JPEGTHREAD_NAME ("Cam@Jpeg")
#define THUMBTHREAD_NAME ("Cam@JpegThumb")

#define JPEGTHREAD_POLICY (SCHED_OTHER)
#define JPEGTHREAD_PRIORITY (0)
//
#undef ENABLE_PRERELEASE
#define ENABLE_DEBUG_INFO (1)
#define ENABLE_PRERELEASE (0)
#define DBG_BOUND_WIDTH (320)
#define DBG_BOUND_HEIGH (240)

/******************************************************************************
 *
 ******************************************************************************/

#define CHECK_ERROR(_err_)                          \
  do {                                              \
    MERROR const err = (_err_);                     \
    if (err != OK) {                                \
      MY_LOGE("err:%d(%s)", err, ::strerror(-err)); \
      return err;                                   \
    }                                               \
  } while (0)

#define EXIFAPP1_MAX_SIZE 65535  // 64K exif appn max data size
#define EXIFHEADER_ALIGN 128

static char filename[256] = {0};  // for file dump naming

#define RESOLUTION_14MP_WIDTH 4352
#define RESOLUTION_14MP_HEIGHT 3264
#define STDCOPY(dst, src, size) std::copy((src), ((src) + (size)), (dst))

/******************************************************************************
 *
 ******************************************************************************/
static inline MBOOL isStream(
    std::shared_ptr<NSCam::v3::IStreamInfo> pStreamInfo,
    NSCam::v3::StreamId_T streamId) {
  return pStreamInfo && pStreamInfo->getStreamId() == streamId;
}

/******************************************************************************
 *
 ******************************************************************************/
template <typename T>
inline MBOOL tryGetMetadata(IMetadata const* const pMetadata,
                            MUINT32 const tag,
                            T* rVal) {
  if (pMetadata == nullptr) {
    MY_LOGE("pMetadata == NULL");
    return MFALSE;
  }

  IMetadata::IEntry entry = pMetadata->entryFor(tag);
  if (!entry.isEmpty()) {
    *rVal = entry.itemAt(0, Type2Type<T>());
    return MTRUE;
  }
  return MFALSE;
}

/******************************************************************************
 *
 ******************************************************************************/
template <typename T>
inline MVOID updateEntry(IMetadata* pMetadata,
                         MUINT32 const tag,
                         T const& val) {
  if (pMetadata == nullptr) {
    MY_LOGE("pMetadata == NULL");
    return;
  }

  IMetadata::IEntry entry(tag);
  entry.push_back(val, Type2Type<T>());
  pMetadata->update(tag, entry);
}

/******************************************************************************
 *
 ******************************************************************************/
static MRect calCropAspect(MSize const& srcSize, MSize const& dstSize) {
  MRect crop;
  MUINT32 val0 = srcSize.w * dstSize.h;
  MUINT32 val1 = srcSize.h * dstSize.w;
  if (val0 > val1) {
    crop.s.w = ALIGNX(val1 / dstSize.h, 2);
    crop.s.h = srcSize.h;
    crop.p.x = (srcSize.w - crop.s.w) / 2;
    crop.p.y = 0;
  } else if (val0 < val1) {
    crop.s.w = srcSize.w;
    crop.s.h = ALIGNX(val0 / dstSize.w, 2);
    crop.p.x = 0;
    crop.p.y = (srcSize.h - crop.s.h) / 2;
  } else {
    crop = MRect(MPoint(0, 0), srcSize);
  }
  return crop;
}

/******************************************************************************
 *
 ******************************************************************************/
class JpegNodeImp : public NSCam::v3::BaseNode, public NSCam::v3::JpegNode {
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  Implementations.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////                                            Definitions.
  typedef std::shared_ptr<NSCam::v3::IPipelineFrame> QueNode_T;
  typedef std::list<QueNode_T> Que_T;

 protected:
  class EncodeThread {
   public:
    explicit EncodeThread(JpegNodeImp* pNodeImp) : mpNodeImp(pNodeImp) {}

    virtual ~EncodeThread() {}

   public:
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    //  Thread Interface.
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

   public:
    virtual void requestExit();

    virtual status_t readyToRun();

    virtual status_t run();

    virtual status_t join();

   private:
    virtual bool threadLoop();
    virtual bool _threadLoop();

   private:
    std::thread mThread;

    JpegNodeImp* mpNodeImp;
  };

  class EncodeThumbThread {
   public:
    explicit EncodeThumbThread(JpegNodeImp* pNodeImp) : mpNodeImp(pNodeImp) {}

    virtual ~EncodeThumbThread() {}

   public:
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    //  Thread Interface.
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

   public:
    virtual void requestExit();

    virtual status_t readyToRun();

    virtual status_t run();

   private:
    virtual bool threadLoop();

   private:
    std::thread mThread;

    JpegNodeImp* mpNodeImp;
  };

  //
 public:  ////                    Operations.
  JpegNodeImp();

  ~JpegNodeImp();

  virtual MERROR config(ConfigParams const& rParams);

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  IPipelineNode Interface.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////                    Operations.
  virtual MERROR init(InitParams const& rParams);

  virtual MERROR uninit();

  virtual MERROR flush();

  virtual MERROR flush(
      std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame) {
    return BaseNode::flush(pFrame);
  }

  virtual MERROR queue(std::shared_ptr<NSCam::v3::IPipelineFrame> pFrame);

 protected:               ////                    Operations.
  MERROR onDequeRequest(  // TODO(MTK): check frameNo
      std::shared_ptr<NSCam::v3::IPipelineFrame>* rpFrame);
  MVOID onProcessFrame(
      std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame);
  MERROR verifyConfigParams(ConfigParams const& rParams) const;

  MVOID waitForRequestDrained();

  MERROR getImageBufferAndLock(
      std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame,
      NSCam::v3::StreamId_T const streamId,
      std::shared_ptr<NSCam::v3::IImageStreamBuffer>* rpStreamBuffer,
      std::shared_ptr<IImageBuffer>* rpImageBuffer);

  MERROR getMetadataAndLock(
      std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame,
      NSCam::v3::StreamId_T const streamId,
      std::shared_ptr<NSCam::v3::IMetaStreamBuffer>* rpStreamBuffer,
      IMetadata** rpOutMetadataResult);

  MVOID returnMetadataAndUnlock(
      std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame,
      NSCam::v3::StreamId_T const streamId,
      std::shared_ptr<NSCam::v3::IMetaStreamBuffer> rpStreamBuffer,
      IMetadata* rpOutMetadataResult,
      MBOOL success = MTRUE);

  MBOOL isInMetaStream(NSCam::v3::StreamId_T const streamId) const;

  MBOOL isInImageStream(NSCam::v3::StreamId_T const streamId) const;

  bool convertToP411(std::shared_ptr<IImageBuffer> srcBuf, void* dst);
  // P411's Y, U, V are seperated. But the YUY2's Y, U and V are interleaved.
  void YUY2ToP411(int width, int height, int stride, void* src, void* dst);
  // P411's Y, U, V are separated. But the NV12's U and V are interleaved.
  void NV12ToP411Separate(
      int width, int height, int stride, void* srcY, void* srcUV, void* dst);
  // P411's Y, U, V are separated. But the NV21's U and V are interleaved.
  void NV21ToP411Separate(
      int width, int height, int stride, void* srcY, void* srcUV, void* dst);

 private:  ////                    to sync main yuv & thumbnail yuv
  struct jpeg_params {
    // gps related
    IMetadata::IEntry gpsCoordinates;
    IMetadata::IEntry gpsProcessingMethod;
    IMetadata::IEntry gpsTimestamp;
    //
    MINT32 orientation;
    MUINT8 quality;
    MUINT8 quality_thumbnail;
    MSize size_thumbnail;
    //
    MRect cropRegion;
    //
    MINT32 flipMode;
    //
    jpeg_params()
        : gpsCoordinates(),
          gpsProcessingMethod(),
          gpsTimestamp()
          //
          ,
          orientation(0),
          quality(90),
          quality_thumbnail(90),
          size_thumbnail(0, 0)
          //
          ,
          cropRegion(),
          flipMode(0) {}
  };

  class encode_frame {
   public:
    std::shared_ptr<NSCam::v3::IPipelineFrame> const mpFrame;
    MBOOL mbHasThumbnail;
    MBOOL mbSuccess;
    MBOOL mbBufValid;
    MINT8 miJpegEncType;
    //
    jpeg_params mParams;
    //
    std::shared_ptr<IImageBuffer> mpJpeg_Main;
    std::shared_ptr<IImageBuffer> mpJpeg_Thumbnail;
    //
    StdExif exif;
    std::shared_ptr<NSCam::v3::IImageStreamBuffer> mpOutImgStreamBuffer;
    std::shared_ptr<IImageBufferHeap> mpOutImgBufferHeap;
    std::shared_ptr<IImageBufferHeap> mpExifBufferHeap;

    size_t thumbnailMaxSize;
    size_t exifSize;
    //
    encode_frame(std::shared_ptr<NSCam::v3::IPipelineFrame> const pFrame,
                 MBOOL const hasThumbnail)
        : mpFrame(pFrame),
          mbHasThumbnail(hasThumbnail),
          mbSuccess(MTRUE),
          mbBufValid(MTRUE),
          miJpegEncType(-1),
          mpJpeg_Main(nullptr),
          mpJpeg_Thumbnail(nullptr),
          mpOutImgStreamBuffer(nullptr),
          mpOutImgBufferHeap(NULL),
          mpExifBufferHeap(NULL),
          thumbnailMaxSize(0),
          exifSize(0) {}
  };
  MVOID encodeThumbnail(std::shared_ptr<encode_frame>* pEncodeFrame);

  MVOID finalizeEncodeFrame(std::shared_ptr<encode_frame>* rpEncodeFrame);

  MVOID getJpegParams(IMetadata* pMetadata_request, jpeg_params* rParams) const;

  MERROR getImageBufferAndLock(
      std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame,
      NSCam::v3::StreamId_T const streamId,
      std::shared_ptr<NSCam::v3::IImageStreamBuffer>* rpStreamBuffer,
      std::shared_ptr<IImageBuffer>* rpImageBuffer,
      std::shared_ptr<encode_frame>* rpEncodeFrame,
      std::shared_ptr<IImageBufferHeap>* rpImageBufferHeap);

  MERROR getThumbImageBufferAndLock(
      std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame,
      NSCam::v3::StreamId_T const streamId,
      std::shared_ptr<encode_frame> const& rpEncodeFrame,
      std::shared_ptr<IImageBufferHeap> const& rpImageBufferHeap,
      std::shared_ptr<IImageBuffer>* rpImageBuffer /*out*/);

  MVOID updateMetadata(jpeg_params* rParams, IMetadata* pMetadata_result) const;

  MERROR makeExifHeader(std::shared_ptr<encode_frame> rpEncodeFrame,
                        MINT8* const pOutExif,
                        // [IN/OUT] in: exif buf size, out: exif header size
                        size_t* rOutExifSize);

  MVOID updateStdExifParam(MBOOL const& rNeedExifRotate,
                           MSize const& rSize,
                           IMetadata* const rpAppMeta,
                           IMetadata* const rpHalMeta,
                           jpeg_params const& rParams,
                           ExifParams* rStdParams) const;

  MVOID updateStdExifParam_3A(IMetadata const& rMeta,
                              IMetadata const& rAppMeta,
                              ExifParams* rStdParams) const;

  MVOID updateStdExifParam_gps(IMetadata::IEntry const& rGpsCoordinates,
                               IMetadata::IEntry const& rGpsProcessingMethod,
                               IMetadata::IEntry const& rGpsTimestamp,
                               ExifParams* rStdParams) const;

  MVOID updateDebugInfoToExif(IMetadata* const rpHalMeta, StdExif* exif) const;

  MUINT32 calcZoomRatio(MRect const& cropRegion, MSize const& rSize) const;

  MERROR errorHandle(std::shared_ptr<encode_frame> const& rpEncodeFrame);

  MVOID unlockImage(
      std::shared_ptr<NSCam::v3::IImageStreamBuffer>* rpStreamBuffer,
      std::shared_ptr<IImageBuffer>* rpImageBuffer,
      std::shared_ptr<IImageBuffer>* rpImageBuffer1);
  MERROR getStreamInfo(
      NSCam::v3::StreamId_T const streamId,
      std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame,
      std::shared_ptr<NSCam::v3::IImageStreamInfo>* rpStreamInfo);

  MVOID dumpYUVBuffer(MUINT32 const frameNo,
                      std::shared_ptr<IImageBuffer>* rpImageBuffer,
                      MUINT32 const idx);

 protected:  ////                    hw related
  class my_encode_params {
   public:
    // buffer
    std::shared_ptr<IImageBuffer> pSrc;
    std::shared_ptr<IImageBuffer> pDst;

    // settings
    MUINT32 transform;
    MRect crop;
    MUINT32 isSOI;
    MUINT32 quality;
    MUINT32 codecType;
  };

 protected:
  MERROR threadSetting();

 protected:  ////                    Data Members. (Config)
  mutable pthread_rwlock_t mConfigRWLock;
  // meta
  std::shared_ptr<NSCam::v3::IMetaStreamInfo> mpInAppMeta;
  std::shared_ptr<NSCam::v3::IMetaStreamInfo> mpInHalMeta_capture;
  std::shared_ptr<NSCam::v3::IMetaStreamInfo> mpInHalMeta_streaming;
  std::shared_ptr<NSCam::v3::IMetaStreamInfo> mpInHalMeta;
  std::shared_ptr<NSCam::v3::IMetaStreamInfo> mpOutMetaStreamInfo_Result;
  std::unordered_map<int, std::shared_ptr<NSCam::v3::IMetaStreamInfo>>
      mHalMetaMap;

  // image
  std::shared_ptr<NSCam::v3::IImageStreamInfo> mpInYuv_main;
  std::shared_ptr<NSCam::v3::IImageStreamInfo> mpInYuv_thumbnail;
  std::shared_ptr<NSCam::v3::IImageStreamInfo> mpOutJpeg;
  std::shared_ptr<encode_frame> mpEncodeFrame;

 protected:  ////                    Data Members. (Request Queue)
  mutable std::mutex mRequestQueueLock;
  std::condition_variable mRequestQueueCond;
  Que_T mRequestQueue;
  MBOOL mbRequestDrained;
  std::condition_variable mbRequestDrainedCond;
  MBOOL mbRequestExit;

 private:  ////                     Threads
  std::shared_ptr<EncodeThread> mpEncodeThread;
  std::shared_ptr<EncodeThumbThread> mpEncodeThumbThread;

 private:
  mutable std::mutex mEncodeLock;
  std::condition_variable mEncodeCond;
  mutable std::mutex mJpegCompressorLock;
  std::shared_ptr<encode_frame> mpCurEncFrame;
  MUINT32 muDumpBuffer;
  MINT32 mFlip;

 private:           // static infos
  MUINT8 muFacing;  // ref: MTK_LENS_FACING_
  MRect mActiveArray;
  MBOOL mJpegRotationEnable;
  MINT32 mLogLevel;
  MBOOL mThumbDoneFlag;
  MBOOL mDbgInfoEnable;
  MINT32 mUniqueKey;
  MINT32 mFrameNumber;
  MINT32 mRequestNumber;
#if ENABLE_PRERELEASE
  std::shared_ptr<ITimeline> mpTimeline;
  MUINT16 mTimelineCounter;
#endif

 private:
  // cros::JpegCompressor needs YU12 format
  // and the ISP doesn't output YU12 directly.
  // so a temporary intermediate buffer is needed
  std::unique_ptr<cros::JpegCompressor> mJpegCompressor;
};

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<NSCam::v3::JpegNode> NSCam::v3::JpegNode::createInstance() {
  return std::make_shared<JpegNodeImp>();
}

/******************************************************************************
 *
 ******************************************************************************/
JpegNodeImp::JpegNodeImp()
    : BaseNode(),
      JpegNode()
      //
      ,
      mpInHalMeta(nullptr)
      //
      ,
      mbRequestDrained(MFALSE),
      mbRequestExit(MFALSE)
      //
      ,
      mpEncodeThread(NULL)
      //
      //, mbIsEncoding(MFALSE)
      //, muCurFrameNo(0)
      //
      ,
      mpCurEncFrame(nullptr)
      //
      ,
      muFacing(0),
      mJpegRotationEnable(MFALSE),
      mThumbDoneFlag(MFALSE),
      mUniqueKey(-1),
      mFrameNumber(-1),
      mRequestNumber(-1)
#if ENABLE_PRERELEASE
      ,
      mpTimeline(ITimeline::create("Timeline::Jpeg")),
      mTimelineCounter(),
      mpEncodeFrame(nullptr)
#endif
      ,
      mJpegCompressor(cros::JpegCompressor::GetInstance(
          NSCam::Utils::getMojoManagerToken())) {
  pthread_rwlock_init(&mConfigRWLock, NULL);
  mNodeName = "JpegNode";  // default name
  MINT32 enable = ::property_get_int32("vendor.jpeg.rotation.enable", 1);
  mJpegRotationEnable = (enable & 0x1) ? MTRUE : MFALSE;
  MY_LOGD_IF(mJpegRotationEnable, "Jpeg Rotation enable");

  mLogLevel = ::property_get_int32("vendor.debug.camera.log", 0);
  if (mLogLevel == 0) {
    mLogLevel = ::property_get_int32("vendor.debug.camera.log.JpegNode", 0);
  }
  MINT32 forceDbg;
#if (MTKCAM_HW_NODE_LOG_LEVEL_DEFAULT > 3)
  forceDbg = 1;  // for ENG build
#elif MTKCAM_HW_NODE_LOG_LEVEL_DEFAULT > 2
  forceDbg = 1;  // for USERDEBUG build
#else
  forceDbg = 0;  // for USER build
#endif
  mDbgInfoEnable =
      ::property_get_int32("vendor.debug.camera.dbginfo", forceDbg);
  muDumpBuffer = ::property_get_int32("vendor.debug.camera.dump.JpegNode", 0);
  mFlip = ::property_get_int32("vendor.debug.camera.Jpeg.flip", 0);
}

/******************************************************************************
 *
 ******************************************************************************/
JpegNodeImp::~JpegNodeImp() {
  MY_LOGI("");
  pthread_rwlock_destroy(&mConfigRWLock);
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::init(InitParams const& rParams) {
  FUNC_START;
  //
  mOpenId = rParams.openId;
  mNodeId = rParams.nodeId;
  mNodeName = rParams.nodeName;
  //
  MY_LOGD("OpenId %d, nodeId %#" PRIxPTR ", name %s", getOpenId(), getNodeId(),
          getNodeName());
  //
  mpEncodeThread = std::make_shared<EncodeThread>(this);
  if (mpEncodeThread->run() != OK) {
    return UNKNOWN_ERROR;
  }
  //
  {
    std::shared_ptr<IMetadataProvider> pMetadataProvider =
        NSCam::NSMetadataProviderManager::valueFor(getOpenId());
    if (!pMetadataProvider) {
      MY_LOGE(" ! pMetadataProvider.get() ");
      return DEAD_OBJECT;
    }

    IMetadata static_meta = pMetadataProvider->getMtkStaticCharacteristics();
    if (!tryGetMetadata<MRect>(
            &static_meta, MTK_SENSOR_INFO_ACTIVE_ARRAY_REGION, &mActiveArray)) {
      MY_LOGE("no static info: MTK_SENSOR_INFO_ACTIVE_ARRAY_REGION");
      return UNKNOWN_ERROR;
    }

    if (!tryGetMetadata<MUINT8>(&static_meta, MTK_SENSOR_INFO_FACING,
                                &muFacing)) {
      MY_LOGE("no static info: MTK_SENSOR_INFO_FACING");
      return UNKNOWN_ERROR;
    }

    MY_LOGD_IF(1, "active array(%d, %d, %dx%d), facing %d", mActiveArray.p.x,
               mActiveArray.p.y, mActiveArray.s.w, mActiveArray.s.h, muFacing);
  }
  //
  FUNC_END;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::config(ConfigParams const& rParams) {
  FUNC_START;
  CHECK_ERROR(verifyConfigParams(rParams));
  //
  flush();
  //
  {
    pthread_rwlock_wrlock(&mConfigRWLock);
    // meta
    mpInAppMeta = rParams.pInAppMeta;
    mpInHalMeta_capture = rParams.pInHalMeta_capture;
    mpInHalMeta_streaming = rParams.pInHalMeta_streaming;
    if (mpInHalMeta_capture) {
      mHalMetaMap.emplace(std::make_pair(mpInHalMeta_capture->getStreamId(),
                                         mpInHalMeta_capture));
      MY_LOGD("debug capture InHalMeta streamId : %#" PRIx64 "",
              mpInHalMeta_capture->getStreamId());
    }
    if (mpInHalMeta_streaming) {
      mHalMetaMap.emplace(std::make_pair(mpInHalMeta_streaming->getStreamId(),
                                         mpInHalMeta_streaming));
      MY_LOGD("debug streaming InHalMeta streamId : %#" PRIx64 "",
              mpInHalMeta_streaming->getStreamId());
    }
    mpOutMetaStreamInfo_Result = rParams.pOutAppMeta;
    MY_LOGD("debug InOutMeta streamId : %#" PRIx64 "",
            mpOutMetaStreamInfo_Result->getStreamId());
    // image
    mpInYuv_main = rParams.pInYuv_Main;
    mpInYuv_thumbnail = rParams.pInYuv_Thumbnail;
    mpOutJpeg = rParams.pOutJpeg;
    //
    pthread_rwlock_unlock(&mConfigRWLock);
  }
  if (mpInYuv_main != nullptr) {
    MY_LOGD("mpInYuv_main:%dx%d", mpInYuv_main->getImgSize().w,
            mpInYuv_main->getImgSize().h);
  }
  if (mpInYuv_thumbnail != nullptr) {
    MY_LOGD("mpInYuv_thumbnail:%dx%d", mpInYuv_thumbnail->getImgSize().w,
            mpInYuv_thumbnail->getImgSize().h);
  }
  //
  FUNC_END;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::uninit() {
  FUNC_START;
  //
  if (OK != flush()) {
    MY_LOGE("flush failed");
  }
  //
  // exit threads
  mpEncodeThread->requestExit();
  // join
  mpEncodeThread->join();
  //
  mpEncodeThread = nullptr;
  mpEncodeThumbThread = nullptr;
  //
  FUNC_END;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::flush() {
  FUNC_START;
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
  FUNC_END;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::queue(std::shared_ptr<NSCam::v3::IPipelineFrame> pFrame) {
  FUNC_START;
  //
  if (!pFrame) {
    MY_LOGE("Null frame");
    return BAD_VALUE;
  }

  MY_LOGD("FrameNo : %d, RequestNo : %d", pFrame->getFrameNo(),
          pFrame->getRequestNo());

  std::lock_guard<std::mutex> _l(mRequestQueueLock);
  // TODO(MTK): handle main & thumnail yuvs are not queued in the same time
  Que_T::iterator it = mRequestQueue.end();
  for (; it != mRequestQueue.begin();) {
    --it;
    if (0 <= (MINT32)(pFrame->getFrameNo() - (*it)->getFrameNo())) {
      ++it;  // insert(): insert before the current node
      break;
    }
  }
  mRequestQueue.insert(it, pFrame);
  mRequestQueueCond.notify_one();
  //
  FUNC_END;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::onDequeRequest(
    std::shared_ptr<NSCam::v3::IPipelineFrame>* rpFrame) {
  FUNC_START;
  std::unique_lock<std::mutex> _l(mRequestQueueLock);

  //  Wait until the queue is not empty or not going exit
  while (mRequestQueue.empty() && !mbRequestExit) {
    // set dained flag
    mbRequestDrained = MTRUE;
    mbRequestDrainedCond.notify_all();
    //
    MY_LOGD_IF(mLogLevel, "mRequestQueue.size:%zu wait+", mRequestQueue.size());
    mRequestQueueCond.wait(_l);
    MY_LOGD_IF(mLogLevel, "mRequestQueue.size:%zu wait-", mRequestQueue.size());
  }

  if (mbRequestExit) {
    MY_LOGW_IF(!mRequestQueue.empty(), "[flush] mRequestQueue.size:%zu",
               mRequestQueue.size());
    return DEAD_OBJECT;
  }

  //  Here the queue is not empty, take the first request from the queue.
  mbRequestDrained = MFALSE;
  *rpFrame = *mRequestQueue.begin();
  mRequestQueue.erase(mRequestQueue.begin());
  //
  FUNC_END;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::encodeThumbnail(std::shared_ptr<encode_frame>* pEncodeFrame) {
  if (*pEncodeFrame == nullptr) {
    MY_LOGE("thumb encode frame is null");
    return;
  }
  std::shared_ptr<NSCam::v3::IPipelineFrame> const pFrame =
      (*pEncodeFrame)->mpFrame;
  if ((*pEncodeFrame)->mpJpeg_Thumbnail == nullptr) {
    MY_LOGW("thumb imagebuffer is null");
    return;
  }

  // to encode thumbnail
  // try get yuv for thumb jpeg
  NSCam::v3::IStreamBufferSet& streamBufferSet = pFrame->getStreamBufferSet();
  NSCam::v3::StreamId_T const stream_in = mpInYuv_thumbnail->getStreamId();

  std::shared_ptr<NSCam::v3::IImageStreamBuffer> pInImageStreamBuffer = nullptr;
  std::shared_ptr<IImageBuffer> pInImageBuffer = nullptr;

  MERROR const err = getImageBufferAndLock(
      pFrame, stream_in, &pInImageStreamBuffer, &pInImageBuffer);
  if (err != OK) {
    MY_LOGE("getImageBufferAndLock(InImageStreamBuffer) err = %d", err);
    (*pEncodeFrame)->mbBufValid = MFALSE;
    return;
  }
  if (mLogLevel >= 2) {
    dumpYUVBuffer(pFrame->getFrameNo(), &pInImageBuffer, 1);
  }
  //
  MSize thumbsize = (*pEncodeFrame)->mParams.size_thumbnail;
  // do encode
  {
    my_encode_params params;
    params.pSrc = pInImageBuffer;
    params.pDst = (*pEncodeFrame)->mpJpeg_Thumbnail;
    // MUINT32 transform =
    // pInImageStreamBuffer->getStreamInfo()->getTransform(); //TODO
    if (mpEncodeFrame->mParams.flipMode || mFlip) {
      if ((*pEncodeFrame)->mParams.orientation == 90) {
        params.transform = eTransform_ROT_90 | NSCam::eTransform_FLIP_V;
        thumbsize = MSize(thumbsize.h, thumbsize.w);
      } else if ((*pEncodeFrame)->mParams.orientation == 180) {
        params.transform = NSCam::eTransform_FLIP_V;
      } else if ((*pEncodeFrame)->mParams.orientation == 270) {
        params.transform = eTransform_ROT_90 | NSCam::eTransform_FLIP_H;
        thumbsize = MSize(thumbsize.h, thumbsize.w);
      } else {
        params.transform = NSCam::eTransform_FLIP_H;
      }
    } else {
      if ((*pEncodeFrame)->mParams.orientation == 90) {
        params.transform = NSCam::eTransform_ROT_90;
        thumbsize = MSize(thumbsize.h, thumbsize.w);
      } else if ((*pEncodeFrame)->mParams.orientation == 180) {
        params.transform = NSCam::eTransform_ROT_180;
      } else if ((*pEncodeFrame)->mParams.orientation == 270) {
        params.transform = NSCam::eTransform_ROT_270;
        thumbsize = MSize(thumbsize.h, thumbsize.w);
      } else {
        params.transform = 0;
      }
    }

    params.crop = calCropAspect(pInImageBuffer->getImgSize(), thumbsize);
    params.isSOI = 1;
    params.quality = (*pEncodeFrame)->mParams.quality_thumbnail;

    size_t bitstream_thumbsize = 0;
    MINT32 quality = params.quality;
    int thumbsrc_size =
        ((params.pSrc->getImgSize().w) * (params.pSrc->getImgSize().h) * 3) / 2;
    char* thumbsrc_buf = new char[thumbsrc_size];
    if (!convertToP411(params.pSrc, reinterpret_cast<void*>(thumbsrc_buf))) {
      bitstream_thumbsize = 0;
      params.pDst->setBitstreamSize(bitstream_thumbsize);
    } else {
      do {
        MY_LOGI("Encoding thumbnail with quality %d", params.quality);
        bool ret = false;
        {
          std::lock_guard<std::mutex> _l(mJpegCompressorLock);
          ret = mJpegCompressor->GenerateThumbnail(
              thumbsrc_buf, params.pSrc->getImgSize().w,
              params.pSrc->getImgSize().h, params.pSrc->getImgSize().w,
              params.pSrc->getImgSize().h, quality,
              mpEncodeFrame->thumbnailMaxSize,
              reinterpret_cast<void*>(params.pDst->getBufVA(0)),
              &bitstream_thumbsize);
        }
        if (ret != true) {
          MY_LOGE("thumb encode fail src %p, fmt 0x%x, dst %zx, fmt 0x%x",
                  params.pSrc.get(), params.pSrc->getImgFormat(),
                  params.pDst->getBufVA(0), params.pDst->getImgFormat());
          (*pEncodeFrame)->mbSuccess = MFALSE;
        } else {
          params.pDst->setBitstreamSize(bitstream_thumbsize);
          if (mpEncodeFrame->thumbnailMaxSize <
              params.pDst->getBitstreamSize()) {
            if (params.pDst->getBitstreamSize() >
                (mpEncodeFrame->thumbnailMaxSize +
                 mpEncodeFrame->exif.getDbgExifSize())) {
              MY_LOGE("Thumbnail over encode! encode bitstreamSize");
            } else {
              MY_LOGW(
                  "Thumbnail bitStream size is too big, scale down quality and "
                  "re-encode again!");
              quality -= 10;
              if (quality > 0) {
                params.quality = quality;
              }
              continue;
            }
          }
        }
        (*pEncodeFrame)->mbSuccess = MTRUE;
        break;
      } while (quality > 0);
    }
    if (quality <= 0 || !(*pEncodeFrame)->mbSuccess) {
      MY_LOGE("Thumbnail encode fail!");
    }
    delete[] thumbsrc_buf;
  }
  pInImageBuffer->unlockBuf(getNodeName());
  pInImageStreamBuffer->unlock(getNodeName(),
                               pInImageBuffer->getImageBufferHeap().get());
  //
  streamBufferSet.markUserStatus(
      pInImageStreamBuffer->getStreamInfo()->getStreamId(), getNodeId(),
      NSCam::v3::IUsersManager::UserStatus::USED |
          NSCam::v3::IUsersManager::UserStatus::RELEASE);
}
/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::onProcessFrame(
    std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame) {
  FUNC_START;
  //
  NSCam::v3::IPipelineFrame::InfoIOMapSet IOMapSet;
  if (OK != pFrame->queryInfoIOMapSet(getNodeId(), &IOMapSet) ||
      IOMapSet.mImageInfoIOMapSet.size() != 1 ||
      IOMapSet.mMetaInfoIOMapSet.size() != 1) {
    MY_LOGE("queryInfoIOMap failed, IOMap img/meta: %zu/%zu",
            IOMapSet.mImageInfoIOMapSet.size(),
            IOMapSet.mMetaInfoIOMapSet.size());
    return;
  }

  {
    NSCam::v3::IPipelineFrame::MetaInfoIOMap const& metaIOMap =
        IOMapSet.mMetaInfoIOMapSet[0];
    for (auto& i : metaIOMap.vIn) {
      NSCam::v3::StreamId_T const streamId = i.first;
      auto search = mHalMetaMap.find(static_cast<int>(streamId));
      if (search != mHalMetaMap.end()) {
        MY_LOGD("StreamId : %#" PRIx64 "", streamId);
        mpInHalMeta = search->second;
        break;
      }
    }
    if (mpInHalMeta == nullptr) {
      MY_LOGE("PipelineContext doesn't setup input hal meta");
      return;
    }
  }

  {
    MBOOL useThumbnail = MFALSE;

    // query if use thumbnail
    NSCam::v3::IPipelineFrame::ImageInfoIOMap const& imageIOMap =
        IOMapSet.mImageInfoIOMapSet[0];
    for (auto& i : imageIOMap.vIn) {
      NSCam::v3::StreamId_T const streamId = i.first;
      if (isStream(mpInYuv_thumbnail, streamId)) {
        useThumbnail = MTRUE;
        MY_LOGD("need Thumbnail!");
        break;
      }
    }

    // new frame
    mpEncodeFrame = nullptr;
    mpEncodeFrame = std::make_shared<encode_frame>(pFrame, useThumbnail);

    if (!mpEncodeFrame.get()) {
      MY_LOGE("mpEncodeFrame is NULL");
      return;
    }
    // get jpeg params
    {
      IMetadata* pInMeta_Request = nullptr;
      std::shared_ptr<NSCam::v3::IMetaStreamBuffer> pInMetaStream_Request =
          nullptr;

      MERROR err = getMetadataAndLock(pFrame, mpInAppMeta->getStreamId(),
                                      &pInMetaStream_Request, &pInMeta_Request);

      if (err != OK) {
        MY_LOGE("getMetadataAndLock err = %d", err);
        errorHandle(mpEncodeFrame);
        mpEncodeFrame = nullptr;
        return;
      }

      getJpegParams(pInMeta_Request, &mpEncodeFrame->mParams);

      // get HAL meta
      IMetadata* pInMeta_Hal = nullptr;
      std::shared_ptr<NSCam::v3::IMetaStreamBuffer> pInMetaStream_Hal = nullptr;

      err = getMetadataAndLock(pFrame, mpInHalMeta->getStreamId(),
                               &pInMetaStream_Hal, &pInMeta_Hal);
      //
      if (err != OK) {
        MY_LOGE("getMetadataAndLock(pInMetaStream_Hal) err = %d", err);
        errorHandle(mpEncodeFrame);
        mpEncodeFrame = nullptr;
        return;
      }
      {
        MUINT8 encodeType;
        if (tryGetMetadata<MUINT8>(pInMeta_Hal, MTK_JPG_ENCODE_TYPE,
                                   &encodeType)) {
          mpEncodeFrame->miJpegEncType = encodeType;
          MY_LOGD("Assign encode type manually.(%d)",
                  mpEncodeFrame->miJpegEncType);
        }
      }
      // determine exif need rotate
      std::shared_ptr<NSCam::v3::IImageStreamInfo> pYUVStreamInfo = nullptr;
      if (OK !=
          getStreamInfo(mpInYuv_main->getStreamId(), pFrame, &pYUVStreamInfo)) {
        errorHandle(mpEncodeFrame);
        mpEncodeFrame = nullptr;
        MY_LOGE("getStreamInfo fail");
        return;
      }
      MUINT32 transform = pYUVStreamInfo->getTransform();
      MBOOL needExifRotate = MTRUE;

      if ((mpEncodeFrame->mParams.orientation == 90 &&
           transform & NSCam::eTransform_ROT_90) ||
          (mpEncodeFrame->mParams.orientation == 270 &&
           transform & NSCam::eTransform_ROT_270) ||
          (mpEncodeFrame->mParams.orientation == 180 &&
           transform & NSCam::eTransform_ROT_180)) {
        needExifRotate = MFALSE;
      }

      MSize imageSize =
          MSize(pYUVStreamInfo->getImgSize().w, pYUVStreamInfo->getImgSize().h);
      ExifParams stdParams;
      //
      // update standard exif params
      updateStdExifParam(needExifRotate, imageSize, pInMeta_Request,
                         pInMeta_Hal, mpEncodeFrame->mParams, &stdParams);

      // check thumbnail size
      if (!mpEncodeFrame->mParams.size_thumbnail.w ||
          !mpEncodeFrame->mParams.size_thumbnail.h) {
        MY_LOGD(
            "App meta is not set thumbnail size, check request streamInfo "
            "size");
        std::shared_ptr<NSCam::v3::IImageStreamInfo> pThumbnailStreamInfo =
            nullptr;
        if (OK != getStreamInfo(mpInYuv_thumbnail->getStreamId(), pFrame,
                                &pThumbnailStreamInfo)) {
          MY_LOGI("getThumbnailStreamInfo fail, hasThumbnail :%d",
                  mpEncodeFrame->mbHasThumbnail);
          mpEncodeFrame->mbHasThumbnail = MFALSE;
        } else {
          if (pThumbnailStreamInfo->getImgSize().w &&
              pThumbnailStreamInfo->getImgSize().h) {
            mpEncodeFrame->mParams.size_thumbnail =
                pThumbnailStreamInfo->getImgSize();
            if (mJpegRotationEnable) {
              if (mpEncodeFrame->mParams.orientation == 90 ||
                  mpEncodeFrame->mParams.orientation == 270) {
                MINT32 tmp = mpEncodeFrame->mParams.size_thumbnail.w;
                mpEncodeFrame->mParams.size_thumbnail.w =
                    mpEncodeFrame->mParams.size_thumbnail.h;
                mpEncodeFrame->mParams.size_thumbnail.h = tmp;
              }
              MY_LOGD_IF(0, "@@getJpegParams thumb size(w,h)=(%dx%d)",
                         mpEncodeFrame->mParams.size_thumbnail.w,
                         mpEncodeFrame->mParams.size_thumbnail.h);
            }
          } else {
            MY_LOGW("Thumbnail size is not set!");
            mpEncodeFrame->mbHasThumbnail = MFALSE;
          }
        }
      }
      // set common exif debug info
      MINT32 uniqueKey = 0, frameNumber = 0, requestNumber = 0;
      tryGetMetadata<MINT32>(pInMeta_Hal, MTK_PIPELINE_UNIQUE_KEY, &uniqueKey);
      tryGetMetadata<MINT32>(pInMeta_Hal, MTK_PIPELINE_FRAME_NUMBER,
                             &frameNumber);
      tryGetMetadata<MINT32>(pInMeta_Hal, MTK_PIPELINE_REQUEST_NUMBER,
                             &requestNumber);
      std::map<MUINT32, MUINT32> debugInfoList;
      {
        // using namespace dbg_cam_common_param_1;
        debugInfoList[dbg_cam_common_param_1::CMN_TAG_VERSION] =
            ((dbg_cam_common_param_1::CMN_DEBUG_TAG_SUBVERSION << 16) |
             dbg_cam_common_param_1::CMN_DEBUG_TAG_VERSION);
        // tag version : sub version(high 2 byte)
        // | major version(low 2 byte)
        debugInfoList[dbg_cam_common_param_1::CMN_TAG_PIPELINE_UNIQUE_KEY] =
            uniqueKey;
        debugInfoList[dbg_cam_common_param_1::CMN_TAG_PIPELINE_FRAME_NUMBER] =
            frameNumber;
        debugInfoList[dbg_cam_common_param_1::CMN_TAG_PIPELINE_REQUEST_NUMBER] =
            requestNumber;
      }
      IMetadata exifMetadata;
      tryGetMetadata<IMetadata>(pInMeta_Hal, MTK_3A_EXIF_METADATA,
                                &exifMetadata);
      if (DebugExifUtils::setDebugExif(
              DebugExifUtils::DebugExifType::DEBUG_EXIF_CAM,
              static_cast<MUINT32>(MTK_CMN_EXIF_DBGINFO_KEY),
              static_cast<MUINT32>(MTK_CMN_EXIF_DBGINFO_DATA), debugInfoList,
              &exifMetadata) == nullptr) {
        MY_LOGW("set debug exif to metadata fail");
      }
      //

      tryGetMetadata<MINT32>(pInMeta_Hal, MTK_PIPELINE_UNIQUE_KEY, &mUniqueKey);
      tryGetMetadata<MINT32>(pInMeta_Hal, MTK_PIPELINE_FRAME_NUMBER,
                             &mFrameNumber);
      tryGetMetadata<MINT32>(pInMeta_Hal, MTK_PIPELINE_REQUEST_NUMBER,
                             &mRequestNumber);
      MINT32 bound = DBG_BOUND_WIDTH * DBG_BOUND_HEIGH;
      if (imageSize.w * imageSize.h > bound) {
        mpEncodeFrame->exif.init(stdParams, mDbgInfoEnable);
        if (mDbgInfoEnable) {
          updateDebugInfoToExif(&exifMetadata, &mpEncodeFrame->exif);
        }

        MY_LOGD_IF(mLogLevel, "init (%dx%d)", imageSize.w, imageSize.h);
      } else {
        mpEncodeFrame->exif.init(stdParams, 0);
        MY_LOGD_IF(mLogLevel, "skip init (%dx%d)", imageSize.w, imageSize.h);
      }

      if (muDumpBuffer) {
        NSCam::TuningUtils::FILE_DUMP_NAMING_HINT hint;
        hint.UniqueKey = mUniqueKey;
        hint.FrameNo = mFrameNumber;
        hint.RequestNo = mRequestNumber;
        MBOOL res = MTRUE;
        res = extract(&hint, pInMeta_Hal);
        if (!res) {
          MY_LOGW("[DUMP_JPG] extract with metadata fail (%d)", res);
        }
        genFileName_JPG(filename, sizeof(filename), &hint, nullptr);
        MY_LOGD("enable muDumpBuffer FileName[%s]", filename);
      }

      returnMetadataAndUnlock(mpEncodeFrame->mpFrame,
                              mpInHalMeta->getStreamId(), pInMetaStream_Hal,
                              pInMeta_Hal);

      returnMetadataAndUnlock(pFrame, mpInAppMeta->getStreamId(),
                              pInMetaStream_Request, pInMeta_Request);

      // set thumbnail max size & thumbnail size need to be 128 alignment
      size_t& thumbMaxSize = mpEncodeFrame->thumbnailMaxSize;

      if (mpEncodeFrame->mbHasThumbnail) {
        thumbMaxSize = (mpEncodeFrame->mParams.size_thumbnail.w) *
                       (mpEncodeFrame->mParams.size_thumbnail.h) * 18 / 10;

        size_t thumbnailSize = 0;
        if ((EXIFAPP1_MAX_SIZE - mpEncodeFrame->exif.getStdExifSize()) <
            thumbMaxSize) {
          thumbnailSize =
              EXIFAPP1_MAX_SIZE - mpEncodeFrame->exif.getStdExifSize();
          size_t res = thumbnailSize % EXIFHEADER_ALIGN;
          if (res != 0) {
            thumbnailSize = thumbnailSize - res;
          }
        } else {
          thumbnailSize = thumbMaxSize;
          size_t res = thumbnailSize % EXIFHEADER_ALIGN;
          if (res != 0) {
            // prevent it would exceed EXIFAPP1_MAX_SIZE after doing thumbnail
            // size 128 alignemt
            if (thumbnailSize + EXIFHEADER_ALIGN > EXIFAPP1_MAX_SIZE) {
              thumbnailSize -= res;
            } else {
              thumbnailSize = thumbnailSize + EXIFHEADER_ALIGN - res;
            }
          }
        }

        thumbMaxSize = thumbnailSize;
      }

      size_t headerSize = mpEncodeFrame->exif.getStdExifSize() +
                          mpEncodeFrame->exif.getDbgExifSize() + thumbMaxSize;
      if (headerSize % EXIFHEADER_ALIGN != 0) {
        MY_LOGW("not aligned header size %zu", headerSize);
      }
      mpEncodeFrame->exifSize = headerSize;
      mpEncodeFrame->exif.setMaxThumbnail(thumbMaxSize);

      NSCam::IImageBufferAllocator::ImgParam imgParam(headerSize, 0);
      mpEncodeFrame->mpExifBufferHeap =
          NSCam::IGbmImageBufferHeap::create("EXIF", imgParam);
    }
    // get out main imagebuffer
    {
      NSCam::v3::StreamId_T const stream_in = mpOutJpeg->getStreamId();
      std::shared_ptr<NSCam::v3::IImageStreamBuffer>& pOutImgStreamBuffer =
          mpEncodeFrame->mpOutImgStreamBuffer;
      std::shared_ptr<IImageBufferHeap>& pImageBufferHeap =
          mpEncodeFrame->mpOutImgBufferHeap;
      std::shared_ptr<IImageBuffer> pOutImageBuffer = nullptr;
      //
      MERROR const err = getImageBufferAndLock(
          pFrame, stream_in, &pOutImgStreamBuffer, &pOutImageBuffer,
          &mpEncodeFrame, &pImageBufferHeap);
      if (err != OK) {
        MY_LOGE("getImageBufferAndLock(OutImageBuffer) err = %d", err);
        errorHandle(mpEncodeFrame);
        mpEncodeFrame = nullptr;
        return;
      }
      // remember main buffer
      mpEncodeFrame->mpJpeg_Main = pOutImageBuffer;
    }

    // get thumb image buffer and run thumb thread
    if (mpEncodeFrame->mbHasThumbnail) {
      NSCam::v3::StreamId_T const stream_in = mpOutJpeg->getStreamId();
      std::shared_ptr<IImageBuffer> pOutImageBuffer = nullptr;

      MERROR const err = getThumbImageBufferAndLock(
          pFrame, stream_in, mpEncodeFrame, mpEncodeFrame->mpExifBufferHeap,
          &pOutImageBuffer);

      if (err != OK) {
        MY_LOGE("getImageBufferAndLock err = %d", err);
        errorHandle(mpEncodeFrame);
        mpEncodeFrame = nullptr;
        return;
      }
      // remember main&thumb buffer
      mpEncodeFrame->mpJpeg_Thumbnail = pOutImageBuffer;
      mThumbDoneFlag = MFALSE;
      //
      mpEncodeThumbThread = std::make_shared<EncodeThumbThread>(this);
      if (mpEncodeThumbThread->run() != OK) {
        errorHandle(mpEncodeFrame);
        mpEncodeFrame = nullptr;
        return;
      }
    }
  }

  // 2. get src buffers & internal dst buffer for bitstream
  while (mpEncodeFrame->mpJpeg_Main != nullptr) {
    // main jpeg is not encoded, try get yuv for main jpeg
    NSCam::v3::IStreamBufferSet& streamBufferSet = pFrame->getStreamBufferSet();
    NSCam::v3::StreamId_T const stream_in = mpInYuv_main->getStreamId();

    std::shared_ptr<NSCam::v3::IImageStreamBuffer> pInImageStreamBuffer =
        nullptr;
    std::shared_ptr<IImageBuffer> pInImageBuffer = nullptr;
    //
    MERROR const err = getImageBufferAndLock(
        pFrame, stream_in, &pInImageStreamBuffer, &pInImageBuffer);
    if (err != OK) {
      MY_LOGE("getImageBufferAndLock(in main YUV) err = %d", err);
      mpEncodeFrame->mbBufValid = MFALSE;
      break;
    }
    if (mLogLevel >= 2) {
      dumpYUVBuffer(pFrame->getFrameNo(), &pInImageBuffer, 0);
    }
    // do encode
    {
      uint32_t outSize = 0;
      my_encode_params params;
      params.pSrc = pInImageBuffer;
      params.pDst = mpEncodeFrame->mpJpeg_Main;
      params.transform = 0;
      params.crop = MRect(MPoint(0, 0), pInImageBuffer->getImgSize());
      params.isSOI = 0;
      params.quality = mpEncodeFrame->mParams.quality;

      bool ret = false;
      {
        std::lock_guard<std::mutex> _l(mJpegCompressorLock);
        ret = mJpegCompressor->CompressImageFromHandle(
            params.pSrc->getImageBufferHeap()->getBufferHandle(),
            params.pDst->getImageBufferHeap()->getBufferHandle(),
            params.pSrc->getImgSize().w, params.pSrc->getImgSize().h,
            params.quality, nullptr, 0, &outSize);
      }
      if (ret != true) {
        MY_LOGE("encode main jpeg fail!");
        mpEncodeFrame->mbSuccess = MFALSE;
      } else {
        MY_LOGI("encode main jpeg success, out size is %u", outSize);
        params.pDst->setBitstreamSize(outSize);
        mpEncodeFrame->mbSuccess = MTRUE;
      }

      memmove(reinterpret_cast<void*>(
                  (mpEncodeFrame->mpJpeg_Main.get()->getBufVA(0) +
                   mpEncodeFrame->exifSize)),
              reinterpret_cast<void*>(
                  (mpEncodeFrame->mpJpeg_Main.get()->getBufVA(0) + 2)),
              outSize - 2);
    }
    //
    pInImageBuffer->unlockBuf(getNodeName());
    pInImageStreamBuffer->unlock(getNodeName(),
                                 pInImageBuffer->getImageBufferHeap().get());
    //
    streamBufferSet.markUserStatus(
        pInImageStreamBuffer->getStreamInfo()->getStreamId(), getNodeId(),
        NSCam::v3::IUsersManager::UserStatus::USED |
            NSCam::v3::IUsersManager::UserStatus::RELEASE);
    // 3. end
    {
      size_t const totalJpegSize =
          mpEncodeFrame->mpJpeg_Main->getBitstreamSize() +
          mpEncodeFrame->exif.getHeaderSize();
      mpEncodeFrame->mpJpeg_Main->getImageBufferHeap()->setBitstreamSize(
          totalJpegSize);
    }
    break;
  }

  // 4. if no thumbnail, copy to dst buffer & release buffers/metadata
  //    else add to pending list to wait for the other src buffer
  if (
      // condition 1: without thumbnail
      (!mpEncodeFrame->mbHasThumbnail && mpEncodeFrame->mpJpeg_Main.get()) ||
      // condition 2: with thumbnail
      (mpEncodeFrame->mbHasThumbnail && mpEncodeFrame->mpJpeg_Main.get() &&
       mpEncodeFrame->mpJpeg_Thumbnail.get())) {
    {
      std::unique_lock<std::mutex> _l(mEncodeLock);
      if (mThumbDoneFlag != MTRUE) {
        MY_LOGD("waiting thumbnail encoding done+");
        mEncodeCond.wait(_l);
        MY_LOGD("waiting thumbnail encoding done-");
      } else {
        MY_LOGD_IF(mLogLevel, "enc done and go on...");
      }
    }
    if (mpEncodeFrame->mbBufValid != MTRUE) {
      unlockImage(&mpEncodeFrame->mpOutImgStreamBuffer,
                  &mpEncodeFrame->mpJpeg_Main,
                  &mpEncodeFrame->mpJpeg_Thumbnail);
      errorHandle(mpEncodeFrame);
    } else {
      mpEncodeFrame->mpJpeg_Main->unlockBuf(getNodeName());
      if (mpEncodeFrame->mpJpeg_Thumbnail.get()) {
        mpEncodeFrame->mpJpeg_Thumbnail->unlockBuf(getNodeName());
      }
      finalizeEncodeFrame(&mpEncodeFrame);
    }
    //
    mpEncodeFrame = nullptr;
  }

  FUNC_END;
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::verifyConfigParams(ConfigParams const& rParams) const {
  if (!rParams.pInAppMeta.get()) {
    MY_LOGE("no in app meta");
    return BAD_VALUE;
  }
  if (!rParams.pOutAppMeta.get()) {
    MY_LOGE("no out app meta");
    return BAD_VALUE;
  }
  if (nullptr == rParams.pInYuv_Main.get()) {
    MY_LOGE("no in hal main yuv image");
    return BAD_VALUE;
  }

  if (!rParams.pOutJpeg.get()) {
    MY_LOGE("no out hal jpeg image");
    return BAD_VALUE;
  }
  //
  MY_LOGD_IF(rParams.pInAppMeta.get() && rParams.pOutAppMeta.get(),
             "stream: [meta] in app %#" PRIx64 ", out app %#" PRIx64,
             rParams.pInAppMeta->getStreamId(),
             rParams.pOutAppMeta->getStreamId());
  MY_LOGD_IF(rParams.pInHalMeta_capture.get(),
             "stream: [meta] in hal capture %#" PRIx64,
             rParams.pInHalMeta_capture->getStreamId());
  MY_LOGD_IF(rParams.pInHalMeta_streaming.get(),
             "stream: [meta] in hal streaming %#" PRIx64,
             rParams.pInHalMeta_streaming->getStreamId());
  MY_LOGD_IF(rParams.pInYuv_Main.get(), "stream: [img] in main %#" PRIx64,
             rParams.pInYuv_Main->getStreamId());
  MY_LOGD_IF(rParams.pInYuv_Thumbnail.get(),
             "stream: [img] in thumbnail %#" PRIx64,
             rParams.pInYuv_Thumbnail->getStreamId());
  MY_LOGD_IF(rParams.pOutJpeg.get(), "stream: [img] out jpeg %#" PRIx64,
             rParams.pOutJpeg->getStreamId());
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::waitForRequestDrained() {
  FUNC_START;
  //
  std::unique_lock<std::mutex> _l(mRequestQueueLock);
  if (!mbRequestDrained) {
    MY_LOGD("wait for request drained");
    mbRequestDrainedCond.wait(_l);
  }
  //
  FUNC_END;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::getImageBufferAndLock(
    std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame,
    NSCam::v3::StreamId_T const streamId,
    std::shared_ptr<NSCam::v3::IImageStreamBuffer>* rpStreamBuffer,
    std::shared_ptr<IImageBuffer>* rpImageBuffer) {
  NSCam::v3::IStreamBufferSet& rStreamBufferSet = pFrame->getStreamBufferSet();
  std::shared_ptr<IImageBufferHeap> pImageBufferHeap = nullptr;
  MERROR const err = ensureImageBufferAvailable(
      pFrame->getFrameNo(), streamId, &rStreamBufferSet, rpStreamBuffer);

  if (err != OK) {
    return err;
  }

  //  Query the group usage.
  MUINT const groupUsage = (*rpStreamBuffer)->queryGroupUsage(getNodeId());
  if (isInImageStream(streamId)) {
    pImageBufferHeap.reset(
        (*rpStreamBuffer)->tryReadLock(getNodeName()),
        [](IImageBufferHeap* p) { MY_LOGI("release implement"); });
  } else {
    pImageBufferHeap.reset(
        (*rpStreamBuffer)->tryWriteLock(getNodeName()),
        [](IImageBufferHeap* p) { MY_LOGI("release implement"); });
  }

  if (pImageBufferHeap == nullptr) {
    MY_LOGE("pImageBufferHeap == NULL");
    return BAD_VALUE;
  }

  *rpImageBuffer = pImageBufferHeap->createImageBuffer();
  if (*rpImageBuffer == nullptr) {
    (*rpStreamBuffer)->unlock(getNodeName(), pImageBufferHeap.get());
    MY_LOGE("rpImageBuffer == NULL");
    return BAD_VALUE;
  }
  MBOOL ret = (*rpImageBuffer)->lockBuf(getNodeName(), groupUsage);
  if (!ret) {
    return BAD_VALUE;
  }

  MY_LOGD_IF(mLogLevel,
             "stream buffer: (%#" PRIx64
             ") %p, heap: %p, buffer: %p, usage: %u",
             streamId, (*rpStreamBuffer).get(), pImageBufferHeap.get(),
             (*rpImageBuffer).get(), groupUsage);

  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::getMetadataAndLock(
    std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame,
    NSCam::v3::StreamId_T const streamId,
    std::shared_ptr<NSCam::v3::IMetaStreamBuffer>* rpStreamBuffer,
    IMetadata** rpMetadata) {
  NSCam::v3::IStreamBufferSet& rStreamBufferSet = pFrame->getStreamBufferSet();
  MY_LOGD_IF(mLogLevel >= 2, "nodeID %#" PRIxPTR " streamID %#" PRIx64 " ",
             getNodeId(), streamId);
  MERROR const err = ensureMetaBufferAvailable(
      pFrame->getFrameNo(), streamId, &rStreamBufferSet, rpStreamBuffer);

  if (err != OK) {
    MY_LOGD_IF(*rpStreamBuffer == NULL,
               "streamId(%#" PRIx64 ") meta streamBuf not exit", streamId);
    return err;
  }

  *rpMetadata = isInMetaStream(streamId)
                    ? (*rpStreamBuffer)->tryReadLock(getNodeName())
                    : (*rpStreamBuffer)->tryWriteLock(getNodeName());
  if (rpMetadata == nullptr) {
    MY_LOGE("[frame:%u node:%#" PRIxPTR
            "][stream buffer:%s] cannot get metadata",
            pFrame->getFrameNo(), getNodeId(), (*rpStreamBuffer)->getName());
    return BAD_VALUE;
  }

  MY_LOGD_IF(mLogLevel, "stream %#" PRIx64 ": stream buffer %p, metadata: %p",
             streamId, (*rpStreamBuffer).get(), *rpMetadata);

  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::returnMetadataAndUnlock(
    std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame,
    NSCam::v3::StreamId_T const streamId,
    std::shared_ptr<NSCam::v3::IMetaStreamBuffer> rpStreamBuffer,
    IMetadata* rpMetadata,
    MBOOL success) {
  NSCam::v3::IStreamBufferSet& rStreamBufferSet = pFrame->getStreamBufferSet();
  //
  if (rpStreamBuffer == nullptr) {
    MY_LOGE("StreamId %#" PRIx64 ": rpStreamBuffer == NULL", streamId);
    return;
  }
  //
  // Buffer Producer must set this status.
  if (!isInMetaStream(streamId)) {
    if (success) {
      rpStreamBuffer->markStatus(NSCam::v3::STREAM_BUFFER_STATUS::WRITE_OK);
    } else {
      rpStreamBuffer->markStatus(NSCam::v3::STREAM_BUFFER_STATUS::WRITE_ERROR);
    }
  }
  //
  if (rpMetadata) {
    rpStreamBuffer->unlock(getNodeName(), rpMetadata);
  }
  //
  //  Mark this buffer as USED by this user.
  //  Mark this buffer as RELEASE by this user.
  rStreamBufferSet.markUserStatus(
      streamId, getNodeId(),
      NSCam::v3::IUsersManager::UserStatus::USED |
          NSCam::v3::IUsersManager::UserStatus::RELEASE);
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
JpegNodeImp::isInMetaStream(NSCam::v3::StreamId_T const streamId) const {
  pthread_rwlock_rdlock(&mConfigRWLock);
  MBOOL ret =
      isStream(mpInAppMeta, streamId) || isStream(mpInHalMeta, streamId);
  pthread_rwlock_unlock(&mConfigRWLock);
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
JpegNodeImp::isInImageStream(NSCam::v3::StreamId_T const streamId) const {
  pthread_rwlock_rdlock(&mConfigRWLock);
  //
  if (isStream(mpInYuv_main, streamId)) {
    pthread_rwlock_unlock(&mConfigRWLock);
    return MTRUE;
  }
  //
  if (isStream(mpInYuv_thumbnail, streamId)) {
    pthread_rwlock_unlock(&mConfigRWLock);
    return MTRUE;
  }
  //
  MY_LOGD_IF(1, "stream id %#" PRIx64 " is not in-stream", streamId);
  pthread_rwlock_unlock(&mConfigRWLock);
  return MFALSE;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::finalizeEncodeFrame(std::shared_ptr<encode_frame>* rpEncodeFrame) {
  std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame =
      (*rpEncodeFrame)->mpFrame;
  NSCam::v3::IStreamBufferSet& streamBufferSet = pFrame->getStreamBufferSet();

  // update metadata
  {
    IMetadata* pOutMeta_Result = nullptr;
    std::shared_ptr<NSCam::v3::IMetaStreamBuffer> pOutMetaStream_Result =
        nullptr;

    MERROR const err __unused =
        getMetadataAndLock(pFrame, mpOutMetaStreamInfo_Result->getStreamId(),
                           &pOutMetaStream_Result, &pOutMeta_Result);

    updateMetadata(&((*rpEncodeFrame)->mParams), pOutMeta_Result);

    returnMetadataAndUnlock(pFrame, mpOutMetaStreamInfo_Result->getStreamId(),
                            pOutMetaStream_Result, pOutMeta_Result,
                            (*rpEncodeFrame)->mbSuccess);
  }

  // get out buffer
  {
    std::shared_ptr<NSCam::v3::IImageStreamBuffer> pOutImgStreamBuffer =
        (*rpEncodeFrame)->mpOutImgStreamBuffer;  //
    std::shared_ptr<IImageBuffer> pOutImageBuffer = nullptr;

    pOutImageBuffer = (*rpEncodeFrame)
                          ->mpExifBufferHeap->createImageBuffer_FromBlobHeap(
                              0, (*rpEncodeFrame)->exif.getHeaderSize());

    if (!pOutImageBuffer) {
      MY_LOGE("rpImageBuffer == NULL");
    }
    MUINT const groupUsage = pOutImgStreamBuffer->queryGroupUsage(getNodeId());
    MBOOL ret = pOutImageBuffer->lockBuf(getNodeName(), groupUsage);
    if (!ret) {
      return;
    }

    size_t exifSize = 0;
    MINT8* pExifBuf = reinterpret_cast<MINT8*>(pOutImageBuffer->getBufVA(0));
    if (pExifBuf == nullptr ||
        OK != makeExifHeader((*rpEncodeFrame), pExifBuf, &exifSize)) {
      (*rpEncodeFrame)->mbSuccess = MFALSE;
      MY_LOGE("frame %u make exif header failed: buf %p, size %zu",
              (*rpEncodeFrame)->mpFrame->getFrameNo(), pExifBuf, exifSize);
    }

    (*rpEncodeFrame)->mpOutImgBufferHeap->lockBuf(getNodeName(), groupUsage);
    memmove(reinterpret_cast<void*>(
                (*rpEncodeFrame)->mpOutImgBufferHeap->getBufVA(0)),
            reinterpret_cast<void*>(pExifBuf), mpEncodeFrame->exifSize);
    pExifBuf = nullptr;
    (*rpEncodeFrame)->mpOutImgBufferHeap->unlockBuf(getNodeName());

    pOutImageBuffer->unlockBuf(getNodeName());
    pOutImgStreamBuffer->markStatus(
        (*rpEncodeFrame)->mbSuccess
            ? NSCam::v3::STREAM_BUFFER_STATUS::WRITE_OK
            : NSCam::v3::STREAM_BUFFER_STATUS::WRITE_ERROR);

    //  Mark this buffer as USED by this user.
    //  Mark this buffer as RELEASE by this user.
    streamBufferSet.markUserStatus(
        pOutImgStreamBuffer->getStreamInfo()->getStreamId(), getNodeId(),
        NSCam::v3::IUsersManager::UserStatus::USED |
            NSCam::v3::IUsersManager::UserStatus::RELEASE);
  }

  if (muDumpBuffer) {
    std::shared_ptr<NSCam::v3::IImageStreamBuffer> pStreamBuffer =
        (*rpEncodeFrame)->mpOutImgStreamBuffer;
    int jpeg_size = (*rpEncodeFrame)->exif.getHeaderSize() +
                    (*rpEncodeFrame)->mpJpeg_Main->getBitstreamSize();
    std::shared_ptr<IImageBuffer> dumpImgBuffer =
        (*rpEncodeFrame)
            ->mpOutImgBufferHeap->createImageBuffer_FromBlobHeap(0, jpeg_size);
    if (!dumpImgBuffer) {
      MY_LOGE("dumpBuffer == NULL");
      return;
    }

    MUINT groupUsage = pStreamBuffer->queryGroupUsage(getNodeId());
    groupUsage |= NSCam::eBUFFER_USAGE_SW_READ_OFTEN;
    MBOOL ret = dumpImgBuffer->lockBuf(getNodeName(), groupUsage);
    if (!ret) {
      return;
    }

    if (!NSCam::Utils::makePath(JPEG_DUMP_PATH, 0660)) {
      MY_LOGI("makePath[%s] fails", JPEG_DUMP_PATH);
    }

    MBOOL rets = dumpImgBuffer->saveToFile(filename);
    MY_LOGI("[DUMP_JPG] SaveFile[%s]:(%d)", filename, rets);

    dumpImgBuffer->unlockBuf(getNodeName());
  }
  //
  // release
  streamBufferSet.applyRelease(getNodeId());
#if ENABLE_PRERELEASE
  MY_LOGD("jpeg node-release SB");
  mpTimeline->inc(1);
#endif
  onDispatchFrame(pFrame);
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::getJpegParams(IMetadata* pMetadata_request,
                           jpeg_params* rParams) const {
  if (nullptr == pMetadata_request) {
    MY_LOGE("pMetadata_request=NULL");
  }
  (*rParams).gpsCoordinates =
      pMetadata_request->entryFor(MTK_JPEG_GPS_COORDINATES);

  (*rParams).gpsProcessingMethod =
      pMetadata_request->entryFor(MTK_JPEG_GPS_PROCESSING_METHOD);

  (*rParams).gpsTimestamp = pMetadata_request->entryFor(MTK_JPEG_GPS_TIMESTAMP);

#define getParam(meta, tag, type, param)            \
  do {                                              \
    if (!tryGetMetadata<type>(meta, tag, &param)) { \
      MY_LOGI("no tag: %s", #tag);                  \
    }                                               \
  } while (0)
#define getAppParam(tag, type, param) \
  getParam(pMetadata_request, tag, type, param)

  // request from app
  getAppParam(MTK_JPEG_ORIENTATION, MINT32, (*rParams).orientation);
  getAppParam(MTK_JPEG_QUALITY, MUINT8, (*rParams).quality);
  getAppParam(MTK_JPEG_THUMBNAIL_QUALITY, MUINT8, (*rParams).quality_thumbnail);
  getAppParam(MTK_JPEG_THUMBNAIL_SIZE, MSize, (*rParams).size_thumbnail);
  getAppParam(MTK_SCALER_CROP_REGION, MRect, (*rParams).cropRegion);
  getAppParam(MTK_CONTROL_CAPTURE_JPEG_FLIP_MODE, MINT32, (*rParams).flipMode);

#undef getAppParam
#undef getParam

  if (mJpegRotationEnable) {
    if ((*rParams).orientation == 90 || (*rParams).orientation == 270) {
      MINT32 tmp = (*rParams).size_thumbnail.w;
      (*rParams).size_thumbnail.w = (*rParams).size_thumbnail.h;
      (*rParams).size_thumbnail.h = tmp;
    }
    MY_LOGD_IF(0, "@@getJpegParams thumb size(w,h)=(%dx%d)",
               (*rParams).size_thumbnail.w, (*rParams).size_thumbnail.h);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::updateMetadata(jpeg_params* rParams,
                            IMetadata* pMetadata_result) const {
#define updateNonEmptyEntry(pMetadata, tag, entry) \
  do {                                             \
    if (!entry.isEmpty()) {                        \
      pMetadata->update(tag, entry);               \
    }                                              \
  } while (0)

  // gps related
  updateNonEmptyEntry(pMetadata_result, MTK_JPEG_GPS_COORDINATES,
                      (*rParams).gpsCoordinates);
  updateNonEmptyEntry(pMetadata_result, MTK_JPEG_GPS_PROCESSING_METHOD,
                      (*rParams).gpsProcessingMethod);
  updateNonEmptyEntry(pMetadata_result, MTK_JPEG_GPS_TIMESTAMP,
                      (*rParams).gpsTimestamp);
  //
  updateEntry<MINT32>(pMetadata_result, MTK_JPEG_ORIENTATION,
                      (*rParams).orientation);
  updateEntry<MUINT8>(pMetadata_result, MTK_JPEG_QUALITY, (*rParams).quality);
  updateEntry<MUINT8>(pMetadata_result, MTK_JPEG_THUMBNAIL_QUALITY,
                      (*rParams).quality_thumbnail);
  updateEntry<MSize>(pMetadata_result, MTK_JPEG_THUMBNAIL_SIZE,
                     (*rParams).size_thumbnail);
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::makeExifHeader(
    std::shared_ptr<encode_frame> rpEncodeFrame,
    MINT8* const pOutExif,
    size_t* rOutExifSize  // [IN/OUT] in: exif buf size, out: exif header size
) {
  MERROR ret;
  //
  ret = rpEncodeFrame->exif.make((MUINTPTR)pOutExif, rOutExifSize);
  //
  rpEncodeFrame->exif.uninit();
  //
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::updateStdExifParam(MBOOL const& rNeedExifRotate,
                                MSize const& rSize,
                                IMetadata* const rpAppMeta,
                                IMetadata* const rpHalMeta,
                                jpeg_params const& rParams,
                                ExifParams* rStdParams) const {
  (*rStdParams).u4ImageWidth = rSize.w;
  (*rStdParams).u4ImageHeight = rSize.h;
  //
  // 3A
  if (rpHalMeta && rpAppMeta) {
    IMetadata exifMeta;
    if (tryGetMetadata<IMetadata>(rpHalMeta, MTK_3A_EXIF_METADATA, &exifMeta)) {
      updateStdExifParam_3A(exifMeta, *rpAppMeta, rStdParams);
    } else {
      MY_LOGW("no tag: MTK_3A_EXIF_METADATA");
    }
  } else {
    MY_LOGW("no in hal meta or app meta");
  }
  // gps
  updateStdExifParam_gps(rParams.gpsCoordinates, rParams.gpsProcessingMethod,
                         rParams.gpsTimestamp, rStdParams);
  // icc profile
  if (rpHalMeta) {
    MINT32 iccIdx = -1;
    if (!tryGetMetadata<MINT32>(rpHalMeta, MTK_ISP_COLOR_SPACE, &iccIdx)) {
      MY_LOGI("no tag: MTK_ISP_COLOR_SPACE");
    } else {
      if (iccIdx == MTK_ISP_COLOR_SPACE_SRGB) {
        (*rStdParams).u4ICCIdx = EXIF_ICC_PROFILE_SRGB;
      } else if (iccIdx == MTK_ISP_COLOR_SPACE_DISPLAY_P3) {
        (*rStdParams).u4ICCIdx = EXIF_ICC_PROFILE_DCI_P3;
      } else {
        MY_LOGW("not support isp profile in MTK_ISP_COLOR_SPACE %d ", iccIdx);
      }
    }
  }
  // others
  if (!rNeedExifRotate) {
    (*rStdParams).u4Orientation = 22;
  } else {
    (*rStdParams).u4Orientation = rParams.orientation;
  }
  (*rStdParams).u4ZoomRatio = calcZoomRatio(rParams.cropRegion, rSize);
  (*rStdParams).u4Facing = (muFacing == MTK_LENS_FACING_BACK) ? 0 : 1;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::updateStdExifParam_3A(IMetadata const& rMeta,
                                   IMetadata const& rAppMeta,
                                   ExifParams* rStdParams) const {
#define getParam(meta, tag, type, param)          \
  do {                                            \
    type val = -1;                                \
    if (!tryGetMetadata<type>(meta, tag, &val)) { \
      MY_LOGI("no tag: %s", #tag);                \
    }                                             \
    param = val;                                  \
  } while (0)

  // from result meta of 3A
  // for Hal3 yuv reprocessing [must need!]
  getParam(&rMeta, MTK_3A_EXIF_FNUMBER, MINT32, (*rStdParams).u4FNumber); /**/
  if ((*rStdParams).u4FNumber == -1) {
    MFLOAT fNumber = 0.0f;
    getParam(&rAppMeta, MTK_LENS_APERTURE, MFLOAT, fNumber);
    (*rStdParams).u4FNumber = fNumber * 10;
    MY_LOGD("miss in Hal find APP MTK_LENS_APERTURE : %d",
            (*rStdParams).u4FNumber);
  }
  getParam(&rMeta, MTK_3A_EXIF_FOCAL_LENGTH, MINT32,
           (*rStdParams).u4FocalLength); /**/
  if ((*rStdParams).u4FocalLength == -1) {
    MFLOAT focalLength = 0.0f;
    getParam(&rAppMeta, MTK_LENS_FOCAL_LENGTH, MFLOAT, focalLength);
    (*rStdParams).u4FocalLength = focalLength * 1000;
    MY_LOGD("miss in Hal find APP MTK_LENS_FOCAL_LENGTH : %d",
            (*rStdParams).u4FocalLength);
  }
  getParam(&rMeta, MTK_3A_EXIF_CAP_EXPOSURE_TIME, MINT32,
           (*rStdParams).u4CapExposureTime); /**/
  if ((*rStdParams).u4CapExposureTime == -1) {
    MINT64 capExposure = 0;
    getParam(&rAppMeta, MTK_SENSOR_EXPOSURE_TIME, MINT64, capExposure);
    (*rStdParams).u4CapExposureTime = (MINT32)(capExposure / 1000);
    MY_LOGD("miss in Hal find APP MTK_3A_EXIF_CAP_EXPOSURE_TIME : %" PRId32,
            (*rStdParams).u4CapExposureTime);
  }
  getParam(&rMeta, MTK_3A_EXIF_AE_ISO_SPEED, MINT32,
           (*rStdParams).u4AEISOSpeed); /**/
  if ((*rStdParams).u4AEISOSpeed == -1) {
    getParam(&rAppMeta, MTK_SENSOR_SENSITIVITY, MINT32,
             (*rStdParams).u4AEISOSpeed);
    MY_LOGD("miss in Hal find APP MTK_SENSOR_SENSITIVITY : %d",
            (*rStdParams).u4AEISOSpeed);
  }
  //
  getParam(&rMeta, MTK_3A_EXIF_FOCAL_LENGTH_35MM, MINT32,
           (*rStdParams).u4FocalLength35mm);
  getParam(&rMeta, MTK_3A_EXIF_AWB_MODE, MINT32, (*rStdParams).u4AWBMode);
  getParam(&rMeta, MTK_3A_EXIF_LIGHT_SOURCE, MINT32,
           (*rStdParams).u4LightSource);
  getParam(&rMeta, MTK_3A_EXIF_EXP_PROGRAM, MINT32, (*rStdParams).u4ExpProgram);
  getParam(&rMeta, MTK_3A_EXIF_SCENE_CAP_TYPE, MINT32,
           (*rStdParams).u4SceneCapType);
  getParam(&rMeta, MTK_3A_EXIF_FLASH_LIGHT_TIME_US, MINT32,
           (*rStdParams).u4FlashLightTimeus);
  getParam(&rMeta, MTK_3A_EXIF_AE_METER_MODE, MINT32,
           (*rStdParams).u4AEMeterMode);
  getParam(&rMeta, MTK_3A_EXIF_AE_EXP_BIAS, MINT32, (*rStdParams).i4AEExpBias);
#undef getParam
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::updateStdExifParam_gps(
    IMetadata::IEntry const& rGpsCoordinates,
    IMetadata::IEntry const& rGpsProcessingMethod,
    IMetadata::IEntry const& rGpsTimestamp,
    ExifParams* rStdParams) const {
  if (rGpsCoordinates.count() == 3) {
    (*rStdParams).u4GpsIsOn = 1;
    // latitude
    ::snprintf(reinterpret_cast<char*>((*rStdParams).uGPSLatitude),
               sizeof((*rStdParams).uGPSLatitude), "%f",
               rGpsCoordinates.itemAt(0, Type2Type<MDOUBLE>()));
    // longitude
    ::snprintf(reinterpret_cast<char*>((*rStdParams).uGPSLongitude),
               sizeof((*rStdParams).uGPSLongitude), "%f",
               rGpsCoordinates.itemAt(1, Type2Type<MDOUBLE>()));
    // altitude
    (*rStdParams).u4GPSAltitude =
        (MUINT32)rGpsCoordinates.itemAt(2, Type2Type<MDOUBLE>());

    // timestamp
    if (!rGpsTimestamp.isEmpty()) {
      ::snprintf(reinterpret_cast<char*>((*rStdParams).uGPSTimeStamp),
                 sizeof((*rStdParams).uGPSTimeStamp), "%" PRId64 "",
                 rGpsTimestamp.itemAt(0, Type2Type<MINT64>()));
    } else {
      MY_LOGW("no MTK_JPEG_GPS_TIMESTAMP");
    }

    if (!rGpsProcessingMethod.isEmpty()) {
      size_t size = rGpsProcessingMethod.count();
      if (size > 64) {
        MY_LOGW("gps processing method too long, size %zu", size);
        size = 64;
      }

      for (size_t i = 0; i < size; i++) {
        (*rStdParams).uGPSProcessingMethod[i] =
            rGpsProcessingMethod.itemAt(i, Type2Type<MUINT8>());
      }
      (*rStdParams).uGPSProcessingMethod[63] = '\0';  // null-terminating
    } else {
      MY_LOGW("no MTK_JPEG_GPS_PROCESSING_METHOD");
    }
  } else {
    MY_LOGD_IF(1, "no gps data, coordinates count %d", rGpsCoordinates.count());
    // no gps data
    (*rStdParams).u4GpsIsOn = 0;
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::updateDebugInfoToExif(IMetadata* const pExifMeta,
                                   StdExif* exif) const {
  if (pExifMeta == nullptr) {
    MY_LOGW("pExifMeta is NULL, update debug info to exif fail");
    return;
  }
  MUINT32 dbgKey = MTK_3A_EXIF_DEBUGINFO_BEGIN;
  MUINT32 dbgVal = MTK_3A_EXIF_DEBUGINFO_BEGIN + 1;
  while (dbgVal < MTK_3A_EXIF_DEBUGINFO_END) {
    MINT32 key;
    IMetadata::Memory dbgmem;
    if (tryGetMetadata<MINT32>(pExifMeta, dbgKey, &key) &&
        tryGetMetadata<IMetadata::Memory>(pExifMeta, dbgVal, &dbgmem)) {
      MINT32 ID;
      void* data = static_cast<void*>(dbgmem.editArray());
      size_t size = dbgmem.size();
      if (size > 0) {
        MY_LOGD_IF(mLogLevel, "key 0x%x, data %p, size %zu", key, data, size);
        (*exif).sendCommand(CMD_REGISTER, key, reinterpret_cast<MUINTPTR>(&ID));
        (*exif).sendCommand(CMD_SET_DBG_EXIF, ID,
                            reinterpret_cast<MUINTPTR>(data), size);
      } else {
        MY_LOGW("key 0x%x with size %zu", key, size);
      }
    }
    //
    dbgKey += 2;
    dbgVal += 2;
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MUINT32
JpegNodeImp::calcZoomRatio(MRect const& cropRegion, MSize const& rSize) const {
  pthread_rwlock_rdlock(&mConfigRWLock);
  MUINT32 zoomRatio = 100;
  if (!mpOutJpeg) {
    MY_LOGW("jpeg stream is not configured");
    pthread_rwlock_unlock(&mConfigRWLock);
    return 100;
  }

  MRect const cropAspect =
      calCropAspect(cropRegion.s, rSize);  // mpOutJpeg->getImgSize()
  if (!cropAspect.s) {
    MY_LOGW("cropRegion(%d, %d, %dx%d), jpeg size %dx%d", cropRegion.p.x,
            cropRegion.p.y, cropRegion.s.w, cropRegion.s.h, rSize.w, rSize.h);
    pthread_rwlock_unlock(&mConfigRWLock);
    return 100;
  }

  {
    MUINT32 val0 = cropAspect.s.w * mActiveArray.s.h;
    MUINT32 val1 = cropAspect.s.h * mActiveArray.s.w;
    if (val0 > val1) {
      zoomRatio = mActiveArray.s.w * 100 / cropAspect.s.w;
    } else {
      zoomRatio = mActiveArray.s.h * 100 / cropAspect.s.h;
    }
  }

  MY_LOGD("active(%d, %d, %dx%d), cropRegion(%d, %d, %dx%d), zoomRatio %d",
          mActiveArray.p.x, mActiveArray.p.y, mActiveArray.s.w,
          mActiveArray.s.h, cropRegion.p.x, cropRegion.p.y, cropRegion.s.w,
          cropRegion.s.h, zoomRatio);
  pthread_rwlock_unlock(&mConfigRWLock);
  return zoomRatio;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::threadSetting() {
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
void JpegNodeImp::EncodeThread::requestExit() {
  FUNC_START;
  // TODO(MTK): refine this
  std::lock_guard<std::mutex> _l(mpNodeImp->mRequestQueueLock);
  mpNodeImp->mbRequestExit = MTRUE;
  mpNodeImp->mRequestQueueCond.notify_one();
  FUNC_END;
}

/******************************************************************************
 *
 ******************************************************************************/
status_t JpegNodeImp::EncodeThread::readyToRun() {
  return mpNodeImp->threadSetting();
}

/******************************************************************************
 *
 ******************************************************************************/
status_t JpegNodeImp::EncodeThread::run() {
  mThread =
      std::thread(std::bind(&JpegNodeImp::EncodeThread::threadLoop, this));
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
status_t JpegNodeImp::EncodeThread::join() {
  if (mThread.joinable()) {
    mThread.join();
  }
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
bool JpegNodeImp::EncodeThread::threadLoop() {
  while (this->_threadLoop() == true) {
  }
  MY_LOGI("threadLoop exit");
  return true;
}

/******************************************************************************
 *
 ******************************************************************************/
bool JpegNodeImp::EncodeThread::_threadLoop() {
  std::shared_ptr<NSCam::v3::IPipelineFrame> pFrame;
  if (OK == mpNodeImp->onDequeRequest(&pFrame) && pFrame != nullptr) {
    mpNodeImp->mThumbDoneFlag = MTRUE;
    mpNodeImp->onProcessFrame(pFrame);
    return true;
  }

  MY_LOGD("exit encode thread %d", mpNodeImp->mThumbDoneFlag);
  return false;
}

/******************************************************************************
 *
 ******************************************************************************/
void JpegNodeImp::EncodeThumbThread::requestExit() {
  FUNC_START;
  FUNC_END;
}

/******************************************************************************
 *
 ******************************************************************************/
status_t JpegNodeImp::EncodeThumbThread::readyToRun() {
  return mpNodeImp->threadSetting();
}

/******************************************************************************
 *
 ******************************************************************************/
status_t JpegNodeImp::EncodeThumbThread::run() {
  mThread =
      std::thread(std::bind(&JpegNodeImp::EncodeThumbThread::threadLoop, this));
  mThread.detach();
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
bool JpegNodeImp::EncodeThumbThread::threadLoop() {
  mpNodeImp->encodeThumbnail(&(mpNodeImp->mpEncodeFrame));
  {
    std::lock_guard<std::mutex> _l(mpNodeImp->mEncodeLock);
    mpNodeImp->mThumbDoneFlag = MTRUE;
    mpNodeImp->mEncodeCond.notify_one();
  }
  MY_LOGD_IF(mpNodeImp->mLogLevel, "exit thumb encode thread");
  return true;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::getThumbImageBufferAndLock(
    std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame,
    NSCam::v3::StreamId_T const streamId,
    std::shared_ptr<encode_frame> const& rpEncodeFrame,
    std::shared_ptr<IImageBufferHeap> const& rpImageBufferHeap,
    std::shared_ptr<IImageBuffer>* rpImageBuffer /*out*/
) {
  std::shared_ptr<NSCam::v3::IImageStreamInfo> pStreamInfo =
      pFrame->getStreamInfoSet().getImageInfoFor(streamId);

  if (rpImageBufferHeap == nullptr) {
    MY_LOGE("exif heap not exist");
    return BAD_VALUE;
  }

  MUINT const groupUsage =
      rpEncodeFrame->mpOutImgStreamBuffer->queryGroupUsage(getNodeId());

  MBOOL ret = rpImageBufferHeap->lockBuf("EXIF", groupUsage);
  if (!ret) {
    return BAD_VALUE;
  }

  // get thumb IImageBuffer
  size_t thumbnailMaxSize = rpEncodeFrame->thumbnailMaxSize;
  size_t thumbnailOffset = rpEncodeFrame->exif.getStdExifSize();

  size_t const bufStridesInBytes[3] = {thumbnailMaxSize, 0, 0};
  size_t bufBoundaryInBytes[] = {0, 0, 0};
  // ref v1 prepare heap & imagebuffer

  NSCam::IImageBufferAllocator::ImgParam imgParam =
      NSCam::IImageBufferAllocator::ImgParam(
          rpImageBufferHeap->getImgFormat(),  // blob
          MSize(rpEncodeFrame->mParams.size_thumbnail.w,
                rpEncodeFrame->mParams.size_thumbnail.h),
          bufStridesInBytes, bufBoundaryInBytes,
          NSCam::Utils::Format::queryPlaneCount(
              rpImageBufferHeap->getImgFormat()));

  PortBufInfo_v1 portBufInfo = PortBufInfo_v1(
      rpImageBufferHeap->getHeapID(),
      (MUINTPTR)(rpImageBufferHeap->getBufVA(0) + thumbnailOffset));

  MBOOL mbEnableIImageBufferLog = MTRUE;
  std::shared_ptr<IImageBufferHeap> pHeap = NSCam::ImageBufferHeap::create(
      LOG_TAG, imgParam, portBufInfo, mbEnableIImageBufferLog);
  if (pHeap == nullptr) {
    MY_LOGE("pHeap is NULL");
    return BAD_VALUE;
  }
  *rpImageBuffer = pHeap->createImageBuffer_FromBlobHeap(
      0, eImgFmt_JPEG, rpEncodeFrame->mParams.size_thumbnail,
      bufStridesInBytes);

  ret = (*rpImageBuffer)->lockBuf(getNodeName(), groupUsage);
  if (!ret) {
    return BAD_VALUE;
  }

  if (!(*rpImageBuffer)) {
    MY_LOGE("rpImageThumbnailBuffer == NULL");
    return BAD_VALUE;
  }

  MY_LOGD(
      "thumb stream buffer(%#" PRIx64
      "), heap(0x%x): %p, buffer: %p, usage: %x, heapVA: %zx, bufferVA: %zx",
      streamId, rpImageBufferHeap->getImgFormat(), rpImageBufferHeap.get(),
      (*rpImageBuffer).get(), groupUsage, rpImageBufferHeap->getBufVA(0),
      (*rpImageBuffer)->getBufVA(0));

  rpImageBufferHeap->unlockBuf("EXIF");

  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::getStreamInfo(
    NSCam::v3::StreamId_T const streamId,
    std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame,
    std::shared_ptr<NSCam::v3::IImageStreamInfo>* rpStreamInfo) {
  NSCam::v3::IStreamBufferSet& rStreamBufferSet = pFrame->getStreamBufferSet();
  std::shared_ptr<NSCam::v3::IImageStreamBuffer> rpStreamBuffer = nullptr;

  MERROR const err = ensureImageBufferAvailable(
      pFrame->getFrameNo(), streamId, &rStreamBufferSet, &rpStreamBuffer);
  if (err != OK) {
    return err;
  }
  *rpStreamInfo = rpStreamBuffer->getStreamInfo();

  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::getImageBufferAndLock(
    std::shared_ptr<NSCam::v3::IPipelineFrame> const& pFrame,
    NSCam::v3::StreamId_T const streamId,
    std::shared_ptr<NSCam::v3::IImageStreamBuffer>* rpStreamBuffer,
    std::shared_ptr<IImageBuffer>* rpImageBuffer,
    std::shared_ptr<encode_frame>* rpEncodeFrame,
    std::shared_ptr<IImageBufferHeap>* rpImageBufferHeap) {
  NSCam::v3::IStreamBufferSet& rStreamBufferSet = pFrame->getStreamBufferSet();

  if (!(*rpImageBufferHeap)) {
    MERROR const err = ensureImageBufferAvailable(
        pFrame->getFrameNo(), streamId, &rStreamBufferSet, rpStreamBuffer);
    if (err != OK) {
      return err;
    }

    // pre-release
#if ENABLE_PRERELEASE
    if (streamId != 0) {
      MY_LOGD("test prerelease flow start w/ timeline counter: %d",
              mTimelineCounter);
      // prepare timeline & release fence
      std::shared_ptr<IFence> release_fence = nullptr;
      int sync_fence_fd =
          mpTimeline->createFence("RF_Jpeg", ++mTimelineCounter);
      rStreamBufferSet.setUserReleaseFence(streamId, getNodeId(),
                                           sync_fence_fd);
      //
      if (*rpStreamBuffer) {
        rStreamBufferSet.markUserStatus(
            streamId, getNodeId(),
            IUsersManager::UserStatus::USED |
                IUsersManager::UserStatus::PRE_RELEASE);
        rStreamBufferSet.applyPreRelease(getNodeId());
      }
    }
#endif
    if (isInImageStream(streamId)) {
      (*rpImageBufferHeap)
          .reset((*rpStreamBuffer)->tryReadLock(getNodeName()),
                 [](IImageBufferHeap* p) { MY_LOGI("release implement"); });
    } else {
      (*rpImageBufferHeap)
          .reset((*rpStreamBuffer)->tryWriteLock(getNodeName()),
                 [](IImageBufferHeap* p) { MY_LOGI("release implement"); });
    }
    if (!(*rpImageBufferHeap)) {
      MY_LOGE("rpImageBufferHeap is NULL");
      return BAD_VALUE;
    }
  }

  //
  {
    std::shared_ptr<NSCam::v3::IImageStreamInfo> pYUVStreamInfo = nullptr;
    getStreamInfo(mpInYuv_main->getStreamId(), pFrame, &pYUVStreamInfo);
    size_t mainOffset = (*rpEncodeFrame)->exif.getHeaderSize();
    size_t mainMaxSize =
        (*rpImageBufferHeap)->getBufSizeInBytes(0) - mainOffset;

    MUINT32 transform = pYUVStreamInfo->getTransform();
    MSize imageSize =
        MSize(pYUVStreamInfo->getImgSize().w, pYUVStreamInfo->getImgSize().h);

    size_t const bufStridesInBytes[3] = {mainMaxSize, 0, 0};
    *rpImageBuffer = (*rpImageBufferHeap)
                         ->createImageBuffer_FromBlobHeap(
                             0, eImgFmt_JPEG, imageSize, bufStridesInBytes);

    if (!(*rpImageBuffer)) {
      (*rpStreamBuffer)->unlock(getNodeName(), (*rpImageBufferHeap).get());
      MY_LOGE("rpImageMainBuffer is NULL");
      return BAD_VALUE;
    }
    //  Query the group usage.
    MUINT const groupUsage = (*rpStreamBuffer)->queryGroupUsage(getNodeId());
    MBOOL ret = (*rpImageBuffer)->lockBuf(getNodeName(), groupUsage);
    if (!ret) {
      return BAD_VALUE;
    }

    MY_LOGD("stream buffer(%#" PRIx64
            ") %p, heap(0x%x): %p, buffer: %p, usage: %x, trans:%d, ori:%d, "
            "heapVA: %zx, bufferVA: %zx",
            streamId, (*rpStreamBuffer).get(),
            (*rpEncodeFrame)->mpOutImgBufferHeap->getImgFormat(),
            (*rpImageBufferHeap).get(), (*rpImageBuffer).get(), groupUsage,
            transform, (*rpEncodeFrame)->mParams.orientation,
            (*rpImageBufferHeap)->getBufVA(0), (*rpImageBuffer)->getBufVA(0));
  }
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
JpegNodeImp::errorHandle(std::shared_ptr<encode_frame> const& rpEncodeFrame) {
  MY_LOGE("Discard frameNo=%d", rpEncodeFrame->mpFrame->getRequestNo());

  MERROR err = BaseNode::flush(rpEncodeFrame->mpFrame);

  return err;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::unlockImage(
    std::shared_ptr<NSCam::v3::IImageStreamBuffer>* rpStreamBuffer,
    std::shared_ptr<IImageBuffer>* rpImageBuffer,
    std::shared_ptr<IImageBuffer>* rpImageBuffer1) {
  if ((*rpStreamBuffer) == nullptr || (*rpImageBuffer) == nullptr) {
    MY_LOGE("rpStreamBuffer %p, rpImageBuffer %p should not be NULL",
            (*rpStreamBuffer).get(), (*rpImageBuffer).get());
    return;
  }
  (*rpImageBuffer)->unlockBuf(getNodeName());
  if ((*rpImageBuffer1) != nullptr) {
    (*rpImageBuffer1)->unlockBuf(getNodeName());
  }
  (*rpStreamBuffer)
      ->unlock(getNodeName(), (*rpImageBuffer)->getImageBufferHeap().get());
}
/******************************************************************************
 *
 ******************************************************************************/
MVOID
JpegNodeImp::dumpYUVBuffer(MUINT32 const frameNo,
                           std::shared_ptr<IImageBuffer>* rpImageBuffer,
                           MUINT32 const idx) {
  char filename[256];
  snprintf(filename, sizeof(filename), "%s/Buffer_frame%d_%dx%d_%d.yuv",
           JPEG_DUMP_PATH, frameNo, (*rpImageBuffer)->getImgSize().w,
           (*rpImageBuffer)->getImgSize().h, idx);
  NSCam::Utils::saveBufToFile(filename,
                              (unsigned char*)(*rpImageBuffer)->getBufVA(0),
                              (*rpImageBuffer)->getBufSizeInBytes(0));
}

bool JpegNodeImp::convertToP411(std::shared_ptr<IImageBuffer> srcBuf,
                                void* dst) {
  int width = srcBuf->getImgSize().w;
  int height = srcBuf->getImgSize().h;
  int stride = srcBuf->getBufStridesInBytes(0);
  void* srcY = reinterpret_cast<void*>(srcBuf->getBufVA(0));

  void* srcUV =
      reinterpret_cast<unsigned char*>(srcBuf->getBufVA(0)) + stride * height;
  switch (srcBuf->getImgFormat()) {
    case eImgFmt_YUY2:
      YUY2ToP411(width, height, stride, srcY, dst);
      break;
    case eImgFmt_NV12:
      NV12ToP411Separate(width, height, stride, srcY, srcUV, dst);
      break;
    case eImgFmt_NV21:
      NV21ToP411Separate(width, height, stride, srcY, srcUV, dst);
      break;
    default:
      MY_LOGE("%s Unsupported format %d", __FUNCTION__, srcBuf->getImgFormat());
      return false;
  }
  return true;
}

// P411's Y, U, V are seperated. But the YUY2's Y, U and V are interleaved.
void JpegNodeImp::YUY2ToP411(
    int width, int height, int stride, void* src, void* dst) {
  int ySize = width * height;
  int cSize = width * height / 4;
  int wHalf = width >> 1;
  unsigned char* srcPtr = (unsigned char*)src;
  unsigned char* dstPtr = (unsigned char*)dst;
  unsigned char* dstPtrU = (unsigned char*)dst + ySize;
  unsigned char* dstPtrV = (unsigned char*)dst + ySize + cSize;

  for (int i = 0; i < height; i++) {
    // The first line of the source
    // Copy first Y Plane first
    for (int j = 0; j < width; j++) {
      dstPtr[j] = srcPtr[j * 2];
    }

    if (i & 1) {
      // Copy the V plane
      for (int k = 0; k < wHalf; k++) {
        dstPtrV[k] = srcPtr[k * 4 + 3];
      }
      dstPtrV = dstPtrV + wHalf;
    } else {
      // Copy the U plane
      for (int k = 0; k < wHalf; k++) {
        dstPtrU[k] = srcPtr[k * 4 + 1];
      }
      dstPtrU = dstPtrU + wHalf;
    }

    srcPtr = srcPtr + stride;
    dstPtr = dstPtr + width;
  }
}

// P411's Y, U, V are separated. But the NV12's U and V are interleaved.
void JpegNodeImp::NV12ToP411Separate(
    int width, int height, int stride, void* srcY, void* srcUV, void* dst) {
  int i, j, p, q;
  unsigned char* psrcY = (unsigned char*)srcY;
  unsigned char* pdstY = (unsigned char*)dst;
  unsigned char *pdstU, *pdstV;
  unsigned char* psrcUV;

  // copy Y data
  for (i = 0; i < height; i++) {
    STDCOPY(pdstY, psrcY, width);
    pdstY += width;
    psrcY += stride;
  }

  // copy U data and V data
  psrcUV = (unsigned char*)srcUV;
  pdstU = (unsigned char*)dst + width * height;
  pdstV = pdstU + width * height / 4;
  p = q = 0;
  for (i = 0; i < height / 2; i++) {
    for (j = 0; j < width; j++) {
      if (j % 2 == 0) {
        pdstU[p] = (psrcUV[i * stride + j] & 0xFF);
        p++;
      } else {
        pdstV[q] = (psrcUV[i * stride + j] & 0xFF);
        q++;
      }
    }
  }
}

// P411's Y, U, V are separated. But the NV21's U and V are interleaved.
void JpegNodeImp::NV21ToP411Separate(
    int width, int height, int stride, void* srcY, void* srcUV, void* dst) {
  int i, j, p, q;
  unsigned char* psrcY = (unsigned char*)srcY;
  unsigned char* pdstY = (unsigned char*)dst;
  unsigned char *pdstU, *pdstV;
  unsigned char* psrcUV;

  // copy Y data
  for (i = 0; i < height; i++) {
    STDCOPY(pdstY, psrcY, width);
    pdstY += width;
    psrcY += stride;
  }

  // copy U data and V data
  psrcUV = (unsigned char*)srcUV;
  pdstU = (unsigned char*)dst + width * height;
  pdstV = pdstU + width * height / 4;
  p = q = 0;
  for (i = 0; i < height / 2; i++) {
    for (j = 0; j < width; j++) {
      if ((j & 1) == 0) {
        pdstV[p] = (psrcUV[i * stride + j] & 0xFF);
        p++;
      } else {
        pdstU[q] = (psrcUV[i * stride + j] & 0xFF);
        q++;
      }
    }
  }
}
