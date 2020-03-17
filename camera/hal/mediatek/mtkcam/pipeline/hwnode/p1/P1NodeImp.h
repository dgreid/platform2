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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_P1_P1NODEIMP_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_P1_P1NODEIMP_H_

#define LOG_TAG "MtkCam/P1NodeImp"
//
#include "P1Common.h"
#include "P1Utility.h"
#include "P1ConnectLMV.h"
#include "P1TaskCtrl.h"
#include "P1DeliverMgr.h"
#include "P1RegisterNotify.h"

// MTKCAM/V4L2 with IPC usage
#if MTKCAM_HAVE_SANDBOX_SUPPORT
#include <mtkcam/v4l2/IPCIHalSensor.h>
#include <mtkcam/v4l2/V4L2HwEventMgr.h>
#include <mtkcam/v4l2/V4L2LensMgr.h>
#include <mtkcam/v4l2/V4L2P13ACallback.h>
#include <mtkcam/v4l2/V4L2SensorMgr.h>
#include <mtkcam/v4l2/V4L2SttPipeMgr.h>
#include <mtkcam/v4l2/V4L2TuningPipeMgr.h>
#endif
#include <TuningUtils/AccelerationDetector.h>

#include <atomic>
#include <list>
#include <memory>
#include <vector>
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
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

/******************************************************************************
 *
 ******************************************************************************/
class P1NodeImp : public BaseNode,
                  public P1Node,
                  public NS3Av3::IHal3ACb,
                  public std::enable_shared_from_this<P1NodeImp> {
  friend class P1NodeAct;
  friend class P1QueAct;
  friend class P1QueJob;
  friend class P1TaskCollector;
  friend class P1TaskCtrl;
  friend class P1DeliverMgr;
  friend class P1RegisterNotify;

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  Implementations.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////
  //
  typedef std::vector<P1QueJob> Que_T;
  //
  class Tag {
   public:
    Tag() : mInfo(0) { pthread_rwlock_init(&mLock, NULL); }
    ~Tag() { pthread_rwlock_destroy(&mLock); }
    void clear(void) {
      pthread_rwlock_wrlock(&mLock);
      mInfo = 0;
      pthread_rwlock_unlock(&mLock);
    }
    void set(MUINT32 info) {
      // for the performance consideration, set/get without locking, only for
      // log usage.
      mInfo = info;
    }
    MUINT32 get(void) {
      // for the performance consideration, set/get without locking, only for
      // log usage.
      return mInfo;
    }

   private:
    MUINT32 mInfo;
    mutable pthread_rwlock_t mLock;
  };
  //
  class Cfg {
   public:
    Cfg()
        : mSupportDynamicTwin(MFALSE),
          mSensorNum(NSCam::NSIoPipe::NSCamIOPipe::E_1_SEN),
          mQualityLv(eCamIQ_MAX),
          mPattern(0) {}
    ~Cfg() {}

   public:
    MBOOL mSupportDynamicTwin;
    NSCam::NSIoPipe::NSCamIOPipe::E_SEN mSensorNum;
    E_CamIQLevel mQualityLv;
    MUINT32 mPattern;
  };
  //
  class IndependentVerification {
   public:
    IndependentVerification(MINT32 nLogLevel,
                            MINT32 nLogLevelI,
                            MUINT32 ms,
                            std::weak_ptr<P1NodeImp> wpP1NodeImp)
        : mLock(),
          mLogLevel(nLogLevel),
          mLogLevelI(nLogLevelI),
          mExit(MFALSE),
          mCount(0),
          mIntervalMs(ms),
          mThread(),
          mwpP1NodeImp(wpP1NodeImp) {
      MY_LOGI("[P1_IV][CTR] BGN ms(%d) loglevel(%d %d)", mIntervalMs, mLogLevel,
              mLogLevelI);
      mThread = std::thread([this]() mutable { loop(); });
      MY_LOGI("[P1_IV][CTR] END ms(%d)", mIntervalMs);
    }

    virtual ~IndependentVerification() {
      MY_LOGI("[P1_IV][DTR] BGN cnt(%d)", mCount);
      {
        std::lock_guard<std::mutex> _l(mLock);
        mExit = MTRUE;
      }
      MY_LOGD("[P1_IV][DTR] JOIN cnt(%d)", mCount);
      if (mThread.joinable()) {
        mThread.join();
      }
      // mwpP1NodeImp = NULL;
      MY_LOGI("[P1_IV][DTR] END cnt(%d)", mCount);
    }

   private:
    MVOID loop(void) {
      MBOOL run = MFALSE;
      mCount = 0;
      char str[32] = {0};
      do {
        if (quit()) {
          break;
        }
        mCount++;
        run = exe();
        if (quit()) {
          break;
        }
        if (run) {
          snprintf(str, sizeof(str), "P1_IV:%d", mCount);
          P1_NOTE_SLEEP(str, mIntervalMs);
        }
        if (quit()) {
          break;
        }
      } while (run);
      return;
    }

    MBOOL quit(void) {
      std::lock_guard<std::mutex> _l(mLock);
      return mExit;
    }

    MBOOL exe(void);

   private:
    mutable std::mutex mLock;
    MINT32 mLogLevel;
    MINT32 mLogLevelI;
    MBOOL mExit;
    MUINT32 mCount;
    MUINT32 mIntervalMs;
    std::thread mThread;
    std::weak_ptr<P1NodeImp> mwpP1NodeImp;
  };
  friend class P1NodeImp::IndependentVerification;
  //
  enum START_CAP_STATE {
    START_CAP_STATE_NONE = 0,
    START_CAP_STATE_WAIT_REQ,
    START_CAP_STATE_WAIT_CB,
    START_CAP_STATE_READY
  };
  //
  enum IO_PIPE_EVT_STATE {
    IO_PIPE_EVT_STATE_NONE = 0,
    IO_PIPE_EVT_STATE_ACQUIRING,
    IO_PIPE_EVT_STATE_ACQUIRED  // after acquiring, wait for releasing
  };
  //
 protected:  ////                    Data Members. (Config)
  mutable pthread_rwlock_t mConfigRWLock;
  mutable std::mutex mInitLock;
  MBOOL mInit;

 protected:  ////                    Data Members. (Config)
  mutable std::mutex mPowerNotifyLock;
  MBOOL mPowerNotify;

 protected:  ////                    Data Members. (Config)
  mutable std::mutex mStartStateLock;
  MUINT8 mStartState;

  std::vector<StreamId_T> mInStreamIds;
  std::shared_ptr<IMetaStreamInfo> mvStreamMeta[STREAM_META_NUM];
  std::shared_ptr<IImageStreamInfo> mvStreamImg[STREAM_IMG_NUM];
  char maStreamMetaName[STREAM_META_NUM][P1_STREAM_NAME_LEN] = {
      {"InAPP"},   // STREAM_META_IN_APP
      {"InHAL"},   // STREAM_META_IN_HAL
      {"OutAPP"},  // STREAM_META_OUT_APP
      {"OutHAL"}   // STREAM_META_OUT_HAL
  };
  char maStreamImgName[STREAM_IMG_NUM][P1_STREAM_NAME_LEN] = {
      {"InYUV"},   // STREAM_IMG_IN_YUV
      {"InOPQ"},   // STREAM_IMG_IN_OPAQUE
      {"OutOPQ"},  // STREAM_IMG_OUT_OPAQUE
      {"OutIMG"},  // STREAM_IMG_OUT_FULL
      {"OutRRZ"},  // STREAM_IMG_OUT_RESIZE
      {"OutLCS"},  // STREAM_IMG_OUT_LCS
      {"OutRSS"}   // STREAM_IMG_OUT_RSS
  };
  //
  SensorParams mSensorParams;
  std::shared_ptr<IImageStreamBufferPoolT> mpStreamPool_full;
  std::shared_ptr<IImageStreamBufferPoolT> mpStreamPool_resizer;
  std::shared_ptr<IImageStreamBufferPoolT> mpStreamPool_lcso;
  std::shared_ptr<IImageStreamBufferPoolT> mpStreamPool_rsso;
  MUINT8 mBurstNum;
  MUINT8 mDepthNum;
  MUINT32 mMeta_PatMode;

  MBOOL mRawPostProcSupport;
  MBOOL mRawProcessed;
  RAW_DEF_TYPE mRawSetDefType;

  /**
   * the raw default type, if the request do not set the raw type,
   * it will use this setting to driver
   */
  MUINT32 mRawDefType;

  /**
   * the raw type option means the capability recorded in each bit,
   * it is decided after the driver configuration
   */
  MUINT32 mRawOption;
  MBOOL mDisableFrontalBinning;
  MBOOL mDisableDynamicTwin;

  MBOOL mEnableEISO;
  MBOOL mForceSetEIS;
  MUINT64 mPackedEisInfo;
  MBOOL mEnableLCSO;
  MBOOL mEnableRSSO;
  MBOOL mEnableUniForcedOn;

  MBOOL mDisableHLR;    // true:force-off false:auto
  PIPE_MODE mPipeMode;  // (EPipeSelect)
  MUINT32 mPipeBit;     // (E_CAM_PipelineBitDepth_SEL)

  NSCam::IMetadata mCfgAppMeta;
  NSCam::IMetadata mCfgHalMeta;

  RESIZE_QUALITY mResizeQuality;

  MUINT8 mTgNum;

  MINT mRawFormat;
  MUINT32 mRawStride;
  MUINT32 mRawLength;

  REV_MODE mReceiveMode;
  MUINT mSensorFormatOrder;
  mutable std::mutex mQualitySwitchLock;
  MBOOL mQualitySwitching;

  LongExposureStatus mLongExp;
  // Storage                       mImageStorage;

 protected:  ////                    Data Members. (System capability)
  static const int mNumInMeta = 2;
  static const int mNumOutMeta = 3;
  int m3AProcessedDepth;
  int mNumHardwareBuffer;
  int mDelayframe;

 protected:  ////
  MUINT32 mLastNum;
  mutable std::mutex mLastNumLock;
  MUINT32 mLastSofIdx;
  MINT32 mLastSetNum;

 protected:  ////                    Data Members. (Hardware)
  mutable std::mutex mHardwareLock;
  mutable std::mutex mStopSttLock;
  mutable std::mutex mActiveLock;
  MBOOL mActive;
  mutable std::mutex mReadyLock;
  MBOOL mReady;
  //
#if (USING_DRV_IO_PIPE_EVENT)
  IO_PIPE_EVT_STATE mIoPipeEvtState;
  mutable pthread_rwlock_t mIoPipeEvtStateLock;
  //
  mutable std::mutex mIoPipeEvtWaitLock;
  std::condition_variable mIoPipeEvtWaitCond;
  MBOOL mIoPipeEvtWaiting;
  //
  mutable std::mutex mIoPipeEvtOpLock;
  MBOOL mIoPipeEvtOpAcquired;
  MBOOL mIoPipeEvtOpLeaving;
  std::shared_ptr<IoPipeEventHandle> mspIoPipeEvtHandleAcquire;
  std::shared_ptr<IoPipeEventHandle> mspIoPipeEvtHandleRelease;
  //
#endif
  MUINT32 mCamIOVersion;
  std::shared_ptr<V4L2IIOPipe> mpCamIO;
  std::shared_ptr<IHal3A_T> mp3A;
  LcsHal* mpLCS;
  std::unique_ptr<cros::NSCam::AccelerationDetector> mpAccDetector;
  //
  Cfg mCfg;
  //
  MRect mActiveArray;
  MUINT32 mPixelMode;
  //
  MUINT32 mConfigPort;
  MUINT32 mConfigPortNum;
  MBOOL mIsBinEn;
  MBOOL mIsDynamicTwinEn;
  MBOOL mIsLegacyStandbyMode;
  MINT8 mForceStandbyMode;
  //
  MINT32 mResizeRatioMax;
  //
  mutable std::mutex mCurBinLock;
  MSize mCurBinSize;
  //
  std::weak_ptr<INodeCallbackToPipeline> mwpPipelineCb;
  mutable std::mutex mPipelineCbLock;
  mutable std::mutex mLastFrmReqNumLock;
  MINT32 mLastFrmNum;
  MINT32 mLastReqNum;
  MINT32 mLastCbCnt;
  //
  MINT64 mMonitorTime;
  mutable std::mutex mMonitorLock;
  //
  StuffBufferManager mStuffBufMgr;
  //
#define DRAWLINE_PORT_RRZO 0x1
#define DRAWLINE_PORT_IMGO 0x2
  MUINT32 mDebugScanLineMask;
  std::unique_ptr<DebugScanLine> mpDebugScanLine;
  //
  MUINT32 mIvMs;
  std::unique_ptr<P1NodeImp::IndependentVerification> mpIndependentVerification;

 protected:  ////                    Data Members. (Queue: Request)
  mutable std::mutex mRequestQueueLock;
  Que_T mRequestQueue;

#if USING_CTRL_3A_LIST_PREVIOUS
  std::list<MetaSet_T> mPreviousCtrlList;
#endif
  //
  mutable std::mutex mFrameSetLock;
  MBOOL mFrameSetAlready;
  //
  MBOOL mFirstReceived;
  //
  mutable std::mutex mStartCaptureLock;
  std::condition_variable mStartCaptureCond;
  START_CAP_STATE mStartCaptureState;
  MUINT32 mStartCaptureType;
  MUINT32 mStartCaptureIdx;
  MINT64 mStartCaptureExp;

 protected:  ////
 protected:  ////                    Data Members. (Queue: Processing)
  mutable std::mutex mProcessingQueueLock;
  std::condition_variable mProcessingQueueCond;
  Que_T mProcessingQueue;

 protected:  ////                    Data Members. (Queue: drop)
  mutable std::mutex mDropQueueLock;
  std::vector<MINT32> mDropQueue;

 protected:  ////                    Data Members.
  mutable std::mutex mTransferJobLock;
  std::condition_variable mTransferJobCond;
  MINT32 mTransferJobIdx;
  MBOOL mTransferJobWaiting;

 protected:  ////                    Data Members.
  mutable std::mutex mStartLock;
  std::condition_variable mStartCond;

 protected:  ////                    Data Members.
  mutable std::mutex mThreadLock;
  std::condition_variable mThreadCond;

 protected:  ////                    Data Members.
  DurationProfile mDequeThreadProfile;

 protected:  ////                    Data Members.
  mutable std::mutex mPublicLock;

 protected:  ////                    Data Members.
  std::atomic_int mInFlightRequestCnt;

 protected:  ////                    Data Members.
  std::shared_ptr<P1DeliverMgr> mpDeliverMgr;

 protected:  ////                    Data Members.
  std::shared_ptr<P1RegisterNotify> mpRegisterNotify;

 protected:  ////                    Data Members.
  std::shared_ptr<P1TaskCtrl> mpTaskCtrl;

 protected:  ////                    Data Members.
  std::shared_ptr<P1TaskCollector> mpTaskCollector;

 protected:  ////                    Data Members.
  std::shared_ptr<P1ConnectLMV> mpConnectLMV;

 protected:  ////                    Data Members.
  std::shared_ptr<ConcurrenceControl> mpConCtrl;

 protected:  ////                    Data Members.
  std::shared_ptr<HardwareStateControl> mpHwStateCtrl;

 protected:  ////                    Data Members.
  std::shared_ptr<TimingCheckerMgr> mpTimingCheckerMgr;
  MUINT32 mTimingFactor;

 protected:  ////                    Data Members.
  std::shared_ptr<NSCam::v3::Utils::Imp::ISyncHelper> mspSyncHelper;
  mutable std::mutex mSyncHelperLock;
  MBOOL mSyncHelperReady;

 protected:  ////                    Data Members.
  std::shared_ptr<IResourceConcurrency> mspResConCtrl;
  IResourceConcurrency::CLIENT_HANDLER mResConClient;
  MBOOL mIsResConGot;

 protected:  ////                    Data Members.
  LogInfo mLogInfo;

 protected:
  MINT32 mLogLevel;
  MINT32 mLogLevelI;
  MINT32 mSysLevel;
  MINT32 mMetaLogOp;
  MUINT32 mMetaLogTag;
  MINT32 mCamDumpEn;
  MINT32 mEnableDumpRaw;
  MINT32 mDisableAEEIS;
  Tag mTagReq;
  Tag mTagSet;
  Tag mTagEnq;
  Tag mTagDeq;
  Tag mTagOut;
  Tag mTagList;
  //
  FrameNote mNoteRelease;
  FrameNote mNoteDispatch;

 protected:             ////                    Data Members.
  MUINT32 mInitReqSet;  // the request set from user
  MUINT32 mInitReqNum;  // the total number need to receive
  MUINT32 mInitReqCnt;  // the currently received count
  MBOOL mInitReqOff;    // the initial request flow disable

 protected:  ////                    Data Members.
  MBOOL mEnableCaptureFlow;
  MBOOL mEnableFrameSync;
  MBOOL mExitPending;
  std::thread mThread;

#if MTKCAM_HAVE_SANDBOX_SUPPORT
 protected:  ////                    Data Members. (V4L2 IPC usage)
  std::shared_ptr<v4l2::V4L2LensMgr> mpV4L2LensMgr;

  std::shared_ptr<v4l2::V4L2SensorWorker> mpV4L2SensorMgr;

  std::shared_ptr<v4l2::V4L2HwEventWorker> mpV4L2HwEventMgr[3];  // 3

  std::shared_ptr<v4l2::V4L2P13ACallback> mpV4L2P13ACallback;

  std::shared_ptr<v4l2::V4L2TuningPipeMgr> mpV4L2TuningPipe;

  std::shared_ptr<v4l2::V4L2SttPipeMgr> mpV4L2SttPipe;
#endif

 protected:  ////                    Operations.
  MVOID setActive(MBOOL active);

  MBOOL getActive(void);

  MVOID setReady(MBOOL ready);

  MBOOL getReady(void);

  MVOID setInit(MBOOL init);

  MBOOL getInit(void);

  MVOID setPowerNotify(MBOOL notify);

  MBOOL getPowerNotify(void);

  MVOID setStartState(MUINT8 state);

  MUINT8 getStartState(void);

  MVOID setQualitySwitching(MBOOL switching);

  MBOOL getQualitySwitching(void);

  MVOID setCurrentBinSize(MSize size);

  MSize getCurrentBinSize(void);

  MVOID lastFrameRequestInfoUpdate(MINT32 const frameNum,
                                   MINT32 const requestNum);

  MINT32 lastFrameRequestInfoNotice(MINT32* frameNum,
                                    MINT32* requestNum,
                                    MINT32 const addCbCnt = 0);

  MVOID syncHelperStart(void);

  MVOID syncHelperStop(void);

  MVOID ensureStartReady(MUINT8 infoType,
                         MINT32 infoNum = P1_MAGIC_NUM_INVALID);

  MVOID onRequestFrameSet(MBOOL initial = MFALSE);

  MVOID setRequest(MBOOL initial = MFALSE);

  MBOOL acceptRequest(std::shared_ptr<IPipelineFrame> pFrame,
                      MUINT32* rRevResult);

  MBOOL beckonRequest(void);

  MBOOL checkReqCnt(MINT32* cnt);

  MVOID onSyncEnd(void);

  MVOID onSyncBegin(MBOOL initial,
                    NS3Av3::RequestSet_T* reqSet = nullptr,  // magicNum = 0,
                    MUINT32 sofIdx = P1SOFIDX_INIT_VAL,
                    NS3Av3::CapParam_T* capParam = nullptr);

  MERROR fetchJob(P1QueJob* rOutJob);
  MERROR onProcessEnqueFrame(P1QueJob* job);

  MERROR onProcessDequeFrame();

  MERROR onProcessDropFrame(MBOOL isTrigger = MFALSE);

  MVOID onCheckDropFrame(void);

  MBOOL getProcessingFrame_ByAddr(IImageBuffer* const imgBuffer,
                                  MINT32 magicNum,
                                  P1QueJob* job);

  P1QueJob getProcessingFrame_ByNumber(MINT32 magicNum);

  MVOID onHandleFlush(MBOOL wait, MBOOL isInitReqOff = MFALSE);

  MVOID processRedoFrame(P1QueAct* rAct);

  MVOID processYuvFrame(P1QueAct* rAct);

  MVOID onReturnFrame(P1QueAct* rAct,
                      FLUSH_TYPE flushType,
                      MBOOL isTrigger = MTRUE);

  MVOID releaseAction(P1QueAct* rAct);

  MVOID releaseFrame(P1FrameAct* rFrameAct);

  MVOID onProcessResult(P1QueAct* rAct,
                        QBufInfo const& deqBuf,
                        NS3Av3::MetaSet_T const& result3A,
                        IMetadata const& resultAppend,
                        MUINT32 const index = 0);

  MBOOL findPortBufIndex(QBufInfo const& deqBuf, P1QueJob* job);

  MVOID createAction(P1QueAct* rAct,
                     std::shared_ptr<IPipelineFrame> appFrame = nullptr,
                     REQ_TYPE eType = REQ_TYPE_UNKNOWN);

#if (USING_DRV_IO_PIPE_EVENT)
  MVOID eventStreamingInform();

  MVOID eventStreamingOn();

  MVOID eventStreamingOff();
#endif

 protected:  ////                    Hardware Operations.
  MERROR hardwareOps_start();

  MERROR hardwareOps_enque(P1QueJob* job,
                           ENQ_TYPE type = ENQ_TYPE_NORMAL,
                           MINT64 data = 0);

  MERROR hardwareOps_deque(QBufInfo* deqBuf);

  MERROR hardwareOps_stop();

  MERROR hardwareOps_request();

  MERROR hardwareOps_capture();

  MERROR hardwareOps_streaming();

  MERROR procedureAid_start();

  MERROR buildInitItem();

  MERROR setupAction(P1QueAct* act, QBufInfo* info);

  MERROR createStuffBuffer(std::shared_ptr<IImageBuffer>* imageBuffer,
                           std::shared_ptr<IImageStreamInfo> const& streamInfo,
                           NSCam::MSize::value_type const changeHeight = 0);

  MERROR createStuffBuffer(std::shared_ptr<IImageBuffer>* imageBuffer,
                           char const* szName,
                           MINT32 format,
                           MSize size,
                           std::vector<MUINT32> vStride);

  MERROR destroyStuffBuffer(std::shared_ptr<IImageBuffer>* imageBuffer);

  MVOID generateAppMeta(P1QueAct* act,
                        NS3Av3::MetaSet_T const& result3A,
                        QBufInfo const& deqBuf,
                        IMetadata* appMetadata,
                        MUINT32 const index = 0);

  MVOID generateAppTagIndex(IMetadata* appMetadata, IMetadata* appTagIndex);

  MVOID generateHalMeta(P1QueAct* act,
                        NS3Av3::MetaSet_T const& result3A,
                        QBufInfo const& deqBuf,
                        IMetadata const& resultAppend,
                        IMetadata const& inHalMetadata,
                        IMetadata* halMetadata,
                        MUINT32 const index = 0);

#if USING_CTRL_3A_LIST
  MVOID generateCtrlList(std::list<MetaSet_T>* pList, P1QueJob* rJob);
#endif

#if SUPPORT_LCS
  MERROR lcsInit();
#endif

#if SUPPORT_3A
  MERROR getAEInitExpoSetting(NS3Av3::AEInitExpoSetting_T* initExpoSetting);
#endif

#if MTKCAM_HAVE_SANDBOX_SUPPORT
  MVOID v4l2DeviceStart();
#endif
  MVOID addConfigPort(std::vector<PortInfo>* vPortInfo,
                      EImageFormat* resizer_fmt);
  MERROR lmvInit(MSize sensorSize, MSize rrzoSize);
  QInitParam prepareQInitParam(IHalSensor::ConfigParam* sensorCfg,
                               NS3Av3::AEInitExpoSetting_T initExpoSetting,
                               std::vector<PortInfo> vPortInfo);
  MERROR startCamIO(
      QInitParam halCamIOinitParam,
      MSize* binInfoSize,
      MSize rawSize[2],
      PipeTag* pipe_tag,
      std::map<int, std::vector<std::shared_ptr<IImageBuffer>>>* buffers);
  MVOID generateCtrlQueue(std::vector<NS3Av3::MetaSet_T*>* rQue,
                          P1QueJob* rJob);

  MVOID prepareCropInfo(P1QueAct* rAct,
                        IMetadata* pAppMetadata,
                        IMetadata* pHalMetadata,
                        PREPARE_CROP_PHASE phase,
                        MBOOL* pCtrlFlush = nullptr);

  MERROR check_config(ConfigParams const& rParams);

  MERROR checkConstraint(void);

  MERROR attemptCtrlSync(P1QueAct* rAct);

  MERROR attemptCtrlSetting(P1QueAct* rAct);

  MERROR attemptCtrlResize(P1QueAct* rAct, MBOOL* rIsChanged);

  MERROR attemptCtrlReadout(P1QueAct* rAct,
                            IMetadata* pAppMetadata,
                            IMetadata* pHalMetadata,
                            MBOOL* rIsChanged);

  MERROR notifyCtrlSync(P1QueAct* rAct);

  MERROR notifyCtrlMeta(IPipelineNodeCallback::eCtrlType eType,
                        P1QueAct* rAct,
                        STREAM_META const streamAppMeta,
                        IMetadata* pAppMetadata,
                        STREAM_META const streamHalMeta,
                        IMetadata* pHalMetadata,
                        MBOOL* rIsChanged);

  MERROR requestMetadataEarlyCallback(P1QueAct* act,
                                      STREAM_META const streamMeta,
                                      IMetadata* pMetadata);

  MVOID checkBufferDumping(P1QueAct* rAct);

  MVOID inflightMonitoring(INFLIGHT_MONITORING_TIMING timing = IMT_COMMON);

  MUINT32 get_and_increase_magicnum();
  MUINT32 get_last_magicnum() {
    std::lock_guard<std::mutex> _l(mLastNumLock);
    MUINT32 ret = (mLastNum > 0) ? (mLastNum - 1) : 0;
    return ret;
  }

  MBOOL isRevMode(REV_MODE mode) {
    return (mode == mReceiveMode) ? (MTRUE) : (MFALSE);
  }

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 private:
  MVOID dispatch(std::shared_ptr<IPipelineFrame> pFrame);

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  Operations in base class Thread
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////
  virtual void requestExit();

  // Good place to do one-time initializations
  virtual status_t readyToRun();

 private:
  virtual bool threadLoop();
  virtual bool _threadLoop();
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  Interface.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////                    Operations.
  P1NodeImp();
  virtual ~P1NodeImp();
  virtual MERROR config(ConfigParams const& rParams);

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  IPipelineNode Interface.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////                    Operations.
  virtual MERROR init(InitParams const& rParams);

  virtual MERROR uninit();

  virtual MERROR flush();

  virtual MERROR flush(std::shared_ptr<IPipelineFrame> const& pFrame);

  virtual MERROR queue(std::shared_ptr<IPipelineFrame> pFrame);

  virtual MERROR kick();

  virtual MERROR setNodeCallBack(
      std::weak_ptr<INodeCallbackToPipeline> pCallback);

 public:  ////                    Operations.
  virtual void doNotifyCb(MINT32 _msgType,
                          MINTPTR _ext1,
                          MINTPTR _ext2,
                          MINTPTR _ext3);

  static void doNotifyDropframe(MUINT magicNum, void* cookie);

#if (USING_DRV_IO_PIPE_EVENT)
  static NSCam::NSIoPipe::IoPipeEventCtrl onEvtCtrlAcquiring(
      P1NodeImp* user, NSCam::NSIoPipe::IpRawP1AcquiringEvent* evt);
  static NSCam::NSIoPipe::IoPipeEventCtrl onEvtCtrlReleasing(
      P1NodeImp* user, NSCam::NSIoPipe::IpRawP1ReleasedEvent* evt);
#endif

#if MTKCAM_HAVE_SANDBOX_SUPPORT
 public:
  int setDynamicSensorInfoToIPCHalSensor(size_t sensorIdx);

  // set P1 dynamic info to IPCHalSensor
  int setDynamicInfoExToIPCHalSensor(size_t sensorIdx,
                                     const IIPCHalSensor::DynamicInfo& info);
#endif
};

};  // namespace NSP1Node
};  // namespace v3
};  // namespace NSCam

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_P1_P1NODEIMP_H_
