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

#define LOG_TAG "MtkCam/P1NodeImp"
//
#include <algorithm>
#include <memory>
#include "mtkcam/pipeline/hwnode/p1/P1NodeImp.h"
#include "P1TaskCtrl.h"
#include "P1DeliverMgr.h"
#include <mtkcam/aaa/aaa_utils.h>
#include <mtkcam/feature/eis/EisInfo.h>
#include <mtkcam/utils/hw/HwTransform.h>
#include <property_lib.h>
#include <string>
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
P1NodeImp::P1NodeImp()
    : BaseNode(),
      P1Node(),
      mInit(MTRUE)
      //
      ,
      mPowerNotify(MFALSE)
      //
      ,
      mStartState(NSP1Node::START_STATE_NULL)
      //
      ,
      mpStreamPool_full(nullptr),
      mpStreamPool_resizer(nullptr),
      mpStreamPool_lcso(nullptr),
      mpStreamPool_rsso(nullptr),
      mBurstNum(1),
      mDepthNum(1),
      mMeta_PatMode(0),
      mRawPostProcSupport(MTRUE),
      mRawProcessed(MFALSE),
      mRawSetDefType(RAW_DEF_TYPE_AUTO),
      mRawDefType(EPipe_PURE_RAW),
      mRawOption(0),
      mDisableFrontalBinning(MFALSE),
      mDisableDynamicTwin(MFALSE),
      mEnableLCSO(MFALSE),
      mEnableRSSO(MFALSE),
      mEnableUniForcedOn(MFALSE),
      mDisableHLR(MFALSE),
      mPipeMode(PIPE_MODE_NORMAL),
      mPipeBit(CAM_Pipeline_12BITS),
      mResizeQuality(RESIZE_QUALITY_UNKNOWN),
      mTgNum(0)
      //
      ,
      mRawFormat(P1_IMGO_DEF_FMT),
      mRawStride(0),
      mRawLength(0)
      //
      ,
      mReceiveMode(REV_MODE_NORMAL),
      mSensorFormatOrder(SENSOR_FORMAT_ORDER_NONE),
      mQualitySwitching(MFALSE),
      m3AProcessedDepth(3),
      mNumHardwareBuffer(3),
      mDelayframe(3),
      mLastNum(1),
      mLastSofIdx(P1SOFIDX_NULL_VAL),
      mLastSetNum(0),
      mActive(MFALSE),
      mReady(MFALSE)
#if (USING_DRV_IO_PIPE_EVENT)
      ,
      mIoPipeEvtState(IO_PIPE_EVT_STATE_NONE),
      mIoPipeEvtWaiting(MFALSE),
      mIoPipeEvtOpAcquired(MFALSE),
      mIoPipeEvtOpLeaving(MFALSE),
      mspIoPipeEvtHandleAcquire(nullptr),
      mspIoPipeEvtHandleRelease(nullptr)
#endif
      ,
      mCamIOVersion(0),
      mpCamIO(nullptr),
      mp3A(nullptr),
      mpLCS(nullptr)
      //
      ,
      mPixelMode(0)
      //
      ,
      mConfigPort(CONFIG_PORT_NONE),
      mConfigPortNum(0),
      mIsBinEn(MFALSE),
      mIsDynamicTwinEn(MFALSE),
      mIsLegacyStandbyMode(MFALSE),
      mForceStandbyMode(0)
      //
      ,
      mResizeRatioMax(RESIZE_RATIO_MAX_100X)
      //
      ,
      mLastFrmReqNumLock(),
      mLastFrmNum(P1_FRM_NUM_NULL),
      mLastReqNum(P1_REQ_NUM_NULL),
      mLastCbCnt(0)
      //
      ,
      mMonitorTime(0)
      //
      ,
      mDebugScanLineMask(0),
      mpDebugScanLine(nullptr)
      //
      ,
      mIvMs(0),
      mpIndependentVerification(nullptr)
      //
      ,
      mFrameSetAlready(MFALSE)
      //
      ,
      mFirstReceived(MFALSE)
      //
      ,
      mStartCaptureState(START_CAP_STATE_NONE),
      mStartCaptureType(NS3Av3::E_CAPTURE_NORMAL),
      mStartCaptureIdx(0),
      mStartCaptureExp(0),
      mTransferJobIdx(P1ACT_ID_NULL),
      mTransferJobWaiting(MFALSE)
      //
      //
      ,
      mDequeThreadProfile("P1Node::deque", 30000000LL),
      mInFlightRequestCnt(0)
      //
      ,
      mpDeliverMgr(nullptr)
      //
      ,
      mpConnectLMV(NULL)
      //
      ,
      mpConCtrl(nullptr)
      //
      ,
      mpHwStateCtrl(nullptr)
      //
      ,
      mpTimingCheckerMgr(nullptr),
      mTimingFactor(1)
      //
      ,
      mspSyncHelper(nullptr),
      mSyncHelperReady(MFALSE)
      //
      ,
      mspResConCtrl(nullptr),
      mResConClient(IResourceConcurrency::CLIENT_HANDLER_NULL),
      mIsResConGot(MFALSE)
      //
      //
      ,
      mLogLevel(0),
      mLogLevelI(0),
      mSysLevel(P1_SYS_LV_DEFAULT),
      mMetaLogOp(0),
      mMetaLogTag(0),
      mCamDumpEn(0),
      mEnableDumpRaw(0),
      mDisableAEEIS(0)
      //
      ,
      mNoteRelease(P1NODE_FRAME_NOTE_SLOT_SIZE_DEF),
      mNoteDispatch(P1NODE_FRAME_NOTE_SLOT_SIZE_DEF)
      //
      ,
      mInitReqSet(0),
      mInitReqNum(0),
      mInitReqCnt(0),
      mInitReqOff(MFALSE)
      //
      ,
      mEnableCaptureFlow(MFALSE),
      mEnableFrameSync(MFALSE),
      mExitPending(MFALSE) {
  pthread_rwlock_init(&mConfigRWLock, NULL);
#if (USING_DRV_IO_PIPE_EVENT)
  pthread_rwlock_init(&mIoPipeEvtStateLock, NULL);
#endif
  mNodeName = "P1Node";  // default name
  MINT32 cam_log = ::property_get_int32("vendor.debug.camera.log", 0);
  MINT32 p1_log = ::property_get_int32("vendor.debug.camera.log.p1node", 1);
  MINT32 p1_logi = ::property_get_int32("vendor.debug.camera.log.p1nodei", 0);
  MINT32 g_log = ::property_get_int32("persist.vendor.mtk.camera.log_level",
                                      0);  // global log level control
  MINT32 g_log_lv =
      (g_log >= 2) ? (g_log - 2) : (0);  // 2:I:USER 3:D:USERDEBUG 4:V:ENG
  mLogLevel = MAX(cam_log, p1_log);
#if 0  // force to enable all p1 node log
#warning "[FIXME] force to enable P1Node log"
    if (mLogLevel < 2) {
        mLogLevel = 2;
    }
#endif
  MBOOL buildLogD = MFALSE;
  MBOOL buildLogI = MFALSE;
#if (IS_P1_LOGI)
  // #warning "IS_P1_LOGI build LogI"
  mLogLevelI = (mLogLevel > 0) ? (mLogLevel - 1) : (mLogLevel);
  buildLogI = MTRUE;
#endif
#if (IS_P1_LOGD)
  // #warning "IS_P1_LOGD build LogD"
  mLogLevelI = mLogLevel;
  buildLogD = MTRUE;
#endif
  if (p1_log > 1) {
    mLogLevelI = mLogLevel;
  }
  //
  if (p1_logi > 0) {
    mLogLevelI = p1_logi;
  }
  mLogLevel = MAX(mLogLevel, g_log_lv);
  mLogLevelI = MAX(mLogLevelI, g_log_lv);
  //
  MINT32 g_sys = P1_SYS_LV_DEFAULT;
  MINT32 g_sys_set = 1;
#if 1  // decide by global-setting
  g_sys_set = ::property_get_int32("vendor.debug.mtkcam.systrace.level",
                                   MTKCAM_SYSTRACE_LEVEL_DEFAULT);
#endif
  g_sys = (g_sys_set > 0) ? P1_SYS_LV_DEFAULT : P1_SYS_LV_CRITICAL;
  mSysLevel = g_sys;
  MINT32 p1sys = ::property_get_int32("vendor.debug.camera.log.p1nodesys", 9);
  if (p1sys < 9) {  // update by manual-setting
    // =0 : forced-off all P1_TRACE
    // =1 : basic-on P1_TRACE-Lv==1 (P1_SYS_LV_BASIC)
    // =2 : critical-on P1_TRACE-Lv<=2 (P1_SYS_LV_CRITICAL)
    // =3 : default-on P1_TRACE-Lv<=3 (P1_SYS_LV_DEFAULT)
    // >3 : manually-on
    mSysLevel = p1sys;
  }
  //
  MINT32 pMetaLogOp =
      property_get_int32("vendor.debug.camera.log.p1nodemeta", 0);
  MINT32 pMetaLogTag =
      property_get_int32("vendor.debug.camera.log.p1nodemetatag", 0);
  mMetaLogOp = pMetaLogOp;
  mMetaLogTag = pMetaLogTag;
  if (mMetaLogTag != 0) {
    mMetaLogOp = 1;
  }
  //
  mCamDumpEn = property_get_int32("vendor.debug.camera.dump.en", 0);
  //
  mEnableDumpRaw =
      property_get_int32("vendor.debug.feature.forceEnableIMGO", 0);
  //
  mDisableAEEIS = property_get_int32("vendor.debug.eis.disableae", 0);
  //
  mDebugScanLineMask =
      ::property_get_int32("vendor.debug.camera.scanline.p1", 0);
  if (mDebugScanLineMask != 0) {
    mpDebugScanLine = std::make_unique<DebugScanLineImp>();
  }
  //
  mIvMs = property_get_int32(
      "vendor.debug.camera.log.p1independentverification", 0);
  //
#if (SUPPORT_BUFFER_TUNING_DUMP)
  MY_LOGI("SUPPORT_BUFFER_TUNING_DUMP CamDumpEn(%d)", mCamDumpEn);
#else
  if (mCamDumpEn > 0) {
    MY_LOGI("NOT-SUPPORT_BUFFER_TUNING_DUMP CamDumpEn(%d)", mCamDumpEn);
  }
  mCamDumpEn = 0;
#endif
  //
#if (P1NODE_BUILD_LOG_LEVEL_DEFAULT > 3)
  mTimingFactor = 32;  // for ENG build
#elif P1NODE_BUILD_LOG_LEVEL_DEFAULT > 2
  mTimingFactor = 2;  // for USERDEBUG build
#else
  mTimingFactor = 1;  // for USER build
  mIvMs = 0;
#endif
  //
  MY_LOGI(
      "LOGD[%d](%d) LOGI[%d](%d) prop(cam:%d pl:%d pi:%d g:%d:%d) - "
      " SYS[%d-%d:%d](%d) - "
      "MetaLog(p:%d/%d m:%d/x%X) DumpRaw(%d) DataDump(%d) DrawLine(%d) - "
      "TF(%d) - IV(%d)",
      buildLogD, mLogLevel, buildLogI, mLogLevelI, cam_log, p1_log, p1_logi,
      g_log, g_log_lv, g_sys_set, g_sys, p1sys, mSysLevel, pMetaLogOp,
      pMetaLogTag, mMetaLogOp, mMetaLogTag, mEnableDumpRaw, mCamDumpEn,
      mDebugScanLineMask, mTimingFactor, mIvMs);
}

/******************************************************************************
 *
 ******************************************************************************/
P1NodeImp::~P1NodeImp() {
  MY_LOGI("");
  pthread_rwlock_destroy(&mConfigRWLock);
#if (USING_DRV_IO_PIPE_EVENT)
  pthread_rwlock_destroy(&mIoPipeEvtStateLock);
#endif
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::init(InitParams const& rParams) {
  FUNCTION_IN;

  P1_TRACE_AUTO(SLG_B, "P1:init");

  std::lock_guard<std::mutex> _l(mPublicLock);

  {
    pthread_rwlock_wrlock(&mConfigRWLock);
    //
    mOpenId = rParams.openId;
    mNodeId = rParams.nodeId;
    mNodeName = rParams.nodeName;
    pthread_rwlock_unlock(&mConfigRWLock);
  }
  //
  if (mIvMs > 0) {
    mpIndependentVerification =
        std::make_unique<P1NodeImp::IndependentVerification>(
            mLogLevel, mLogLevelI, (MUINT32)mIvMs, shared_from_this());
    if (mpIndependentVerification == nullptr) {
      MY_LOGE("IndependentVerification create fail");
      return NO_MEMORY;
    }
  }
  //  Select CamIO version
  {
    auto pModule = getNormalPipeModule();

    if (!pModule) {
      MY_LOGE("getNormalPipeModule() fail");
      return UNKNOWN_ERROR;
    }

    MUINT32 const* version = nullptr;
    size_t count = 0;
    int err = pModule->get_sub_module_api_version(&version, &count);
    if (err < 0 || !count || !version) {
      MY_LOGE(
          "[%d] INormalPipeModule::get_sub_module_api_version - err:%#x "
          "count:%zu version:%p",
          mOpenId, err, count, version);
      return UNKNOWN_ERROR;
    }

    mCamIOVersion = *(version + count - 1);  // Select max. version
    MY_LOGD("[%d] count:%zu Selected CamIO Version:%0#x", mOpenId, count,
            mCamIOVersion);
  }

#if (USING_DRV_IO_PIPE_EVENT)
  {
    std::lock_guard<std::mutex> _l(mIoPipeEvtOpLock);
    mIoPipeEvtOpLeaving = MFALSE;
    NSCam::NSIoPipe::IoPipeEventSystem& evtSystem =
        NSCam::NSIoPipe::IoPipeEventSystem::getGlobal();
    if (mspIoPipeEvtHandleAcquire != nullptr) {
      mspIoPipeEvtHandleAcquire->unsubscribe();
      mspIoPipeEvtHandleAcquire = nullptr;
    }
    mspIoPipeEvtHandleAcquire = evtSystem.subscribe(
        NSCam::NSIoPipe::EVT_IPRAW_P1_ACQUIRING, onEvtCtrlAcquiring, this);
    if (mspIoPipeEvtHandleAcquire == nullptr) {
      MY_LOGE("IoPipeEventSystem subscribe EVT_IPRAW_P1_ACQUIRING fail");
      return UNKNOWN_ERROR;
    }
    if (mspIoPipeEvtHandleRelease != nullptr) {
      mspIoPipeEvtHandleRelease->unsubscribe();
      mspIoPipeEvtHandleRelease = nullptr;
    }
    mspIoPipeEvtHandleRelease = evtSystem.subscribe(
        NSCam::NSIoPipe::EVT_IPRAW_P1_RELEASED, onEvtCtrlReleasing, this);
    if (mspIoPipeEvtHandleRelease == nullptr) {
      MY_LOGE("IoPipeEventSystem subscribe EVT_IPRAW_P1_RELEASED fail");
      return UNKNOWN_ERROR;
    }
  }
#endif

  mStuffBufMgr.setLog(getOpenId(), mLogLevel, mLogLevelI);

  mLongExp.config(getOpenId(), mLogLevel, mLogLevelI);

  mpConCtrl =
      std::make_shared<ConcurrenceControl>(mLogLevel, mLogLevelI, mSysLevel);
  if (mpConCtrl == nullptr || mpConCtrl->getStageCtrl() == nullptr) {
    MY_LOGE("ConcurrenceControl create fail");
    return NO_MEMORY;
  }

  mpHwStateCtrl = std::make_shared<HardwareStateControl>();
  if (mpHwStateCtrl == nullptr) {
    MY_LOGE("HardwareStateControl create fail");
    return NO_MEMORY;
  }
  mpConnectLMV = std::make_shared<P1ConnectLMV>(getOpenId(), mLogLevel,
                                                mLogLevelI, mSysLevel);
  if (mpConnectLMV == NULL) {
    MY_LOGE("ConnectLMV create fail Log(%d)(%d) Id(%d)", mLogLevel, mLogLevelI,
            getOpenId());
    return NO_MEMORY;
  }
  mpTimingCheckerMgr = std::make_shared<TimingCheckerMgr>(
      mTimingFactor, getOpenId(), mLogLevel, mLogLevelI);
  if (mpTimingCheckerMgr == nullptr) {
    MY_LOGE("TimingCheckerMgr create fail");
    return NO_MEMORY;
  }

  mThread = std::thread(std::bind(&P1NodeImp::threadLoop, this));

  mpDeliverMgr = std::make_shared<P1DeliverMgr>();
  if (mpDeliverMgr != nullptr) {
    mpDeliverMgr->init(shared_from_this());
  } else {
    MY_LOGE("DeliverMgr create fail");
    return NO_MEMORY;
  }

  mpRegisterNotify = std::make_shared<P1RegisterNotify>(shared_from_this());
  if (mpRegisterNotify != nullptr) {
    mpRegisterNotify->init();
  } else {
    MY_LOGE("RegisterNotify create fail");
    return NO_MEMORY;
  }

  mpTaskCtrl = std::make_shared<P1TaskCtrl>(shared_from_this());
  if (mpTaskCtrl == nullptr) {
    MY_LOGE("TaskCtrl create fail");
    return NO_MEMORY;
  }

  mpTaskCollector = std::make_shared<P1TaskCollector>(mpTaskCtrl);
  if (mpTaskCollector == nullptr) {
    MY_LOGE("TaskCollector create fail");
    return NO_MEMORY;
  }

  mpAccDetector = std::make_unique<cros::NSCam::AccelerationDetector>();
  mpAccDetector->prepare();

  FUNCTION_OUT;

  return NO_ERROR;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::uninit() {
  FUNCTION_IN;

  P1_TRACE_AUTO(SLG_B, "P1:uninit");

  LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_API_UNINIT_BGN,
                       LogInfo::CP_API_UNINIT_END);

#if (USING_DRV_IO_PIPE_EVENT)
  {
    std::lock_guard<std::mutex> _l(mIoPipeEvtOpLock);
    mIoPipeEvtOpLeaving = MTRUE;
    if (mspIoPipeEvtHandleAcquire != nullptr) {
      mspIoPipeEvtHandleAcquire->unsubscribe();
      mspIoPipeEvtHandleAcquire = nullptr;
    }
    if (mspIoPipeEvtHandleRelease != nullptr) {
      mspIoPipeEvtHandleRelease->unsubscribe();
      mspIoPipeEvtHandleRelease = nullptr;
    }
  }
#endif

  std::lock_guard<std::mutex> _l(mPublicLock);

  // flush the left frames if exist
  onHandleFlush(MFALSE);

  requestExit();

  // mvStreamMeta.clear();
  for (int stream = STREAM_ITEM_START; stream < STREAM_META_NUM; stream++) {
    mvStreamMeta[stream] = nullptr;
  }

  // mvStreamImg.clear();
  for (int stream = STREAM_ITEM_START; stream < STREAM_IMG_NUM; stream++) {
    mvStreamImg[stream] = nullptr;
  }

  mspSyncHelper = nullptr;

  if (mspResConCtrl != nullptr) {
    P1NODE_RES_CON_RETURN(mspResConCtrl, mResConClient);
    mspResConCtrl = nullptr;
  }

  if (mpDeliverMgr != nullptr) {
    mpDeliverMgr->uninit();
    mpDeliverMgr = nullptr;
  }

  if (mpRegisterNotify != nullptr) {
    mpRegisterNotify->uninit();
    mpRegisterNotify = nullptr;
  }

  mpTaskCollector = nullptr;

  mpTaskCtrl = nullptr;

  mpTimingCheckerMgr = nullptr;

  mpHwStateCtrl = nullptr;

  mpConCtrl = nullptr;

  FUNCTION_OUT;

  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::check_config(ConfigParams const& rParams) {
  P1_TRACE_AUTO(SLG_S, "P1:check_config");

  if (rParams.pInAppMeta == nullptr) {
    MY_LOGE("in app metadata is null");
    return BAD_VALUE;
  }

  if (rParams.pInHalMeta == nullptr) {
    MY_LOGE("in hal metadata is null");
    return BAD_VALUE;
  }

  if (rParams.pOutAppMeta == nullptr) {
    MY_LOGE("out app metadata is null");
    return BAD_VALUE;
  }

  if (rParams.pOutHalMeta == nullptr) {
    MY_LOGE("out hal metadata is null");
    return BAD_VALUE;
  }

  if (rParams.pvOutImage_full.size() == 0 &&
      rParams.pOutImage_resizer == nullptr) {
    MY_LOGE("image is empty");
    return BAD_VALUE;
  }

  if (rParams.pStreamPool_full != nullptr &&
      rParams.pvOutImage_full.size() == 0) {
    MY_LOGE("wrong full input");
    return BAD_VALUE;
  }

  if (rParams.pStreamPool_resizer != nullptr &&
      rParams.pOutImage_resizer == nullptr) {
    MY_LOGE("wrong resizer input");
    return BAD_VALUE;
  }

  if (rParams.pStreamPool_lcso != nullptr &&
      rParams.pOutImage_lcso == nullptr) {
    MY_LOGE("wrong resizer input");
    return BAD_VALUE;
  }

  if (rParams.enableLCS == MTRUE && rParams.pOutImage_lcso == nullptr) {
    MY_LOGE("wrong resizer input");
    return BAD_VALUE;
  }
  //
  if (mpDeliverMgr != nullptr && mpDeliverMgr->runningGet() /*isRunning()*/) {
    MY_LOGI("DeliverMgr thread is running");
    mpDeliverMgr->requestExit();
    mpDeliverMgr->trigger();
    mpDeliverMgr->join();
    mpDeliverMgr->runningSet(MFALSE);
  }

  // Get sensor format
  IHalSensorList* const pIHalSensorList = GET_HalSensorList();
  if (pIHalSensorList) {
    MUINT32 sensorDev =
        (MUINT32)pIHalSensorList->querySensorDevIdx(getOpenId());

    NSCam::SensorStaticInfo sensorStaticInfo;
    memset(&sensorStaticInfo, 0, sizeof(NSCam::SensorStaticInfo));
    pIHalSensorList->querySensorStaticInfo(sensorDev, &sensorStaticInfo);
    mSensorFormatOrder = sensorStaticInfo.sensorFormatOrder;
    MY_LOGI("SensorFormatOrder %d", mSensorFormatOrder);
  }

  //
  {
    pthread_rwlock_wrlock(&mConfigRWLock);
    //
    for (int meta = STREAM_ITEM_START; meta < STREAM_META_NUM; meta++) {
      mvStreamMeta[meta] = nullptr;
    }
    if (rParams.pInAppMeta != nullptr) {
      mvStreamMeta[STREAM_META_IN_APP] = rParams.pInAppMeta;
    }
    if (rParams.pInHalMeta != nullptr) {
      mvStreamMeta[STREAM_META_IN_HAL] = rParams.pInHalMeta;
    }
    if (rParams.pOutAppMeta != nullptr) {
      mvStreamMeta[STREAM_META_OUT_APP] = rParams.pOutAppMeta;
    }
    if (rParams.pOutHalMeta != nullptr) {
      mvStreamMeta[STREAM_META_OUT_HAL] = rParams.pOutHalMeta;
    }
    //
    for (int img = STREAM_ITEM_START; img < STREAM_IMG_NUM; img++) {
      mvStreamImg[img] = nullptr;
    }
    //
    if (rParams.pInImage_yuv != nullptr) {
      mvStreamImg[STREAM_IMG_IN_YUV] = rParams.pInImage_yuv;
    }
    if (rParams.pInImage_opaque != nullptr) {
      mvStreamImg[STREAM_IMG_IN_OPAQUE] = rParams.pInImage_opaque;
    }
    if (rParams.pOutImage_opaque != nullptr) {
      mvStreamImg[STREAM_IMG_OUT_OPAQUE] = rParams.pOutImage_opaque;
    }

    for (size_t i = 0; i < rParams.pvOutImage_full.size(); i++) {
      if (rParams.pvOutImage_full[i] != nullptr) {  // pick the first item
        mvStreamImg[STREAM_IMG_OUT_FULL] = rParams.pvOutImage_full[i];
        break;
      }
    }

    if (rParams.pOutImage_resizer != nullptr) {
      mvStreamImg[STREAM_IMG_OUT_RESIZE] = rParams.pOutImage_resizer;
    }

    if (rParams.pOutImage_lcso != nullptr) {
      mvStreamImg[STREAM_IMG_OUT_LCS] = rParams.pOutImage_lcso;
      mEnableLCSO = MTRUE;
    }

    //
    mpStreamPool_full = rParams.pStreamPool_full;
    mpStreamPool_resizer = rParams.pStreamPool_resizer;
    mpStreamPool_lcso = rParams.pStreamPool_lcso;
    //
#if 0
#warning "[FIXME] force to change p1 not use pool"
        {
            MUINT8 no_pool =
                ::property_get_int32("vendor.debug.camera.p1nopool", 0);
            if (no_pool > 0) {
                mpStreamPool_full = NULL;
                mpStreamPool_resizer = NULL;
                mpStreamPool_lcso = NULL;
                mpStreamPool_rsso = NULL;
            }
            MY_LOGI("debug.camera.p1nopool = %d", no_pool);
        }
#endif
    //
    { mspSyncHelper = rParams.pSyncHelper; }
    //
    {
      if (mspResConCtrl != nullptr) {
        P1NODE_RES_CON_RETURN(mspResConCtrl, mResConClient);
        mspResConCtrl = nullptr;
      }
      mspResConCtrl = rParams.pResourceConcurrency;
      if (mspResConCtrl != nullptr) {
        mResConClient = IResourceConcurrency::CLIENT_HANDLER_NULL;
        mIsResConGot = MFALSE;
      }
    }
    //
    mBurstNum = MAX(rParams.burstNum, 1);
#if (ENABLE_CHECK_CONFIG_COMMON_PORPERTY || (0))  // for SMVR IT
#warning "[FIXME] force to change p1 burst number"
    {
      MUINT8 burst_num = ::property_get_int32("vendor.debug.camera.p1burst", 0);
      if (burst_num > 0) {
        mBurstNum = burst_num;
      }
      MY_LOGI("debug.camera.p1burst = %d  -  BurstNum = %d", burst_num,
              mBurstNum);
    }
#endif
    //
    mReceiveMode = rParams.receiveMode;
#if (ENABLE_CHECK_CONFIG_COMMON_PORPERTY || (0))  // receive mode IT
#warning "[FIXME] force to change p1 receive mode"
    {
      MUINT8 rev_mode = ::property_get_int32("vendor.debug.camera.p1rev", 0);
      if (rev_mode > 0) {
        mReceiveMode = (REV_MODE)rev_mode;
      }
      MY_LOGI("debug.camera.p1rev = %d  - RevMode=%d BurstNum=%d", rev_mode,
              mReceiveMode, mBurstNum);
    }
#endif
    //
    // #warning "force to change standby mode"
    {
      MINT8 standby_mode =
          ::property_get_int32("vendor.debug.camera.p1standbymode", 0);
      if (standby_mode > 0) {
        mForceStandbyMode = standby_mode;
        MY_LOGI("debug.camera.standbymode = %d - ForceStandbyMode = %d",
                standby_mode, mForceStandbyMode);
      }
    }
    //
    std::string meta_str("");
    mCfgAppMeta.clear();
    if (rParams.cfgAppMeta.count() > 0) {
      mCfgAppMeta = rParams.cfgAppMeta;
      if (1 <= mLogLevelI) {
        base::StringAppendF(&meta_str, " -- ConfigParams.cfgAppMeta[%d] ",
                            rParams.cfgAppMeta.count());
        for (MUINT32 i = 0; i < rParams.cfgAppMeta.count(); i++) {
          generateMetaInfoStr(rParams.cfgAppMeta.entryAt(i), &meta_str);
        }
      }
    }
    mCfgHalMeta.clear();
    if (rParams.cfgHalMeta.count() > 0) {
      mCfgHalMeta = rParams.cfgHalMeta;
      if (1 <= mLogLevelI) {
        base::StringAppendF(&meta_str, " -- ConfigParams.cfgHalMeta[%d] ",
                            rParams.cfgHalMeta.count());
        for (MUINT32 i = 0; i < rParams.cfgHalMeta.count(); i++) {
          generateMetaInfoStr(rParams.cfgHalMeta.entryAt(i), &meta_str);
        }
      }
    }
    if (!meta_str.empty()) {
      MY_LOGI("%s", meta_str.c_str());
    }
    //
    mSensorParams = rParams.sensorParams;
    //
    mRawProcessed = rParams.rawProcessed;
    mRawSetDefType = rParams.rawDefType;
    //
    mTgNum = rParams.tgNum;
    //
    mPipeMode = rParams.pipeMode;
    //
    mPipeBit = rParams.pipeBit;
    //
    mResizeQuality = rParams.resizeQuality;
    //
    mDisableHLR = rParams.disableHLR;
    //
    mDisableFrontalBinning = rParams.disableFrontalBinning;
    //
    mDisableDynamicTwin = rParams.disableDynamicTwin;
    //
    mEnableUniForcedOn = rParams.enableUNI;
    //
    if (IS_LMV(mpConnectLMV)) {
      mEnableEISO = rParams.enableEIS;
      mForceSetEIS = rParams.forceSetEIS;
      mPackedEisInfo = rParams.packedEisInfo;
    }
    mEnableCaptureFlow = rParams.enableCaptureFlow;
    mEnableFrameSync = rParams.enableFrameSync;
    if (EN_START_CAP) {  // disable - acquire init buffer from pool
      mpStreamPool_full = nullptr;
      mpStreamPool_resizer = nullptr;
    }
    //
    {
      mInitReqSet = rParams.initRequest;
#if (ENABLE_CHECK_CONFIG_COMMON_PORPERTY)  // init request set IT
#warning "[FIXME] force to set init request"
      {
        MUINT8 init_req = ::property_get_int32("vendor.debug.camera.p1init", 0);
        if (init_req > 0) {
          mInitReqSet = init_req;
        }
        MY_LOGI("debug.camera.p1init = %d  - mInitReq=%d BurstNum=%d", init_req,
                mInitReqSet, mBurstNum);
      }
#endif
      if (EN_INIT_REQ_CFG && mInitReqSet <= P1NODE_DEF_SHUTTER_DELAY) {
        MY_LOGE("INVALID init request value (%d)", mInitReqSet);
        pthread_rwlock_unlock(&mConfigRWLock);
        return INVALID_OPERATION;
      }
      mInitReqNum =
          mInitReqSet *
          mBurstNum;  // the InitReq setting will re-assign as re-configure
      mInitReqCnt = 0;
      mInitReqOff = MFALSE;
      if (EN_INIT_REQ_CFG) {
        MY_LOGI("InitReq Set:%d Num:%d Cnt:%d Off:%d", mInitReqSet, mInitReqNum,
                mInitReqCnt, mInitReqOff);
      }
    }
    //
    if (IS_BURST_ON) {
      mDepthNum = 2;
    } else if (isRevMode(REV_MODE_CONSERVATIVE)) {
      mDepthNum = 2;
    } else {
      mDepthNum = 1;
    }
    //
    {
      if (((EN_BURST_MODE) &&
           (EN_INIT_REQ_CFG || EN_START_CAP || EN_REPROCESSING)) ||
          (EN_INIT_REQ_CFG && EN_START_CAP)) {
        MY_LOGE(
            "[Check_Config_Conflict] P1Node::ConfigParams:: "
            "burstNum(%d) enableCaptureFlow(%d) initRequest(%d) "
            "pInImage_opaque[%#" PRIx64
            "] "
            "pInImage_yuv[%#" PRIx64 "] ",
            rParams.burstNum, rParams.enableCaptureFlow, rParams.initRequest,
            ((mvStreamImg[STREAM_IMG_IN_OPAQUE] == nullptr)
                 ? (StreamId_T)(-1)
                 : mvStreamImg[STREAM_IMG_IN_OPAQUE]->getStreamId()),
            ((mvStreamImg[STREAM_IMG_IN_YUV] == nullptr)
                 ? (StreamId_T)(-1)
                 : mvStreamImg[STREAM_IMG_IN_YUV]->getStreamId()));
        pthread_rwlock_unlock(&mConfigRWLock);
        return INVALID_OPERATION;
      }
    }
    pthread_rwlock_unlock(&mConfigRWLock);
  }
  //
  if (mvStreamImg[STREAM_IMG_OUT_OPAQUE] != nullptr) {
    if (mvStreamImg[STREAM_IMG_OUT_FULL] != nullptr) {
      mRawFormat = mvStreamImg[STREAM_IMG_OUT_FULL]->getImgFormat();
      mRawStride =
          mvStreamImg[STREAM_IMG_OUT_FULL]->getBufPlanes()[0].rowStrideInBytes;
      mRawLength =
          mvStreamImg[STREAM_IMG_OUT_FULL]->getBufPlanes()[0].sizeInBytes;
    } else {
      mRawFormat = P1_IMGO_DEF_FMT;
      NormalPipe_QueryInfo queryRst;
      getNormalPipeModule()->query(PORT_IMGO.index, ENPipeQueryCmd_STRIDE_BYTE,
                                   (EImageFormat)mRawFormat,
                                   mSensorParams.size.w, &queryRst);
      mRawStride = queryRst.stride_byte;
      mRawLength = mRawStride * mSensorParams.size.h;
    }
  }
  //
  {
    std::shared_ptr<IMetadataProvider> pMetadataProvider =
        NSMetadataProviderManager::valueFor(getOpenId());
    if (!pMetadataProvider.get()) {
      MY_LOGE(" ! pMetadataProvider.get() ");
      return DEAD_OBJECT;
    }
    IMetadata static_meta = pMetadataProvider->getMtkStaticCharacteristics();
    if (tryGetMetadata<MRect>(&static_meta, MTK_SENSOR_INFO_ACTIVE_ARRAY_REGION,
                              &mActiveArray)) {
      MY_LOGD_IF(mLogLevel > 1, "active array(%d, %d, %dx%d)", mActiveArray.p.x,
                 mActiveArray.p.y, mActiveArray.s.w, mActiveArray.s.h);
    } else {
      MY_LOGE("no static info: MTK_SENSOR_INFO_ACTIVE_ARRAY_REGION");
#if (P1NODE_USING_MTK_LDVT > 0)
      mActiveArray = MRect(mSensorParams.size.w, mSensorParams.size.h);
      MY_LOGI("set sensor size to active array(%d, %d, %dx%d)",
              mActiveArray.p.x, mActiveArray.p.y, mActiveArray.s.w,
              mActiveArray.s.h);
#else
      return UNKNOWN_ERROR;
#endif
    }
  }
  //
  if (mvStreamImg[STREAM_IMG_OUT_FULL] != nullptr) {
    if (mvStreamImg[STREAM_IMG_OUT_FULL]->getImgSize() != mSensorParams.size) {
      MY_LOGE(
          "[Check_Config_Conflict] IMGO_Stream.ImgSize(%dx%d) != "
          "SensorParam.Size(%d,%d) - P1Node::ConfigParams:: "
          "IMGO_StreamID:%#" PRIx64
          "_ImgFormat[0x%x]-ImgSize(%dx%d) "
          "SensorParam_mode(%d)_fps(%d)_pixelMode(%d)_vhdrMode(%d)_"
          "size(%dx%d)",
          mvStreamImg[STREAM_IMG_OUT_FULL]->getImgSize().w,
          mvStreamImg[STREAM_IMG_OUT_FULL]->getImgSize().h,
          mSensorParams.size.w, mSensorParams.size.h,
          mvStreamImg[STREAM_IMG_OUT_FULL]->getStreamId(),
          mvStreamImg[STREAM_IMG_OUT_FULL]->getImgFormat(),
          mvStreamImg[STREAM_IMG_OUT_FULL]->getImgSize().w,
          mvStreamImg[STREAM_IMG_OUT_FULL]->getImgSize().h, mSensorParams.mode,
          mSensorParams.fps, mSensorParams.pixelMode, mSensorParams.vhdrMode,
          mSensorParams.size.w, mSensorParams.size.h);
      return INVALID_OPERATION;
    }
  }
  //
  {
    MERROR res = checkConstraint();
    if (res != OK) {
      return res;
    }
  }
  //
  mLogInfo.config(getOpenId(), mLogLevel, mLogLevelI, mBurstNum);
  mLogInfo.setActive(MFALSE);
  //
  {
    MBOOL deliver_mgr_send = MTRUE;

    MY_LOGD("USE DeliverMgr Thread Loop : %d", deliver_mgr_send);
    if (deliver_mgr_send) {
      if (mpDeliverMgr != nullptr) {
        mpDeliverMgr->config();
        if (NO_ERROR == mpDeliverMgr->run()) {
          MY_LOGD("RUN DeliverMgr Thread OK");
          mpDeliverMgr->runningSet(MTRUE);
        } else {
          MY_LOGE("RUN DeliverMgr Thread FAIL");
          return BAD_VALUE;
        }
      }
    }
  }
  //
  if (mpTaskCtrl != nullptr) {
    mpTaskCtrl->config();
  }
  if (mpTaskCollector != nullptr) {
    mpTaskCollector->config();
  }
  if (mpRegisterNotify != nullptr) {
    mpRegisterNotify->config();
  }
  //
  {
    MUINT32 queReserve = mBurstNum * P1NODE_DEF_QUEUE_DEPTH;
    {
      std::lock_guard<std::mutex> _ll(mDropQueueLock);
      mDropQueue.clear();
      mDropQueue.reserve(queReserve);
    }
    {
      std::lock_guard<std::mutex> _ll(mRequestQueueLock);
      mRequestQueue.clear();
      mRequestQueue.reserve(queReserve);
    }
    {
      std::lock_guard<std::mutex> _ll(mProcessingQueueLock);
      mProcessingQueue.clear();
      mProcessingQueue.reserve(queReserve);
    }
  }

#if (IS_P1_LOGI)
  {
    std::string strInfo("");
    strInfo += base::StringPrintf("Cam::%d ", getOpenId());
    //
    strInfo += base::StringPrintf(
        "Param["
        "N:m%d,p%d,q%d,t%d,b%d,i%d,r%d,w%d,v%" PRId64
        "_"
        "B:p%d,b%d,t%d,h%d,u%d,e%d,l%d,v%d] ",
        // Param-iNt/eNum
        rParams.pipeMode /*m*/, rParams.pipeBit /*p*/,
        rParams.resizeQuality /*q*/, rParams.tgNum /*t*/,
        rParams.burstNum /*b*/, rParams.initRequest /*i*/,
        rParams.receiveMode /*r*/, rParams.rawDefType /*w*/,
        rParams.packedEisInfo /*v*/ /*EisInfo::getMode(mPackedEisInfo)*/,
        // Param-Bool
        rParams.rawProcessed /*p*/, rParams.disableFrontalBinning /*b*/,
        rParams.disableDynamicTwin /*t*/, rParams.disableHLR /*h*/,
        rParams.enableUNI /*u*/, rParams.enableEIS /*e*/,
        rParams.enableLCS /*l*/, rParams.forceSetEIS /*v*/);
    //
    strInfo += base::StringPrintf(
        "S(%d,%d,%d,%d,x%x,%dx%d) ", mSensorParams.mode, mSensorParams.fps,
        mSensorParams.pixelMode, mSensorParams.vhdrMode, mSensorFormatOrder,
        mSensorParams.size.w, mSensorParams.size.h);
    strInfo += base::StringPrintf("R(0x%x-%d-%d,%d-%d-%d,%d-0x%x) ", mRawFormat,
                                  mRawStride, mRawLength, mRawPostProcSupport,
                                  mRawProcessed, mRawSetDefType, mRawDefType,
                                  mRawOption);
    strInfo +=
        base::StringPrintf("D(b%d,t%d,h%d) ", mDisableFrontalBinning /*b*/,
                           mDisableDynamicTwin /*t*/, mDisableHLR /*h*/);
    strInfo +=
        base::StringPrintf("E(l%d,r%d,u%d,c%d,f%d) ", mEnableLCSO /*l*/,
                           mEnableRSSO /*r*/, mEnableUniForcedOn /*u*/,
                           mEnableCaptureFlow /*c*/, mEnableFrameSync /*f*/);
    strInfo += base::StringPrintf(
        "M(m0x%x,p0x%x,q%d,t%d,b%d,d%d,r%d,i%d", mPipeMode /*m*/,
        mPipeBit /*p*/, mResizeQuality /*q*/, mTgNum /*t*/, mBurstNum /*b*/,
        mDepthNum /*d*/, mReceiveMode /*r*/, mInitReqSet /*i*/
        /*v*/ /*EisInfo::getMode(mPackedEisInfo)*/);
    strInfo += base::StringPrintf("Dm(%d) ", mpDeliverMgr->runningGet());
    strInfo += base::StringPrintf("Rc(%p) ", mspResConCtrl.get());
    strInfo += base::StringPrintf("Sh(%p) ", mspSyncHelper.get());
    strInfo += base::StringPrintf(
        "Pool(IMG%p,RRZ%p,LCS%p,RSS%p) ",
        (mpStreamPool_full != NULL) ? (mpStreamPool_full.get()) : (NULL),
        (mpStreamPool_resizer != NULL) ? (mpStreamPool_resizer.get()) : (NULL),
        (mpStreamPool_lcso != NULL) ? (mpStreamPool_lcso.get()) : (NULL),
        (mpStreamPool_rsso != NULL) ? (mpStreamPool_rsso.get()) : (NULL));
    strInfo += base::StringPrintf(
        "Meta%s_%d:%#" PRIx64 " Meta%s_%d:%#" PRIx64
        " "
        "Meta%s_%d:%#" PRIx64 " Meta%s_%d:%#" PRIx64 " ",
        maStreamMetaName[STREAM_META_IN_APP], STREAM_META_IN_APP,
        (mvStreamMeta[STREAM_META_IN_APP] == nullptr)
            ? (StreamId_T)(-1)
            : mvStreamMeta[STREAM_META_IN_APP]->getStreamId(),
        maStreamMetaName[STREAM_META_IN_HAL], STREAM_META_IN_HAL,
        (mvStreamMeta[STREAM_META_IN_HAL] == nullptr)
            ? (StreamId_T)(-1)
            : mvStreamMeta[STREAM_META_IN_HAL]->getStreamId(),
        maStreamMetaName[STREAM_META_OUT_APP], STREAM_META_OUT_APP,
        (mvStreamMeta[STREAM_META_OUT_APP] == nullptr)
            ? (StreamId_T)(-1)
            : mvStreamMeta[STREAM_META_OUT_APP]->getStreamId(),
        maStreamMetaName[STREAM_META_OUT_HAL], STREAM_META_OUT_HAL,
        (mvStreamMeta[STREAM_META_OUT_HAL] == nullptr)
            ? (StreamId_T)(-1)
            : mvStreamMeta[STREAM_META_OUT_HAL]->getStreamId());
    //
    for (int i = STREAM_ITEM_START; i < STREAM_IMG_NUM; i++) {
      if (mvStreamImg[i] != nullptr) {
        strInfo += base::StringPrintf(
            "Img%s_%d:%#" PRIx64 "(%dx%d)[0x%x] ", (maStreamImgName[i]), i,
            mvStreamImg[i]->getStreamId(), mvStreamImg[i]->getImgSize().w,
            mvStreamImg[i]->getImgSize().h, mvStreamImg[i]->getImgFormat());
      }
    }
    //
    strInfo += base::StringPrintf(
        "Meta(APP:%d=%d,HAL:%d=%d) ", rParams.cfgAppMeta.count(),
        mCfgAppMeta.count(), rParams.cfgHalMeta.count(), mCfgHalMeta.count());
    //

    if (mvStreamImg[STREAM_IMG_OUT_RESIZE] != nullptr) {
      strInfo += base::StringPrintf(
          "RR(%d) ", getResizeMaxRatio(
                         mvStreamImg[STREAM_IMG_OUT_RESIZE]->getImgFormat()));
    }
    //
    strInfo += base::StringPrintf("AA(%d,%d-%dx%d) ", mActiveArray.p.x,
                                  mActiveArray.p.y, mActiveArray.s.w,
                                  mActiveArray.s.h);
    //
    MY_LOGI("%s", strInfo.c_str());
  }
#endif

  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::checkConstraint() {
  auto pModule = getNormalPipeModule();
  if (!pModule) {
    MY_LOGE("getNormalPipeModule() fail");
    return UNKNOWN_ERROR;
  }
  // check raw type
  { mRawPostProcSupport = isPostProcRawSupported(); }
  if (mPipeMode == PIPE_MODE_NORMAL_SV) {  // only support pure raw
    mRawDefType = EPipe_PURE_RAW;
    mRawOption = (1 << EPipe_PURE_RAW);
    if (mRawSetDefType == RAW_DEF_TYPE_PROCESSED_RAW) {
      MY_LOGE(
          "INVALID Raw-Default-Type option, "
          "P1Node::ConfigParams::PipeMode(%d) is PIPE_MODE_NORMAL_SV - "
          "it will reject the P1Node::ConfigParams::rawDefType(%d) "
          "A.K.A. RAW_DEF_TYPE_PROCESSED_RAW",
          mPipeMode, mRawSetDefType);
      return INVALID_OPERATION;
    }
    if (mRawProcessed == MTRUE) {
      MY_LOGE(
          "INVALID Raw-Processed option, "
          "P1Node::ConfigParams::PipeMode(%d) is PIPE_MODE_NORMAL_SV - "
          "it will reject the P1Node::ConfigParams::rawProcessed(%d) ",
          mPipeMode, mRawProcessed);
      return INVALID_OPERATION;
    }
  } else if (mRawPostProcSupport) {
    mRawDefType = EPipe_PURE_RAW;
    mRawOption = (1 << EPipe_PURE_RAW);
    if (mRawProcessed == MTRUE) {
      // DualPD, raw type will be selected by driver
      mRawDefType = EPipe_PROCESSED_RAW;
      mRawOption |= (1 << EPipe_PROCESSED_RAW);
    }
    if (mRawSetDefType == RAW_DEF_TYPE_AUTO) {
      // by previous decision
    } else if (mRawSetDefType == RAW_DEF_TYPE_PURE_RAW) {
      mRawDefType = EPipe_PURE_RAW;  // accepted
    } else if (mRawSetDefType == RAW_DEF_TYPE_PROCESSED_RAW) {
      if (mRawProcessed == MTRUE) {
        mRawDefType = EPipe_PROCESSED_RAW;  // accepted
      } else {
        MY_LOGE(
            "INVALID Raw-Default-Type option, "
            "P1Node::ConfigParams::rawProcessed(%d) not enabled - "
            "it will reject the P1Node::ConfigParams::rawDefType(%d) "
            "A.K.A. RAW_DEF_TYPE_PROCESSED_RAW",
            mRawProcessed, mRawSetDefType);
        return INVALID_OPERATION;
      }
    } else {
      MY_LOGE(
          "INVALID Raw-Default-Type option, "
          "P1Node::ConfigParams::rawProcessed(%d) - "
          "P1Node::ConfigParams::rawDefType(%d) "
          "UNKNOWN type",
          mRawProcessed, mRawSetDefType);
      return INVALID_OPERATION;
    }
  } else {  // i.e. the platform without HW PostProc raw support
    // ignore mRawProcessed value
    mRawOption = (1 << EPipe_PURE_RAW) | (1 << EPipe_PROCESSED_RAW);
    if (mRawSetDefType == RAW_DEF_TYPE_AUTO ||
        mRawSetDefType == RAW_DEF_TYPE_PROCESSED_RAW) {
      mRawDefType = EPipe_PROCESSED_RAW;  // accepted
    } else if (mRawSetDefType == RAW_DEF_TYPE_PURE_RAW) {
      mRawDefType = EPipe_PURE_RAW;
      MY_LOGW(
          "WARNING Raw-Default-Type option, "
          "use default-pure-raw without post-proc-raw-support - "
          "P1Node::ConfigParams::rawDefType(%d)",
          mRawSetDefType);
      // return INVALID_OPERATION;
    } else {
      MY_LOGE(
          "INVALID Raw-Default-Type option, "
          "P1Node::ConfigParams::rawDefType(%d) "
          "UNKNOWN type",
          mRawSetDefType);
      return INVALID_OPERATION;
    }
  }
  //
  // check Burst Mode
  if (mBurstNum > 1) {
    MBOOL ret = MFALSE;
    sCAM_QUERY_BURST_NUM res;
    res.QueryOutput = 0x0;
    ret = pModule->query(ENPipeQueryCmd_BURST_NUM, (MUINTPTR)(&res));
    if (!ret) {
      MY_LOGE("[Cam::%d] Cannot query ENPipeQueryCmd_BURST_NUM", getOpenId());
#if USING_DRV_QUERY_CAPABILITY_EXP_SKIP
      MY_LOGW("USING_DRV_QUERY_CAPABILITY_EXP_SKIP go-on");
#else
      return BAD_VALUE;
#endif
    } else if ((res.QueryOutput & (0x1 << mBurstNum)) == 0x0) {
      MY_LOGE(
          "[Cam::%d] ENPipeQueryCmd_BURST_NUM - support (0x%X) ,"
          " but BurstNum set as (0x%X)",
          getOpenId(), res.QueryOutput, mBurstNum);
#if USING_DRV_QUERY_CAPABILITY_EXP_SKIP
      MY_LOGW("USING_DRV_QUERY_CAPABILITY_EXP_SKIP go-on");
#else
      return INVALID_OPERATION;
#endif
    }
  }
  //
  // check Raw Pattern
  mCfg.mPattern = eCAM_NORMAL;
  {
    if (mSensorParams.vhdrMode == SENSOR_VHDR_MODE_ZVHDR) {
      // ZVHDR Mode, pass ZVHDR relative enum to P1 driver
      mCfg.mPattern = (eCAM_ZVHDR);
    } else if (mSensorParams.vhdrMode == SENSOR_VHDR_MODE_IVHDR) {
      // IVHDR Mode, pass IVHDR relative enum to P1 driver
      mCfg.mPattern = (eCAM_IVHDR);
    } else {
      mCfg.mPattern = (eCAM_NORMAL);  // EPipe_Normal
    }
  }
  if (mCfg.mPattern != eCAM_NORMAL) {
    MBOOL ret = MFALSE;
    sCAM_QUERY_SUPPORT_PATTERN res;
    res.QueryOutput = 0x0;
    ret = pModule->query(ENPipeQueryCmd_SUPPORT_PATTERN, (MUINTPTR)(&res));
    if (!ret) {
      MY_LOGE("[Cam::%d] Cannot query ENPipeQueryCmd_SUPPORT_PATTERN",
              getOpenId());
#if USING_DRV_QUERY_CAPABILITY_EXP_SKIP
      MY_LOGW("USING_DRV_QUERY_CAPABILITY_EXP_SKIP go-on");
#else
      return BAD_VALUE;
#endif
    } else if ((res.QueryOutput & (0x1 << mCfg.mPattern)) == 0x0) {
      MY_LOGE(
          "[Cam::%d] ENPipeQueryCmd_IQ_LEVEL - support (0x%X) ,"
          " but Pattern set as (0x%X) - by "
          " VhdrMode(%d)",
          getOpenId(), res.QueryOutput, mCfg.mPattern, mSensorParams.vhdrMode);
#if USING_DRV_QUERY_CAPABILITY_EXP_SKIP
      MY_LOGW("USING_DRV_QUERY_CAPABILITY_EXP_SKIP go-on");
#else
      return INVALID_OPERATION;
#endif
    }
  }
  //
  // check IQ Level
  mCfg.mQualityLv = eCamIQ_MAX;
  {
    switch (mResizeQuality) {
      case RESIZE_QUALITY_H:
        mCfg.mQualityLv = eCamIQ_H;
        break;
      case RESIZE_QUALITY_L:
        mCfg.mQualityLv = eCamIQ_L;
        break;
      default:
        break;
    }
  }
  if (mCfg.mQualityLv != eCamIQ_MAX) {
    MBOOL ret = MFALSE;
    sCAM_QUERY_IQ_LEVEL res;
    res.QueryOutput = MFALSE;
    ret = pModule->query(ENPipeQueryCmd_IQ_LEVEL, (MUINTPTR)(&res));
    if (!ret) {
      MY_LOGE("[Cam::%d] Cannot query ENPipeQueryCmd_IQ_LEVEL", getOpenId());
#if USING_DRV_QUERY_CAPABILITY_EXP_SKIP
      MY_LOGW("USING_DRV_QUERY_CAPABILITY_EXP_SKIP go-on");
#else
      return BAD_VALUE;
#endif
    } else if (res.QueryOutput == MFALSE) {
      MY_LOGE(
          "[Cam::%d] ENPipeQueryCmd_IQ_LEVEL - not support ,"
          " but Quality-Level set as (%d)",
          getOpenId(), mCfg.mQualityLv);
#if USING_DRV_QUERY_CAPABILITY_EXP_SKIP
      MY_LOGW("USING_DRV_QUERY_CAPABILITY_EXP_SKIP go-on");
#else
      return INVALID_OPERATION;
#endif
    }
  }
  //
  // check Dynamic Twin
  mCfg.mSupportDynamicTwin = MFALSE;
  {
    MBOOL ret = MFALSE;
    sCAM_QUERY_D_Twin res;
    res.QueryOutput = MFALSE;
    ret = pModule->query(ENPipeQueryCmd_D_Twin, (MUINTPTR)(&res));
    if (!ret) {
      MY_LOGE("[Cam::%d] Cannot query ENPipeQueryCmd_D_Twin", getOpenId());
#if USING_DRV_QUERY_CAPABILITY_EXP_SKIP
      MY_LOGW("USING_DRV_QUERY_CAPABILITY_EXP_SKIP go-on");
#else
      return BAD_VALUE;
#endif
    }
    mCfg.mSupportDynamicTwin = res.QueryOutput;
  }
  //
  mIsLegacyStandbyMode = (mCfg.mSupportDynamicTwin) ? MFALSE : MTRUE;
  mIsDynamicTwinEn =
      (mCfg.mSupportDynamicTwin && (!mDisableDynamicTwin)) ? MTRUE : MFALSE;
  //
  // check Sensor-TG Number
  mCfg.mSensorNum = E_1_SEN;
  switch (mTgNum) {
    case 0:
    case 1:
      mCfg.mSensorNum = E_1_SEN;
      break;
    case 2:
    default:
      mCfg.mSensorNum = E_2_SEN;
      break;
  }
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::config(ConfigParams const& rParams) {
  FUNCTION_IN;
  P1_TRACE_AUTO(SLG_B, "P1:config");

  std::lock_guard<std::mutex> _l(mPublicLock);

  if (getActive()) {
    MY_LOGD("active=%d", getActive());
    onHandleFlush(MFALSE);
  }

  // (1) check
  if (mpTimingCheckerMgr != nullptr) {
    mpTimingCheckerMgr->setEnable(MTRUE);
  }
  //
  MERROR err = check_config(rParams);
  if (err != OK) {
    MY_LOGE("Config Param - Check fail (%d)", err);
    return err;
  }

  // (2) configure hardware
  if (mpConCtrl != nullptr &&
      (!EN_INIT_REQ_RUN)) {  // init-request, no aid-start
    mpConCtrl->setAidUsage(MTRUE);
  }
  //
  if (mpTimingCheckerMgr != nullptr) {
    mpTimingCheckerMgr->waitReady();
  }
  //
  mpTaskCtrl->reset();
  //
  err = hardwareOps_start();
  if ((!(EN_START_CAP || EN_INIT_REQ_RUN)) || (err != OK)) {
    if (mpConCtrl != nullptr && mpConCtrl->getAidUsage() == MTRUE) {
      mpConCtrl->cleanAidStage();
    }
    if (mpTimingCheckerMgr != nullptr) {
      mpTimingCheckerMgr->setEnable(MFALSE);
    }
  }
  if (err != OK) {
    MY_LOGE("Config Param - HW start fail (%d)", err);
    return err;
  }

  FUNCTION_OUT;

  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::fetchJob(P1QueJob* rOutJob) {
  if (CC_UNLIKELY(mpTaskCtrl == nullptr || mpTaskCollector == nullptr)) {
    return BAD_VALUE;
  }
  rOutJob->clear();
  MINT cnt = 0;
  mpTaskCtrl->sessionLock();
  cnt = mpTaskCollector->requireJob(rOutJob);
  mTagList.set(cnt);
  if (rOutJob->empty()) {
    MY_LOGI("using-dummy-request");
    if (2 <= mLogLevelI) {
      mpTaskCollector->dumpRoll();
    }
    //
    P1TaskCollector dummyCollector(mpTaskCtrl);
    for (int i = 0; i < mBurstNum; i++) {
      P1QueAct newAct;
      dummyCollector.enrollAct(&newAct);
      createAction(&newAct, nullptr, REQ_TYPE_DUMMY);
      dummyCollector.verifyAct(&newAct);
    }
    dummyCollector.requireJob(rOutJob);
  }
  mpTaskCtrl->sessionUnLock();
  if (!rOutJob->ready()) {
    MY_LOGE("job-not-ready");
    mpTaskCtrl->dumpActPool();
    return BAD_VALUE;
  }
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::setRequest(MBOOL initial) {
  FUNCTION_IN;
  //
  std::lock_guard<std::mutex> _ll(mFrameSetLock);
  if (CC_UNLIKELY(!initial && !mFrameSetAlready)) {
    MY_LOGI("frame set not init complete");
    return;
  }
  if (CC_UNLIKELY(!getActive())) {
    MY_LOGI("not-active-return");
    return;
  }
  //
  P1QueJob job(mBurstNum);
  //
  {
    if (OK != fetchJob(&job)) {
      MY_LOGE("job-fetch-fail");
      return;
    }
    if (mpHwStateCtrl != nullptr && mpHwStateCtrl->isLegacyStandby() &&
        mpHwStateCtrl->checkReceiveRestreaming()) {
      P1Act pAct = GET_ACT_PTR(pAct, job.edit(0), RET_VOID);
      if (pAct->ctrlSensorStatus == SENSOR_STATUS_CTRL_STREAMING) {
        mpHwStateCtrl->checkRestreamingNum(pAct->getNum());
      }
    }
  }
  //
  if (!initial) {
    beckonRequest();
  }
  //
  if (IS_BURST_OFF &&       // exclude burst mode
      (job.size() >= 1)) {  // check control callback
    attemptCtrlSetting(&(job.edit(0)));
  }
  //
#if USING_CTRL_3A_LIST
  List<NS3Av3::MetaSet_T> ctrlList;
  generateCtrlList(&ctrlList, &job);
  MY_LOGD("CtrlList[%zu]", ctrlList.size());
#else
  std::vector<NS3Av3::MetaSet_T*> ctrlQueue;
  ctrlQueue.clear();
  ctrlQueue.reserve(job.size());
  generateCtrlQueue(&ctrlQueue, &job);
  MY_LOGD("CtrlQueue[%zu]", ctrlQueue.size());
#endif
  //
  mLastSetNum = job.getLastNum();
  mTagSet.set(mLastSetNum);
  {
    std::lock_guard<std::mutex> _l(mRequestQueueLock);
    mRequestQueue.push_back(job);
  }
  P1QueAct* qAct = (job.ready()) ? (job.getLastAct()) : (nullptr);
  if (qAct == nullptr) {
    MY_LOGW("job-not-ready [%zu] < [%d]", job.size(), job.getMax());
    return;
  }
  P1Act pAct = GET_ACT_PTR(pAct, (*qAct), RET_VOID);
#if SUPPORT_3A
  if (mp3A) {
    MINT32 p_key = qAct->id();
    MINT32 m_num = pAct->magicNum;
    MINT32 f_num = pAct->frmNum;
    MINT32 r_num = pAct->reqNum;
    if (initial) {
      mLogInfo.setMemo(LogInfo::CP_START_SET_BGN, LogInfo::START_SET_GENERAL,
                       m_num);
    }
    mLogInfo.setMemo(LogInfo::CP_SET_BGN, p_key, m_num, f_num, r_num);
    P1_TRACE_F_BEGIN(SLG_I, "P1:3A-set|Pkey:%d Mnum:%d Fnum:%d Rnum:%d", p_key,
                     m_num, f_num, r_num);
    MY_LOGD("mp3A->set[%d](%d) +++", p_key, m_num);
#if USING_CTRL_3A_LIST
    mp3A->set(ctrlList);
#else
    mp3A->set(ctrlQueue);
#endif
    MY_LOGD("mp3A->set[%d](%d) ---", p_key, m_num);
    P1_TRACE_C_END(SLG_I);  // "P1:3A-set"
    mLogInfo.setMemo(LogInfo::CP_SET_END, p_key, m_num, f_num, r_num);
    if (initial) {
      mLogInfo.setMemo(LogInfo::CP_START_SET_END, LogInfo::START_SET_GENERAL,
                       m_num);
    }
    mFrameSetAlready = MTRUE;
  }
  if (1 <= mLogLevelI) {
    P1_TRACE_F_BEGIN(SLG_PFL,
                     "P1::SET_LOG|Mnum:%d SofIdx:%d Fnum:%d "
                     "Rnum:%d FlushSet:0x%x",
                     pAct->magicNum, pAct->sofIdx, pAct->frmNum, pAct->reqNum,
                     pAct->flushSet);
    std::string str("");
    MINT32 num = 0;
    size_t idx = 0;
#if USING_CTRL_3A_LIST
    size_t size = ctrlList.size();
    List<NS3Av3::MetaSet_T>::iterator it = ctrlList.begin();
    for (; it != ctrlList.end(); it++) {
      num = it->MagicNum;
      if ((idx > 0) && (idx % mBurstNum == 0)) {
        str += base::StringPrintf(", ");
      }
      str += base::StringPrintf("%d ", num);
      idx++;
    }
#else
    size_t size = ctrlQueue.size();
    std::vector<NS3Av3::MetaSet_T*>::iterator it = ctrlQueue.begin();
    for (; it != ctrlQueue.end(); it++) {
      num = ((*it) != nullptr) ? ((*it)->MagicNum) : (0);
      str += base::StringPrintf("%d ", num);
      idx++;
    }
#endif
    P1_LOGI(1, "[P1::SET]" P1INFO_ACT_STR " Num[%d] Ctrl[%zu]=[ %s]",
            P1INFO_ACT_VAR(*pAct), num, size, str.c_str());
    P1_TRACE_C_END(SLG_PFL);  // "P1::SET_LOG"
  }
#endif
  FUNCTION_OUT;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1NodeImp::acceptRequest(std::shared_ptr<IPipelineFrame> pFrame,
                         MUINT32* rRevResult) {
  FUNCTION_IN;
  *rRevResult = (MUINT32)REQ_REV_RES_ACCEPT_AVAILABLE;
#if (USING_DRV_IO_PIPE_EVENT)
  {
    pthread_rwlock_rdlock(&mIoPipeEvtStateLock);
    if (mIoPipeEvtState != IO_PIPE_EVT_STATE_NONE) {
      *rRevResult = (MUINT32)REQ_REV_RES_REJECT_IO_PIPE_EVT;
      pthread_rwlock_unlock(&mIoPipeEvtStateLock);
      return MFALSE;
    }
    pthread_rwlock_unlock(&mIoPipeEvtStateLock);
  }
#endif
  if ((!getReady()) || (!mFirstReceived)) {
    return MTRUE;
  }
  // check-bypass-request
  if (pFrame != nullptr) {
    MBOOL isBypass = pFrame->IsReprocessFrame();
    if (isBypass) {
      *rRevResult = (MUINT32)REQ_REV_RES_ACCEPT_BYPASS;
      MY_LOGI("Num[F:%d,R:%d] - BypassFrame", P1GET_FRM_NUM(pFrame),
              P1GET_REQ_NUM(pFrame));
      return MTRUE;
    }
  }
  //
  MINT cnt = 0;
  MBOOL isAccept = checkReqCnt(&cnt);
  MY_LOGI("Num[F:%d,R:%d] - Cnt(%d) Accept(%d)", P1GET_FRM_NUM(pFrame),
          P1GET_REQ_NUM(pFrame), cnt, isAccept);
  if (!isAccept) {
    *rRevResult = (MUINT32)REQ_REV_RES_REJECT_NOT_AVAILABLE;
  }
  FUNCTION_OUT;
  return isAccept;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1NodeImp::beckonRequest() {
  FUNCTION_IN;
  MINT cnt = 0;
  if (CC_LIKELY(checkReqCnt(&cnt))) {
    MINT32 frmNum = P1_FRM_NUM_NULL;
    MINT32 reqNum = P1_REQ_NUM_NULL;
    MINT32 cnt = lastFrameRequestInfoNotice(&frmNum, &reqNum, 1);
    MBOOL exeCb = MTRUE;
    {
      std::lock_guard<std::mutex> _l(mPipelineCbLock);
      std::shared_ptr<INodeCallbackToPipeline> spCb = mwpPipelineCb.lock();
      if (CC_LIKELY(spCb != nullptr)) {
        MY_LOGI("Pipeline_CB (F:%d,R:%d) CbButNotQueCnt:%d +++", frmNum, reqNum,
                cnt);
        LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_REQ_NOTIFY_BGN,
                             LogInfo::CP_REQ_NOTIFY_END, frmNum, reqNum, cnt);
        INodeCallbackToPipeline::CallBackParams param;
        param.nodeId = getNodeId();
        param.lastFrameNum = frmNum;
        spCb->onCallback(param);
        MY_LOGI("Pipeline_CB (F:%d,R:%d) CbButNotQueCnt:%d ---", frmNum, reqNum,
                cnt);
      } else {
        exeCb = MFALSE;
      }
    }
    if (CC_UNLIKELY(!exeCb)) {
      cnt = lastFrameRequestInfoNotice(&frmNum, &reqNum, (-1));  // reset count
      MY_LOGI("Pipeline_CB not exist (F:%d,R:%d) CbButNotQueCnt:%d", frmNum,
              reqNum, cnt);
    }
    return MTRUE;
  } else {
    MY_LOGI("not-callback - cnt(%d)", cnt);
  }
  FUNCTION_OUT;
  return MFALSE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1NodeImp::checkReqCnt(MINT32* cnt) {
  FUNCTION_IN;
  if (CC_UNLIKELY(mpTaskCtrl == nullptr || mpTaskCollector == nullptr)) {
    MY_LOGE("Task Controller or Collector not acceptable");
    return MFALSE;
  }
  //
  MINT depth = mDepthNum;
  MINT cnt_num = depth * mBurstNum;
  MINT que_num = 0;
  MBOOL isAccept = MTRUE;
  mpTaskCtrl->sessionLock();
  if ((que_num = mpTaskCollector->remainder()) >= cnt_num) {
    isAccept = MFALSE;
  }
  mpTaskCtrl->sessionUnLock();
  MY_LOGI("Que(%d) < Cnt(%d)=(%d*%d) : Accept(%d)", que_num, cnt_num, depth,
          mBurstNum, isAccept);
  *cnt = que_num;
  FUNCTION_OUT;
  return isAccept;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::setNodeCallBack(std::weak_ptr<INodeCallbackToPipeline> pCallback) {
  std::lock_guard<std::mutex> _l(mPipelineCbLock);
  MY_LOGI("PipelineNodeCallBack=%d", pCallback.expired());
  mwpPipelineCb = pCallback;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::queue(std::shared_ptr<IPipelineFrame> pFrame) {
  FUNCTION_IN;
  mLogInfo.setMemo(LogInfo::CP_REQ_ARRIVE, P1GET_FRM_NUM(pFrame),
                   P1GET_REQ_NUM(pFrame));
  std::lock_guard<std::mutex> _l(mPublicLock);
  //
  MUINT32 revResult = (MUINT32)REQ_REV_RES_UNKNOWN;
  if (!acceptRequest(pFrame, &revResult)) {
    mLogInfo.setMemo(LogInfo::CP_REQ_ACCEPT, P1GET_FRM_NUM(pFrame),
                     P1GET_REQ_NUM(pFrame), MFALSE, revResult);
    FUNCTION_OUT;
    return FAILED_TRANSACTION;
  }
  mLogInfo.setMemo(LogInfo::CP_REQ_ACCEPT, P1GET_FRM_NUM(pFrame),
                   P1GET_REQ_NUM(pFrame), MTRUE, revResult);
  //
  lastFrameRequestInfoUpdate(P1GET_FRM_NUM(pFrame), P1GET_REQ_NUM(pFrame));
  //
  LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_REQ_REV, LogInfo::CP_REQ_RET,
                       P1GET_FRM_NUM(pFrame), P1GET_REQ_NUM(pFrame));
  P1_TRACE_F_BEGIN(SLG_I, "P1:queue|Fnum:%d Rnum:%d", P1GET_FRM_NUM(pFrame),
                   P1GET_REQ_NUM(pFrame));
  MY_LOGD("active=%d", getActive());
  //
  MBOOL isStartSet = MFALSE;
  int32_t currReqCnt = 0;
  currReqCnt = std::atomic_fetch_add_explicit(&mInFlightRequestCnt, 1,
                                              std::memory_order_release);
  P1_TRACE_INT(SLG_B, "P1_request_cnt",
               std::atomic_load_explicit(&mInFlightRequestCnt,
                                         std::memory_order_acquire));
  MY_LOGD("InFlightRequestCount++ (%d) => (%d)", currReqCnt,
          std::atomic_load_explicit(&mInFlightRequestCnt,
                                    std::memory_order_acquire));
  //
  if (EN_INIT_REQ_RUN) {
    if (mInitReqCnt <= (mInitReqNum + mBurstNum)) {
      mInitReqCnt++;
    }
  }
  //
  MINT cnt = 0;
  if (EN_INIT_REQ_RUN && (mInitReqCnt < mInitReqNum)) {
    P1QueAct newAct;
    mpTaskCtrl->sessionLock();
    mpTaskCollector->enrollAct(&newAct);
    createAction(&newAct, pFrame);
    cnt = mpTaskCollector->verifyAct(&newAct);
    mTagList.set(cnt);
    mpTaskCtrl->sessionUnLock();
    // else if (mInitReqCnt == mInitReqNum) go-on the following flow
  } else {  // for REV_MODE_NORMAL/REV_MODE_CONSERVATIVE
#if 1       // restart while queue ready
    if (!getActive()) {
      MY_LOGI("HW start +++");
      if (mpConCtrl != nullptr &&
          (!EN_INIT_REQ_RUN)) {  // init-request, no aid-start
        mpConCtrl->setAidUsage(MTRUE);
      }
      MERROR err = hardwareOps_start();
      if ((!(EN_START_CAP || EN_INIT_REQ_RUN)) || (err != OK)) {
        if (mpConCtrl != nullptr && mpConCtrl->getAidUsage() == MTRUE) {
          mpConCtrl->cleanAidStage();
        }
        if (mpTimingCheckerMgr != nullptr) {
          mpTimingCheckerMgr->setEnable(MFALSE);
        }
      }
      if (err != OK) {
        MY_LOGE("Queue Frame - HW start fail (%d)", err);
        P1_TRACE_C_END(SLG_I);  // "P1:queue"
        return err;
      }
      MY_LOGI("HW start ---");
    }
#endif
    if (mpTaskCtrl == nullptr || mpTaskCollector == nullptr) {
      MY_LOGE("Task Controller or Collector not ready");
      P1_TRACE_C_END(SLG_I);  // "P1:queue"
      return BAD_VALUE;
    }
    P1QueAct newAct;
    P1QueAct setAct;
    NS3Av3::MetaSet_T preSet;
    P1Act pSetAct = nullptr;
    mpTaskCtrl->sessionLock();
    //
    mpTaskCollector->enrollAct(&newAct);
    createAction(&newAct, pFrame);
    cnt = mpTaskCollector->verifyAct(&newAct);
    //
    P1Act pAct = GET_ACT_PTR(pAct, newAct, BAD_VALUE);
    if (pAct->ctrlSensorStatus == SENSOR_STATUS_CTRL_STANDBY) {
      MY_LOGI("receive-standby-control");
    } else if (pAct->ctrlSensorStatus == SENSOR_STATUS_CTRL_STREAMING) {
      MY_LOGI("receive-streaming-control");
      hardwareOps_streaming();
    }
    //
    if ((mFirstReceived) && (pAct->reqType == REQ_TYPE_YUV)) {
      P1QueAct paddingAct;
      mpTaskCollector->enrollAct(&paddingAct);
      createAction(&paddingAct, nullptr, REQ_TYPE_PADDING);
      cnt = mpTaskCollector->verifyAct(&paddingAct);
      MY_LOGI("add-padding-for-YUV-stall Id:%d Num:%d Type:%d", paddingAct.id(),
              paddingAct.getNum(), paddingAct.getType());
    }
    mTagList.set(cnt);
    if (IS_BURST_OFF && mFirstReceived && pAct->getType() == ACT_TYPE_NORMAL) {
      mpTaskCollector->queryAct(&setAct);
      pSetAct = setAct.ptr();  // GET_ACT_PTR(pSetAct, firstAct, BAD_VALUE);
      if (pSetAct !=
          nullptr) {  // for the consideration of session locking peroid with 3A
                      // CB and preset, duplicate this MetaSet
        preSet = pSetAct->metaSet;
      } else {
        MY_LOGW("no act ready to PreSet");
      }
    }
    mpTaskCtrl->sessionUnLock();
    if (mp3A && IS_BURST_OFF && mFirstReceived && (pSetAct != nullptr)) {
      if (preSet.PreSetKey <= P1_PRESET_KEY_NULL) {
        MY_LOGW("Pre-Set-Meta NOT ready (%d)", preSet.PreSetKey);
      } else {
        if (preSet.Dummy > 0) {
          MY_LOGI("Pre-Set-Meta is dummy (%d)", preSet.Dummy);
        }
        MINT32 f_Num = pSetAct->frmNum;
        MINT32 r_Num = pSetAct->reqNum;
        std::vector<NS3Av3::MetaSet_T*> ctrlQueue;  // only insert once
        ctrlQueue.push_back(&preSet);
        if (mMetaLogOp > 0 && ctrlQueue.size() > 0) {
          P1_LOG_META(*pSetAct, &(ctrlQueue[0]->appMeta), "3A.PreSet-APP");
          P1_LOG_META(*pSetAct, &(ctrlQueue[0]->halMeta), "3A.PreSet-HAL");
        }
        mLogInfo.setMemo(LogInfo::CP_PRE_SET_BGN, preSet.PreSetKey,
                         preSet.Dummy, f_Num, r_Num);
        P1_TRACE_F_BEGIN(SLG_I, "P1:3A-preset|Pkey:%d Fnum:%d Rnum:%d",
                         preSet.PreSetKey, f_Num, r_Num);
        MY_LOGD("mp3A->preset[%d] +++", preSet.PreSetKey);
        mp3A->preset(ctrlQueue);
        MY_LOGD("mp3A->preset[%d] ---", preSet.PreSetKey);
        P1_TRACE_C_END(SLG_I);  // "P1:3A-preset"
        mLogInfo.setMemo(LogInfo::CP_PRE_SET_END, preSet.PreSetKey,
                         preSet.Dummy, f_Num, r_Num);
        if (1 <= mLogLevelI) {  // P1_LOGI(1)
          pAct->msg += base::StringPrintf(
              " | [PreSet][Key:%d] Num(%d) "
              "Dummy(%d) MetaCnt[APP:%d,HAL:%d]",
              preSet.PreSetKey, preSet.MagicNum, preSet.Dummy,
              preSet.appMeta.count(), preSet.halMeta.count());
        }
      }
    }
    //
    if (!mFirstReceived) {
      if (cnt >= mBurstNum) {
        mFirstReceived = MTRUE;
        isStartSet = MTRUE;
      }  // else not-start and not-wait, then try to receive more requests
    }
    //
    if (1 <= mLogLevelI) {  // P1_LOGI(1)
      P1_TRACE_F_BEGIN(SLG_PFL,
                       "P1::REQ_LOG|Mnum:%d SofIdx:%d Fnum:%d "
                       "Rnum:%d FlushSet:0x%x",
                       pAct->magicNum, pAct->sofIdx, pAct->frmNum, pAct->reqNum,
                       pAct->flushSet);
      pAct->msg += base::StringPrintf(
          " | [Rev:%d] depth(%d) burst(%d) "
          "Que[%d]",
          mReceiveMode, mDepthNum, mBurstNum, mpTaskCollector->remainder());
      P1_LOGI(1, "%s", pAct->msg.c_str());
      P1_TRACE_C_END(SLG_PFL);  // "P1::REQ_LOG"
    }
  }
  //
#if 1  // SET_REQUEST_BEFORE_FIRST_DONE
  if (isStartSet) {
    if (EN_INIT_REQ_RUN && (!getReady())) {
      MY_LOGI("HW request +++");
      MERROR err = hardwareOps_request();
      if (mpConCtrl != nullptr && mpConCtrl->getAidUsage() == MTRUE) {
        mpConCtrl->cleanAidStage();
      }
      if (mpTimingCheckerMgr != nullptr) {
        mpTimingCheckerMgr->setEnable(MFALSE);
      }
      if (err != OK) {
        MY_LOGE("Queue Frame - HW request fail (%d)", err);
        P1_TRACE_C_END(SLG_I);  // "P1:queue"
        return err;
      }
      MY_LOGI("HW request ---");
    } else if (EN_START_CAP && (!getReady())) {
      MY_LOGI("HW capture +++");
      MERROR err = hardwareOps_capture();
      if (mpConCtrl != nullptr && mpConCtrl->getAidUsage() == MTRUE) {
        mpConCtrl->cleanAidStage();
      }
      if (mpTimingCheckerMgr != nullptr) {
        mpTimingCheckerMgr->setEnable(MFALSE);
      }
      if (err != OK) {
        MY_LOGE("Queue Frame - HW capture fail (%d)", err);
        P1_TRACE_C_END(SLG_I);  // "P1:queue"
        return err;
      }
      MY_LOGI("HW capture ---");
    } else {
      // onRequestFrameSet(MTRUE);
      setRequest(MTRUE);
    }
  }
#endif
  //
  if (mpHwStateCtrl != nullptr) {
    mpHwStateCtrl->checkRequest();
  }
  //
  inflightMonitoring(IMT_REQ);
  //
  P1_TRACE_C_END(SLG_I);  // "P1:queue"
  //
  FUNCTION_OUT;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::kick() {
  FUNCTION_IN;
  //
  if ((!getActive()) || (!getReady())) {
    MY_LOGI("return OK - active(%d) ready(%d)", getActive(), getReady());
    return OK;
  }
  if (IS_BURST_ON) {
    MY_LOGI("return OK - BurstNum(%d)", mBurstNum);
    return OK;
  }
  if (CC_UNLIKELY(mpTaskCtrl == nullptr || mpTaskCollector == nullptr)) {
    return BAD_VALUE;
  }
  //
  {
    mpTaskCtrl->sessionLock();
    MINT cnt = mpTaskCollector->remainder();
    P1_TRACE_F_BEGIN(SLG_E, "P1:kick(%d)", cnt);
    MY_LOGI("cnt(%d)", cnt);
    while (cnt > 0) {
      P1QueAct qAct;
      cnt = mpTaskCollector->requireAct(&qAct);
      if (qAct.id() > P1ACT_ID_NULL) {
        P1Act act = GET_ACT_PTR(act, qAct, BAD_VALUE);
        if (act->ctrlSensorStatus != SENSOR_STATUS_CTRL_NONE) {
          MY_LOGI("Cannot KICK Standby Ctrl Request - " P1INFO_ACT_STR,
                  P1INFO_ACT_VAR(*act));
        } else {
          MY_LOGI("KICK - " P1INFO_ACT_STR, P1INFO_ACT_VAR(*act));
          onReturnFrame(&qAct, FLUSH_KICK, MTRUE);
          /* DO NOT use this P1QueAct after onReturnFrame() */
        }
      }
    }
    mTagList.set(cnt);
    P1_TRACE_C_END(SLG_E);  // "P1:kick"
    mpTaskCtrl->sessionUnLock();
  }
  //
  FUNCTION_OUT;
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::flush(std::shared_ptr<IPipelineFrame> const& pFrame) {
  return BaseNode::flush(pFrame);
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::flush() {
  FUNCTION_IN;

  P1_TRACE_AUTO(SLG_B, "P1:flush");

  LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_API_FLUSH_BGN,
                       LogInfo::CP_API_FLUSH_END);

  kick();

  std::lock_guard<std::mutex> _l(mPublicLock);

  onHandleFlush(MFALSE);

  // wait until deque thread going back to waiting state;
  // in case next node receives queue() after flush()

  FUNCTION_OUT;

  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
void P1NodeImp::requestExit() {
  FUNCTION_IN;

  // let deque thread back
  {
    std::lock_guard<std::mutex> _l(mThreadLock);
    mExitPending = MTRUE;
    mThreadCond.notify_all();
  }
  //
  if (mThread.joinable()) {
    mThread.join();
  }

  // let cb thread back
  {
    std::lock_guard<std::mutex> _l(mStartLock);
    mStartCond.notify_all();
  }

  FUNCTION_OUT;
}

/******************************************************************************
 *
 ******************************************************************************/
status_t P1NodeImp::readyToRun() {
  MY_LOGD("readyToRun P1NodeImp thread");
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
bool P1NodeImp::threadLoop() {
  while (this->_threadLoop() == true) {
  }
  MY_LOGI("threadLoop exit");
  return true;
}

/******************************************************************************
 *
 ******************************************************************************/
bool P1NodeImp::_threadLoop() {
  // check if going to leave thread
  P1_TRACE_FUNC(SLG_B);  // P1_TRACE_AUTO(SLG_B, "P1:threadLoop");
  //
  {
    std::unique_lock<std::mutex> lck(mThreadLock);

    if (!getActive()) {
      P1_TRACE_S_BEGIN(SLG_S, "P1:wait_active");
      MY_LOGD("wait active+");
      mThreadCond.wait(lck);
      MY_LOGD("wait active-");
      P1_TRACE_C_END(SLG_S);  // "P1:wait_active"
    }

    if (mExitPending) {
      MY_LOGD("leaving active");
      return false;
    }
  }
  //
  if (mpConCtrl != nullptr && mpConCtrl->getAidUsage()) {
    procedureAid_start();
  }
  //
  {
    std::unique_lock<std::mutex> lck(mThreadLock);

    if (getActive() && !getReady()) {
      P1_TRACE_S_BEGIN(SLG_S, "P1:wait_ready");
      MY_LOGD("wait ready+");
      mThreadCond.wait(lck);
      MY_LOGD("wait ready-");
      P1_TRACE_C_END(SLG_S);  // "P1:wait_ready"
    }

    if (mExitPending) {
      MY_LOGD("leaving ready");
      return false;
    }
  }

  if (mpHwStateCtrl != nullptr) {
    mpHwStateCtrl->checkThreadStandby();
  }

  // deque buffer, and handle frame and metadata
  onProcessDequeFrame();

  if (!getActive()) {
    MY_LOGI_IF(getInit(), "HW stopped , exit init");
    setInit(MFALSE);
  }

  // trigger point for the first time
  {
    if (getInit()) {
      setInit(MFALSE);
    }
  }

  if ((mpDeliverMgr != nullptr) && (mpDeliverMgr->runningGet())) {
    onProcessDropFrame(MTRUE);
  }

  return true;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::setActive(MBOOL active) {
  std::lock_guard<std::mutex> _l(mActiveLock);
  mActive = active;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1NodeImp::getActive(void) {
  std::lock_guard<std::mutex> _l(mActiveLock);
  return mActive;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::setReady(MBOOL ready) {
  std::lock_guard<std::mutex> _l(mReadyLock);
  mReady = ready;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1NodeImp::getReady(void) {
  std::lock_guard<std::mutex> _l(mReadyLock);
  return mReady;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::setInit(MBOOL init) {
  std::lock_guard<std::mutex> _l(mInitLock);
  mInit = init;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1NodeImp::getInit(void) {
  std::lock_guard<std::mutex> _l(mInitLock);
  return mInit;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::setPowerNotify(MBOOL notify) {
  std::lock_guard<std::mutex> _l(mPowerNotifyLock);
  mPowerNotify = notify;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1NodeImp::getPowerNotify(void) {
  std::lock_guard<std::mutex> _l(mPowerNotifyLock);
  return mPowerNotify;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::setStartState(MUINT8 state) {
  std::lock_guard<std::mutex> _l(mStartStateLock);
  mStartState = state;
}

/******************************************************************************
 *
 ******************************************************************************/
MUINT8
P1NodeImp::getStartState(void) {
  std::lock_guard<std::mutex> _l(mStartStateLock);
  return mStartState;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::setQualitySwitching(MBOOL switching) {
  std::lock_guard<std::mutex> _l(mQualitySwitchLock);
  mQualitySwitching = switching;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1NodeImp::getQualitySwitching(void) {
  std::lock_guard<std::mutex> _l(mQualitySwitchLock);
  return mQualitySwitching;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::setCurrentBinSize(MSize size) {
  std::lock_guard<std::mutex> _l(mCurBinLock);
  mCurBinSize = size;
}

/******************************************************************************
 *
 ******************************************************************************/
MSize P1NodeImp::getCurrentBinSize(void) {
  std::lock_guard<std::mutex> _l(mCurBinLock);
  return mCurBinSize;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::lastFrameRequestInfoUpdate(MINT32 const frameNum,
                                      MINT32 const requestNum) {
  std::lock_guard<std::mutex> _l(mLastFrmReqNumLock);
  mLastFrmNum = frameNum;
  mLastReqNum = requestNum;
  mLastCbCnt = 0;
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MINT32
P1NodeImp::lastFrameRequestInfoNotice(MINT32* frameNum,
                                      MINT32* requestNum,
                                      MINT32 const addCbCnt) {
  std::lock_guard<std::mutex> _l(mLastFrmReqNumLock);
  *frameNum = mLastFrmNum;
  *requestNum = mLastReqNum;
  if (addCbCnt != 0) {
    mLastCbCnt += (addCbCnt);
  }
  return mLastCbCnt;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::syncHelperStart() {
  std::lock_guard<std::mutex> _l(mSyncHelperLock);
  if (!mSyncHelperReady) {
    if (mspSyncHelper != nullptr) {
      status_t res = mspSyncHelper->start(getOpenId());
      if (res == OK) {
        mSyncHelperReady = MTRUE;
      }
    }
  }
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::syncHelperStop() {
  std::lock_guard<std::mutex> _l(mSyncHelperLock);
  if (mSyncHelperReady) {
    if (mspSyncHelper != nullptr) {
      status_t res = mspSyncHelper->stop(getOpenId());
      if (res == OK) {
        mSyncHelperReady = MFALSE;
      }
    }
  }
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
void P1NodeImp::ensureStartReady(MUINT8 infoType, MINT32 infoNum) {
  std::cv_status res;
  MUINT32 needRetry = P1NODE_START_READY_WAIT_CNT_MAX;
  if (getActive()) {
    std::unique_lock<std::mutex> lck(mStartLock);
    while ((!getReady()) && (needRetry != 0) &&
           ((getStartState() != NSP1Node::START_STATE_READY) &&
            (getStartState() >= NSP1Node::START_STATE_DRV_START))) {
      res = mStartCond.wait_for(
          lck, std::chrono::nanoseconds(P1NODE_START_READY_WAIT_INV_NS));
      needRetry--;
      MY_LOGI(
          "Type(%d) Num(%d) - EnStartCap(%d) EnInitReqRun(%d) - "
          "StartState(%d) WaitStatus(%d) NeedRetry(%d)",
          infoType, infoNum, EN_START_CAP, EN_INIT_REQ_RUN, getStartState(),
          res, needRetry);
      if (!getActive()) {
        MY_LOGI("Not Active");
        break;
      }
      if (res != std::cv_status::timeout) {
        MY_LOGI("Got Ready");
        break;
      }
    }
  }
  if ((getActive()) && (!getReady())) {
    MY_LOGE(
        "Wait StartReady Timeout (%d*%d ms) - "
        "Type(%d) Num(%d) - EnStartCap(%d) EnInitReqRun(%d) - "
        "StartState(%d) WaitStatus(%d) NeedRetry(%d)",
        P1NODE_START_READY_WAIT_CNT_MAX,
        (MUINT32)(P1NODE_START_READY_WAIT_INV_NS / ONE_MS_TO_NS), infoType,
        infoNum, EN_START_CAP, EN_INIT_REQ_RUN, getStartState(), res,
        needRetry);
  }
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::onSyncEnd() {
  FUNCTION_IN;
  //
  {
    NS3Av3::IpcPeriSensorData_T data;
    if (mpAccDetector->getAcceleration(data.acceleration))
      mp3A->send3ACtrl(NS3Av3::E3ACtrl_IPC_SetPeriSensorData,
                       reinterpret_cast<MINTPTR>(&data), 0);
  }
  MBOOL toSet = MFALSE;
  if (mpHwStateCtrl != nullptr) {
    if (mpHwStateCtrl->checkSkipSync()) {
      MY_LOGI("SyncEND was paused");
      return;
    }
    MBOOL first = mpHwStateCtrl->checkFirstSync();
    MY_LOGI_IF(first, "Got first CB after re-streaming");
    if (first && IS_BURST_ON) {
      toSet = MTRUE;
    }
  }
  //
  {
    std::lock_guard<std::mutex> _ll(mFrameSetLock);
    if (!mFrameSetAlready) {
      MY_LOGI("should not callback before first set");
      return;
    }
    if (EN_START_CAP && (!getReady())) {
      std::lock_guard<std::mutex> _l(mStartCaptureLock);
      MY_LOGD("StartCaptureState(%d)", mStartCaptureState);
      if (mStartCaptureState != START_CAP_STATE_READY) {
        MY_LOGI("should not callback before capture ready (%d)",
                mStartCaptureState);
        return;
      }
    }
  }
  //
  if (getInit()) {
    MY_LOGI("sync before frame done");
  }
  //
  if ((getActive()) && (!getReady())) {
    ensureStartReady(IHal3ACb::eID_NOTIFY_VSYNC_DONE);
  }
  //
  P1_TRACE_F_BEGIN(SLG_I, "P1:onSyncEnd|TheLastSet-Mnum:%d", mLastSetNum);
  //
  if (IS_BURST_OFF || toSet) {
    setRequest(MFALSE);
  }
  //
  P1_TRACE_C_END(SLG_I);  // "P1:onSyncEnd"
  //
  FUNCTION_OUT;
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::onSyncBegin(MBOOL initial,
                       NS3Av3::RequestSet_T* reqSet,
                       MUINT32 sofIdx,
                       NS3Av3::CapParam_T* capParam) {
  FUNCTION_IN;
  if (mpHwStateCtrl != nullptr) {
    if (mpHwStateCtrl->checkSkipSync()) {
      MY_LOGI("SyncBGN was paused");
      return;
    }
    MBOOL first = mpHwStateCtrl->checkFirstSync();
    MY_LOGI_IF(first, "Got first CB after re-streaming");
  }
  //
  {
    std::lock_guard<std::mutex> _ll(mFrameSetLock);
    if (!mFrameSetAlready) {
      MY_LOGI("should not callback before first set");
      return;
    }
    //
    if (EN_START_CAP && (!getReady())) {
      std::lock_guard<std::mutex> _l(mStartCaptureLock);
      MY_LOGD("StartCaptureState(%d)", mStartCaptureState);
      if (mStartCaptureState == START_CAP_STATE_WAIT_CB) {
        if (capParam != nullptr) {
          mStartCaptureType = capParam->u4CapType;
          mStartCaptureIdx = sofIdx;
          mStartCaptureExp = MAX(capParam->i8ExposureTime, 0);
          if (reqSet != nullptr && reqSet->vNumberSet.size() > 0 &&
              IS_BURST_OFF) {
            mLongExp.set(reqSet->vNumberSet[0], mStartCaptureExp);
          }
        }
        mStartCaptureState = START_CAP_STATE_READY;
        mStartCaptureCond.notify_all();
        MY_LOGI(
            "StartCaptureReady @%d init(%d) Cap-Type(%d)-Idx(%d)"
            "-Exp(%" PRId64 "ns)",
            sofIdx, getInit(), mStartCaptureType, mStartCaptureIdx,
            mStartCaptureExp);
        return;
      } else if (mStartCaptureState == START_CAP_STATE_WAIT_REQ) {
        MY_LOGI("should not callback before capture set (%d)",
                mStartCaptureState);
        return;
      }
    }
  }
  //
  if (getInit()) {
    MY_LOGI("sync before frame done");
  }
  //
  MINT32 magicNum = P1_MAGIC_NUM_NULL;
  if (reqSet != nullptr && reqSet->vNumberSet.size() > 0) {
    magicNum = reqSet->vNumberSet[0];
  }
  //
  if ((getActive()) && (!getReady())) {
    ensureStartReady(IHal3ACb::eID_NOTIFY_3APROC_FINISH, magicNum);
  }
  //
  P1_TRACE_F_BEGIN(SLG_I,
                   "P1:onSyncBegin|"
                   "CB Mnum:%d SofIdx:%d Exp(ns):%" PRId64 " Type:%d",
                   magicNum, sofIdx, capParam->i8ExposureTime,
                   capParam->u4CapType);
  //
  // (1)
  if ((!initial) && (getReady())) {
    P1QueJob job(mBurstNum);
    bool exist = false;
    {
      std::lock_guard<std::mutex> _l(mRequestQueueLock);
      Que_T::iterator it = mRequestQueue.begin();
      for (; it != mRequestQueue.end(); it++) {
        if ((*it).getIdx() == magicNum) {
          job = *it;
          for (MUINT8 i = 0; i < job.size(); i++) {
            P1Act act = GET_ACT_PTR(act, job.edit(i), RET_VOID);
            act->sofIdx = sofIdx;
            if (capParam != nullptr) {
              act->capType = capParam->u4CapType;
              act->frameExpDuration = MAX(capParam->i8ExposureTime, 0);
              if (act->capType == NS3Av3::E_CAPTURE_HIGH_QUALITY_CAPTURE
                  /*&& mRawPostProcSupport == MFALSE*/) {  // no matter
                                                           // legacy/non-legacy
                                                           // platform, HQC need
                                                           // pure raw
                if (act->fullRawType != EPipe_PURE_RAW) {
                  act->isRawTypeChanged = MTRUE;
                  MY_LOGI(
                      "HQC (%d) - full raw type change"
                      " (%d => %d)",
                      mRawPostProcSupport, act->fullRawType, EPipe_PURE_RAW);
                }
                act->fullRawType = EPipe_PURE_RAW;
              }
              if (IS_BURST_OFF) {
                mLongExp.set(act->magicNum, act->frameExpDuration);
              }
              MY_LOGI_IF(((capParam->i8ExposureTime >= 400000000) ||
                          (capParam->i8ExposureTime <= 0)),
                         "check CB "
                         "num(%d) cap(%d) exp(%" PRId64 "ns)",
                         magicNum, capParam->u4CapType,
                         capParam->i8ExposureTime);
              if ((act->capType != NS3Av3::E_CAPTURE_NORMAL) &&
                  (act->appFrame != nullptr)) {
                MY_LOGI("Job(%d) - Cap(%d)(%" PRId64 "ns) - " P1INFO_ACT_STR,
                        job.getIdx(), capParam->u4CapType,
                        capParam->i8ExposureTime, P1INFO_ACT_VAR(*act));
              }
            } else {
              MY_LOGW("cannot find cap param (%d)", magicNum);
            }
          }
          //
          if (it != mRequestQueue.begin()) {
            std::string str;
            str += base::StringPrintf(
                "MissingCallback from 3A : "
                "this CB Mnum(%d) ; current ReqQ[%d] = [ ",
                magicNum, static_cast<int>(mRequestQueue.size()));
            Que_T::iterator it = mRequestQueue.begin();
            for (; it != mRequestQueue.end(); it++) {
              str += base::StringPrintf("%d ", (*it).getIdx());
            }
            str += base::StringPrintf("] @ SOF(%d)", sofIdx);
            MY_LOGW("%s", str.c_str());
          }
          //
          mRequestQueue.erase(it);
          exist = true;
          break;
        }
      }
    }
    if (exist) {
      {
        std::lock_guard<std::mutex> _ll(mTransferJobLock);
        mTransferJobIdx = job.getIdx();
      }
      //
      if (OK != onProcessEnqueFrame(&job)) {
        MY_LOGE("frame en-queue fail (%d)", magicNum);
        for (MUINT8 i = 0; i < job.size(); i++) {
          onReturnFrame(&job.edit(i), FLUSH_FAIL, MTRUE);
          /* DO NOT use this P1QueAct after onReturnFrame() */
        }
      } else {
        if (  // IS_BURST_OFF && // exclude burst mode
            (job.size() >= 1)) {
          P1Act act = GET_ACT_PTR(act, job.edit(0), RET_VOID);
          if ((act->reqType == REQ_TYPE_NORMAL && act->appFrame != nullptr) &&
              (capParam != nullptr && capParam->metadata.count() > 0)) {
            requestMetadataEarlyCallback(&(job.edit(0)), STREAM_META_OUT_HAL,
                                         &(capParam->metadata));
          }
        }
        //
        P1Act pAct = GET_ACT_PTR(pAct, job.edit(0), RET_VOID);
        if (mpHwStateCtrl != nullptr &&
            pAct->ctrlSensorStatus == SENSOR_STATUS_CTRL_STANDBY) {
          MBOOL isAct = MFALSE;
          isAct = mpHwStateCtrl->checkCtrlStandby(pAct->getNum());
          // it might call doNotifyDropframe() in DRV->suspend()
          if ((isAct) &&
              ((mpDeliverMgr != nullptr) && (mpDeliverMgr->runningGet()))) {
            MY_LOGI("DRV-suspend executed : check drop-frame");
            onProcessDropFrame(MTRUE);
          }
        }
      }
      //
      {
        std::lock_guard<std::mutex> _ll(mTransferJobLock);
        mTransferJobIdx = P1ACT_ID_NULL;
        if (CC_UNLIKELY(mTransferJobWaiting)) {
          mTransferJobCond.notify_all();
        }
      }
    } else {
#if (IS_P1_LOGI)
      std::lock_guard<std::mutex> _l(mRequestQueueLock);
      std::string str;
      str += base::StringPrintf("[req(%d)/size(%d)]: ", magicNum,
                                static_cast<int>(mRequestQueue.size()));
      Que_T::iterator it = mRequestQueue.begin();
      for (; it != mRequestQueue.end(); it++) {
        str += base::StringPrintf("%d ", (*it).getIdx());
      }
      MY_LOGI("%s", str.c_str());
#endif
    }
  }
  //
  if (IS_BURST_ON) {
    MBOOL skip = MFALSE;
    if (mpHwStateCtrl != nullptr) {
      skip = mpHwStateCtrl->checkSkipSync();
    }
    if (skip) {
      MY_LOGI("FrameSet was paused");
    } else {
      setRequest(MFALSE);
    }
  }
  //
  P1_TRACE_C_END(SLG_I);  // "P1:onSyncBegin"
  //
  inflightMonitoring(IMT_ENQ);
  //
  FUNCTION_OUT;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::onProcessEnqueFrame(P1QueJob* job) {
  FUNCTION_IN;

  // (1) todo
  // pass request directly if it's a reprocessing one
  //
  // if( mInHalMeta == NULL) {
  //    onDispatchFrame(pFrame);
  //    return;
  // }

  // (2)
  MERROR status = hardwareOps_enque(job);

  FUNCTION_OUT;
  return status;
}

/******************************************************************************
 *
 ******************************************************************************/
P1QueJob P1NodeImp::getProcessingFrame_ByNumber(MINT32 magicNum) {
  FUNCTION_IN;
  P1QueJob job(mBurstNum);

  std::lock_guard<std::mutex> _l(mProcessingQueueLock);
  if (mProcessingQueue.empty()) {
    MY_LOGE("mProcessingQueue is empty");
    return job;
  }

  Que_T::iterator it = mProcessingQueue.begin();
  for (; it != mProcessingQueue.end(); it++) {
    if ((*it).getIdx() == magicNum) {
      break;
    }
  }
  if (it == mProcessingQueue.end()) {
    MY_LOGI("cannot find the right act for num: %d", magicNum);
    job.clear();
    return job;
  } else {
    job = *it;
    mProcessingQueue.erase(it);
    mProcessingQueueCond.notify_all();
  }

  FUNCTION_OUT;
  //
  return job;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1NodeImp::getProcessingFrame_ByAddr(IImageBuffer* const imgBuffer,
                                     MINT32 magicNum,
                                     P1QueJob* job) {
  FUNCTION_IN;

  MBOOL ret = MFALSE;
  if (imgBuffer == nullptr) {
    MY_LOGE("imgBuffer == NULL");
    return ret;
  }

  // get the right act from mProcessingQueue
  MINT32 gotNum = 0;
  std::vector<MINT32>
      vStoreNum;  // not reserve since it will not insert in the most case
  vStoreNum.clear();
  {
    std::lock_guard<std::mutex> _l(mProcessingQueueLock);
    if (mProcessingQueue.empty()) {
      MY_LOGE("ProQ is empty");
      return ret;
    }
    //
    Que_T::iterator it = mProcessingQueue.begin();
    for (; it != mProcessingQueue.end(); it++) {
      P1Act act = GET_ACT_PTR(act, (*it).edit(0), MFALSE);
      if (imgBuffer == act->streamBufImg[STREAM_IMG_OUT_FULL].spImgBuf.get() ||
          imgBuffer ==
              act->streamBufImg[STREAM_IMG_OUT_OPAQUE].spImgBuf.get() ||
          imgBuffer ==
              act->streamBufImg[STREAM_IMG_OUT_RESIZE].spImgBuf.get() ||
          imgBuffer == act->streamBufImg[STREAM_IMG_OUT_LCS].spImgBuf.get() ||
          imgBuffer == act->streamBufImg[STREAM_IMG_OUT_RSS].spImgBuf.get()) {
        gotNum = (*it).getIdx();
        if ((*it).getIdx() == magicNum) {
          ret = MTRUE;
        } else {
#if SUPPORT_PERFRAME_CTRL
          MY_LOGE("magicNum from driver(%d), should(%d)", magicNum,
                  (*it).getIdx());
#else
          if ((magicNum & P1NODE_COMMON_MAGICNUM_MASK) != 0) {
            MY_LOGW("magicNum from driver(0x%x) is uncertain", magicNum);
            ret = MFALSE;
          } else {
            ret = MTRUE;
            MY_LOGW("magicNum from driver(%d), should(%d)", magicNum,
                    (*it).getIdx());
          }
#endif
          // reset act from 3A info
          for (size_t i = 0; i < (*it).size(); i++) {
            P1Act pAct = GET_ACT_PTR(pAct, (*it).edit(i), MFALSE);
            pAct->capType = NS3Av3::E_CAPTURE_NORMAL;
            pAct->frameExpDuration = 0;
          }
        }
        break;
      }
    }
    //
    if (it == mProcessingQueue.end()) {
      MY_LOGE("no act with imagebuf(%p), num(%d)", imgBuffer, magicNum);
#if 1  // dump ProcessingQ info
      char const* str[STREAM_IMG_NUM] = {"YUV-in", "RAW-in", "OPQ", "IMG",
                                         "RRZ",    "LCS",    "RSS"};
      for (Que_T::iterator j = mProcessingQueue.begin();
           j != mProcessingQueue.end(); j++) {
        for (size_t i = 0; i < (*j).size(); i++) {
          P1Act act = GET_ACT_PTR(act, (*j).edit(i), MFALSE);
          MY_LOGW("[ProQ] [%zu] : num(%d)", i, act->magicNum);
          for (int s = STREAM_ITEM_START; s < STREAM_IMG_NUM; s++) {
            if (act->streamBufImg[s].bExist && act->streamBufImg[s].spImgBuf) {
              std::shared_ptr<IImageBuffer> pBuf =
                  act->streamBufImg[s].spImgBuf;
              MY_LOGW("[ProQ] [%zu] : %s(%p)(P:%p)(V:%p)", i,
                      ((str[s] != nullptr) ? str[s] : "UNKNOWN"),
                      reinterpret_cast<void*>(pBuf.get()),
                      reinterpret_cast<void*>(pBuf->getBufPA(0)),
                      reinterpret_cast<void*>(pBuf->getBufVA(0)));
            }
          }
        }
      }
#endif
    } else {
      //
      if (it != mProcessingQueue.begin()) {
        size_t queSize = mProcessingQueue.size();
        MINT queNum = 0;
        Que_T::iterator it_stored = mProcessingQueue.begin();
        for (; it_stored < it; it_stored++) {
          for (size_t i = 0; i < (*it_stored).size(); i++) {
            queNum = ((*it_stored).edit(i).getNum());
            vStoreNum.push_back(queNum);
            MY_LOGI(
                "Non-Dequeued frame(Mnum:%d) in ProcQue[%zu] "
                "current(%d)",
                queNum, queSize, gotNum);
          }
        }
      }
      *job = *it;
      mProcessingQueue.erase(it);
      mProcessingQueueCond.notify_all();
      MY_LOGD("magic: %d", magicNum);
    }
  }  // mProcessingQueueLock.unlock();
  // avoid to execute mpHwStateCtrl functions under mProcessingQueueLock
  MBOOL isPauseDrop = MFALSE;
  if (mpHwStateCtrl != nullptr) {
    if (CC_UNLIKELY(gotNum > 0 && mpHwStateCtrl->checkDoneNum(gotNum))) {
      for (size_t idx = 0; idx < vStoreNum.size(); idx++) {
        MY_LOGI("DropStoreNum[%zu] : %d", idx, vStoreNum[idx]);
        mpHwStateCtrl->setDropNum(vStoreNum[idx]);
      }
      isPauseDrop = MTRUE;
    }
  }
  if (CC_UNLIKELY((!vStoreNum.empty()) && (!isPauseDrop))) {
    size_t nSize = vStoreNum.size();
    if (nSize > 0 && (vStoreNum[0] + P1NODE_DEF_PROCESS_DEPTH) < gotNum) {
      MY_LOGW(
          "[De-queued Frame Skipped] NonDequeuedFrameCount[%zu]:(%d)"
          " - CurrentDequeuedFrameMnum(%d)"
          " - Please Check the DRV Dequeue/Drop Flow",
          nSize, vStoreNum[0], gotNum);
    }
    for (size_t idx = 0; idx < nSize; idx++) {
      MY_LOGI("NonDequeued[%zu/%zu] = FrameMnum(%d) - current(%d)", idx, nSize,
              vStoreNum[idx], gotNum);
    }
  }
  //
  FUNCTION_OUT;
  //
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::onCheckDropFrame(void) {
  MUINT cnt = 0;
  MINT32 num = 0;
  if (mpHwStateCtrl != nullptr) {
    do {
      num = mpHwStateCtrl->getDropNum();
      if (num > 0) {
        std::lock_guard<std::mutex> _l(mDropQueueLock);
        mDropQueue.push_back(num);
        cnt++;
      }
    } while (num > 0);
  }
  if ((cnt > 0) && (mpDeliverMgr != nullptr) && (mpDeliverMgr->runningGet())) {
    MY_LOGI("check drop frame (%d)", cnt);
    onProcessDropFrame(MTRUE);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::onProcessDropFrame(MBOOL isTrigger) {
  mDropQueueLock.lock();
  if (mDropQueue.empty()) {
    mDropQueueLock.unlock();
    return OK;
  }
  std::vector<P1QueAct>
      actQ;  // not reserve since it will not insert in the most case
  for (size_t i = 0; i < mDropQueue.size(); i++) {
    P1QueJob job = getProcessingFrame_ByNumber(mDropQueue[i]);
    // if getProcessingFrame_ByNumber can not find the job
    // the job set size is 0
    for (MUINT8 i = 0; i < job.size(); i++) {
      P1QueAct act = job.edit(i);
      actQ.push_back(act);
    }
    MY_LOGI("drop[%zu/%zu]: %d", i, mDropQueue.size(), mDropQueue[i]);
    P1_LOGI(0, "DropQueue[%zu/%zu] = %d", i, mDropQueue.size(), mDropQueue[i]);
  }
  mDropQueue.clear();
  mDropQueueLock.unlock();
  //
  for (size_t i = 0; i < actQ.size(); i++) {
    //
    P1Act pAct = GET_ACT_PTR(pAct, actQ.at(i), BAD_VALUE);
    if (IS_BURST_OFF) {
      mLongExp.reset(pAct->magicNum);
    }
    if (IS_LMV(mpConnectLMV) && (pAct->buffer_eiso != NULL) && getActive()) {
      MY_LOGD("processDropFrame");
      mpConnectLMV->processDropFrame(&(pAct->buffer_eiso));
    }
    pAct->exeState = EXE_STATE_DONE;
    onReturnFrame(&actQ.at(i), FLUSH_DROP,
                  ((isTrigger) && (i == (actQ.size() - 1))) ? MTRUE : MFALSE);
    /* DO NOT use this P1QueAct after onReturnFrame() */
  }

  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::onProcessDequeFrame() {
#if 0
    // [FIXME]  temp-WA for DRV currently not implement self-signal
    //          the dequeue might be blocked while en-queue empty
    //          it should be removed after DRV self-signal ready
    {
        std::lock_guard<std::mutex> _ll(mProcessingQueueLock);
        if (mProcessingQueue.empty()) {
            return OK;
        }
    }
#endif

  FUNCTION_IN;

  MERROR ret = OK;
  QBufInfo deqBuf;
  if (hardwareOps_deque(&deqBuf) != OK) {
    MY_LOGW("hardwareOps_deque error");
    return BAD_VALUE;
  }

  if (deqBuf.mvOut.size() == 0) {
    MBOOL normal_case = (!getActive());
    if ((!normal_case) && (mpHwStateCtrl != nullptr)) {
      normal_case = (mpHwStateCtrl->checkBufferState());
    }
    MY_LOGI("DeqBuf Out Size is 0 (act:%d,%d)", getActive(), normal_case);
    return ((normal_case) ? OK : BAD_VALUE);
  }

  MY_LOGI("HwLockProcessWait +++");
  std::lock_guard<std::mutex> _l(mHardwareLock);
  MY_LOGI("HwLockProcessWait ---");

  P1QueJob job(mBurstNum);
  MBOOL match = getProcessingFrame_ByAddr(
      deqBuf.mvOut[0].mBuffer, deqBuf.mvOut[0].mMetaData.mMagicNum_hal, &job);
  {
    std::unique_lock<std::mutex> _ll(mTransferJobLock);
    std::cv_status res;
    MUINT32 needRetry = P1NODE_TRANSFER_JOB_WAIT_CNT_MAX;
    while ((match) && (mTransferJobIdx != P1ACT_ID_NULL) &&
           (mTransferJobIdx == job.getIdx()) && (needRetry != 0)) {
      mTransferJobWaiting = MTRUE;
      res = mTransferJobCond.wait_for(
          _ll, std::chrono::nanoseconds(P1NODE_TRANSFER_JOB_WAIT_INV_NS));
      needRetry--;
      MY_LOGI(
          "TransferJob(%d) ThisJob(%d) - WaitStatus(%d) "
          "NeedRetry(%d)",
          mTransferJobIdx, job.getIdx(), res, needRetry);
      if (res != std::cv_status::timeout) {
        MY_LOGI("Got Job");
        break;
      }
    }
    mTransferJobWaiting = MFALSE;
    if ((res == std::cv_status::timeout) && (mTransferJobIdx == job.getIdx())) {
      MY_LOGE("TransferJob(%d) Not-Ready : (%d)", mTransferJobIdx, res);
    }
  }
  onCheckDropFrame();  // must call after getProcessingFrame_ByAddr()
  //
  if (IS_BURST_OFF) {
    mLongExp.reset(deqBuf.mvOut[0].mMetaData.mMagicNum_hal);
  }
  //
  if (!findPortBufIndex(deqBuf, &job)) {
    return BAD_VALUE;
  }
  //
  for (MUINT8 i = 0; i < (MUINT8)job.size(); i++) {
    P1QueAct qAct = job.edit(i);
    P1Act act = GET_ACT_PTR(act, qAct, BAD_VALUE);
    NS3Av3::MetaSet_T result3A;
    // camera display systrace - DeQ
    if (act->appFrame != nullptr) {
      MINT64 const timestamp = deqBuf.mvOut[i].mMetaData.mTimeStamp;
      P1_TRACE_F_BEGIN(
          SLG_B,  // add information
          "Cam:%d:IspP1:deq|timestamp(ns):%" PRId64 " duration(ns):%" PRId64
          " request:%d frame:%d",
          getOpenId(), timestamp, NSCam::Utils::getTimeInNs() - timestamp,
          act->appFrame->getRequestNo(), act->appFrame->getFrameNo());
      P1_TRACE_C_END(SLG_B);  // "IspP1:deq"
    }

    MY_LOGD("job(%d)[%d] = act(%d)", job.getIdx(), i, act->magicNum);
    mTagDeq.set(qAct.getNum());
#if SUPPORT_3A
    {
      std::lock_guard<std::mutex> _ssl(mStopSttLock);
      if (getActive() && mp3A && act->reqType == REQ_TYPE_NORMAL) {
        MBOOL drop_notify = MFALSE;
        MINT32 ret = 0;
        mp3A->notifyP1Done(act->magicNum);
        if (match && act->capType == NS3Av3::E_CAPTURE_HIGH_QUALITY_CAPTURE) {
          P1_TRACE_F_BEGIN(SLG_I,
                           "P1:3A-getCur|"
                           "Mnum:%d SofIdx:%d Fnum:%d Rnum:%d",
                           act->magicNum, act->sofIdx, act->frmNum,
                           act->reqNum);
          MY_LOGD("mp3A->getCur(%d) +++", act->magicNum);
          ret = mp3A->getCur(act->magicNum, &result3A);
          if (ret < 0) {  // 0:success
            drop_notify = MTRUE;
            MY_LOGI("drop-frame by 3A GetC(%d) @ (%d)(%d:%d)", ret,
                    act->magicNum, act->frmNum, act->reqNum);
          }
          MY_LOGD("mp3A->getCur(%d) ---", act->magicNum);
          P1_TRACE_C_END(SLG_I);  // "P1:3A-getCur"
        } else {
          P1_TRACE_F_BEGIN(SLG_I,
                           "P1:3A-get|"
                           "Mnum:%d SofIdx:%d Fnum:%d Rnum:%d",
                           act->magicNum, act->sofIdx, act->frmNum,
                           act->reqNum);
          MY_LOGD("mp3A->get(%d) +++", act->magicNum);
          ret = mp3A->get(act->magicNum, &result3A);
          if (ret < 0) {  // 0:success
            drop_notify = MTRUE;
            MY_LOGI("drop-frame by 3A Get(%d) @ (%d)(%d:%d)", ret,
                    act->magicNum, act->frmNum, act->reqNum);
          }
          MY_LOGD("mp3A->get(%d) ---", act->magicNum);
          P1_TRACE_C_END(SLG_I);  // "P1:3A-get"
        }
        IMetadata::IEntry entry;
        entry = result3A.appMeta.entryFor(MTK_TONEMAP_START);
        if (entry.tag() != IMetadata::IEntry::BAD_TAG) {
          MY_LOGD("find the entry for MTK_TONEMAP_START *****");
          MUINT32 t;
          for (t = MTK_TONEMAP_START; t < MTK_TONEMAP_MODE; t++) {
            result3A.appMeta.remove(t);
          }
        }
        entry = result3A.appMeta.entryFor(MTK_EDGE_MODE);
        if (entry.tag() != IMetadata::IEntry::BAD_TAG) {
          result3A.appMeta.remove(MTK_EDGE_MODE);
        }
        entry = result3A.appMeta.entryFor(MTK_NOISE_REDUCTION_MODE);
        if (entry.tag() != IMetadata::IEntry::BAD_TAG) {
          result3A.appMeta.remove(MTK_NOISE_REDUCTION_MODE);
        }
        entry = result3A.appMeta.entryFor(MTK_JPEG_QUALITY);
        if (entry.tag() != IMetadata::IEntry::BAD_TAG) {
          result3A.appMeta.remove(MTK_JPEG_QUALITY);
        }
        entry = result3A.appMeta.entryFor(MTK_JPEG_THUMBNAIL_QUALITY);
        if (entry.tag() != IMetadata::IEntry::BAD_TAG) {
          result3A.appMeta.remove(MTK_JPEG_THUMBNAIL_QUALITY);
        }
        P1_LOG_META(*act, &(result3A.appMeta), "3A.Get-APP");
        P1_LOG_META(*act, &(result3A.halMeta), "3A.Get-HAL");
        if (!match) {
          act->setFlush(FLUSH_MIS_BUFFER);
        }
        if (drop_notify) {
          act->setFlush(FLUSH_MIS_RESULT);
          match = MFALSE;
        }
      }
    }
#endif

    // check the ReqExpRec
    if (match && (act->expRec != EXP_REC_NONE)) {
      switch (act->reqType) {
        case REQ_TYPE_NORMAL:
          MY_LOGI("check ExpRec " P1INFO_ACT_STR, P1INFO_ACT_VAR(*act));
          break;
        default:  // REQ_TYPE_INITIAL/REQ_TYPE_DUMMY/REQ_TYPE_PADDING
          MY_LOGI("ExpRec " P1INFO_ACT_STR, P1INFO_ACT_VAR(*act));
          break;
      }
    }
    // check the result of raw type
    if (match) {
      MUINT32 port_index = act->portBufIndex[P1_OUTPUT_PORT_IMGO];
      if (port_index != P1_PORT_BUF_IDX_NONE) {
        MBOOL raw_match = MTRUE;
        MUINT32 res_raw = deqBuf.mvOut[port_index].mMetaData.mRawType;
        MINT64 set_raw = (res_raw == (MUINT32)EPipe_PROCESSED_RAW)
                             ? (MINT64)(eIMAGE_DESC_RAW_TYPE_PROCESSED)
                             : (MINT64)(eIMAGE_DESC_RAW_TYPE_PURE);
        if ((act->fullRawType == EPipe_PROCESSED_RAW) &&
            res_raw != (MUINT32)EPipe_PROCESSED_RAW) {
          raw_match = MFALSE;
          // only check Processed-Raw with (mRawType == EPipe_PROCESSED_RAW)
          // in Pure-Raw, it would be EPipe_PURE_RAW or others
        }

        if (!raw_match) {
          MY_LOGE("RawType mismatch DEQ(%d) REQ(%d)" P1INFO_ACT_STR, res_raw,
                  act->fullRawType, P1INFO_ACT_VAR(*act));
#if 1  // flush this frame
          act->setFlush(FLUSH_MIS_RAW);
          match = MFALSE;
#endif
        } else {
          IImageBuffer* pBuf = deqBuf.mvOut[port_index].mBuffer;
          if (pBuf != nullptr) {
            MBOOL res =
                pBuf->setImgDesc(eIMAGE_DESC_ID_RAW_TYPE, set_raw, MTRUE);
            MY_LOGD("ImgBufRawType(%" PRId64 ") %d", set_raw, res);
          }
        }
      }
    }
    act->frameTimeStamp = deqBuf.mvOut[i].mMetaData.mTimeStamp;
    act->frameTimeStampBoot = deqBuf.mvOut[i].mMetaData.mTimeStamp_B;
    act->exeState = EXE_STATE_DONE;
    act->isReadoutReady = MTRUE;

    if (1 <= mLogLevelI) {
      MUINT32 index = i;
      std::string strInfo("");
      strInfo += base::StringPrintf("[P1::DEQ]" P1INFO_ACT_STR " job(%d/%d) ",
                                    P1INFO_ACT_VAR(*act), index, mBurstNum);
      for (size_t n = index; n < deqBuf.mvOut.size(); n += mBurstNum) {
        if (deqBuf.mvOut[n].mPortID.index == PORT_IMGO.index) {
          strInfo += base::StringPrintf(
              "IMG(%s) ",
              (deqBuf.mvOut[n].mMetaData.mRawType == EPipe_PROCESSED_RAW)
                  ? "proc"
                  : "pure");
        } else if (deqBuf.mvOut[n].mPortID.index == PORT_RRZO.index) {
          MRect crop_s = deqBuf.mvOut[n].mMetaData.mCrop_s;
          MRect crop_d = deqBuf.mvOut[n].mMetaData.mCrop_d;
          MSize size_d = deqBuf.mvOut[n].mMetaData.mDstSize;
          strInfo += base::StringPrintf(
              "RRZ%d(%d-%d-%dx%d)(%d-%d-%dx%d)(%dx%d) ", mIsBinEn, crop_s.p.x,
              crop_s.p.y, crop_s.s.w, crop_s.s.h, crop_d.p.x, crop_d.p.y,
              crop_d.s.w, crop_d.s.h, size_d.w, size_d.h);
        }
      }
      strInfo += base::StringPrintf(
          "T-ns(EXP: %" PRId64
          ")"
          "(SOF: m_%" PRId64 " b_%" PRId64 ")(SS: %" PRId64 ") ",
          act->frameExpDuration, act->frameTimeStamp, act->frameTimeStampBoot,
          ((act->frameTimeStampBoot != 0)
               ? (act->frameTimeStampBoot - act->frameExpDuration)
               : ((act->frameTimeStamp != 0)
                      ? (act->frameTimeStamp - act->frameExpDuration)
                      : (0))));
      act->res.clear();
      act->res += strInfo;
    }

    if (!match || act->getType() == ACT_TYPE_INTERNAL || !getActive()) {
      FLUSH_TYPE type = FLUSH_MIS_UNCERTAIN;
      if (!act->getFlush()) {  // if flush type did not set
        if (act->getType() == ACT_TYPE_INTERNAL) {
          if (act->reqType == REQ_TYPE_INITIAL) {
            type = FLUSH_INITIAL;
          } else if (act->reqType == REQ_TYPE_PADDING) {
            type = FLUSH_PADDING;
          } else if (act->reqType == REQ_TYPE_DUMMY) {
            type = FLUSH_DUMMY;
          } else {
            type = FLUSH_MIS_UNCERTAIN;
          }
        } else {
          type = FLUSH_INACTIVE;
        }
      }
      onReturnFrame(&qAct, type, MTRUE);
      /* DO NOT use this P1QueAct after onReturnFrame() */
      ret = BAD_VALUE;
    } else {
      IMetadata resultAppend;
      IMetadata inAPP, inHAL;
      //
      if (IS_LMV(mpConnectLMV)) {
        MBOOL enEIS = IS_PORT(CONFIG_PORT_EISO, mConfigPort);
        MBOOL enRRZ = IS_PORT(CONFIG_PORT_RRZO, mConfigPort);
        MUINT32 idxEIS = act->portBufIndex[P1_OUTPUT_PORT_EISO];
        MUINT32 idxRRZ = act->portBufIndex[P1_OUTPUT_PORT_RRZO];
        if (OK == act->frameMetadataGet(STREAM_META_IN_APP, &inAPP) &&
            OK == act->frameMetadataGet(STREAM_META_IN_HAL, &inHAL)) {
          MBOOL bIsBinEn =
              (act->refBinSize == mSensorParams.size) ? MFALSE : MTRUE;
          mpConnectLMV->processResult(
              bIsBinEn, enEIS, enRRZ, &inAPP, &inHAL, &result3A, mp3A,
              act->magicNum, act->sofIdx, mLastSofIdx, act->uniSwitchState,
              deqBuf, idxEIS, idxRRZ, &resultAppend);
        } else {
        }
      }
#if 1  // for RSSO update buffer
      if (IS_OUT(REQ_OUT_RSSO, act->reqOutSet) &&
          (!IS_EXP(EXP_EVT_NOBUF_RSSO, act->expRec))) {
        MUINT32 port_index = act->portBufIndex[P1_OUTPUT_PORT_RSSO];
        std::shared_ptr<IImageBuffer> spImgBuf =
            act->streamBufImg[STREAM_IMG_OUT_RSS].spImgBuf;
        if (port_index != P1_PORT_BUF_IDX_NONE && spImgBuf != nullptr) {
          MSize size = deqBuf.mvOut[port_index].mMetaData.mDstSize;
          MY_LOGD("RSSO data size (%dx%d)", size.w, size.h);

          IMetadata::IEntry entry(MTK_P1NODE_RSS_SIZE);
          entry.push_back(size, Type2Type<MSize>());
          resultAppend.update(MTK_P1NODE_RSS_SIZE, entry);
        }
      }
#endif

#if SUPPORT_FSC
      if (mpFSC != nullptr) {
        MBOOL bIsBinEn =
            (act->refBinSize == mSensorParams.size) ? MFALSE : MTRUE;
        MUINT32 idxRSS = P1_PORT_BUF_IDX_NONE;
        MUINT32 idxRRZ = act->portBufIndex[P1_OUTPUT_PORT_RRZO];
        if (IS_OUT(REQ_OUT_RSSO, act->reqOutSet) &&
            (!IS_EXP(EXP_EVT_NOBUF_RSSO, act->expRec)) &&
            act->streamBufImg[STREAM_IMG_OUT_RSS].spImgBuf != nullptr) {
          idxRSS = act->portBufIndex[P1_OUTPUT_PORT_RSSO];
        }
        act->frameMetadataGet(STREAM_META_IN_APP, &inAPP);
        act->frameMetadataGet(STREAM_META_IN_HAL, &inHAL);
        mpFSC->processResult(bIsBinEn, &inAPP, &inHAL, &result3A, mp3A,
                             act->magicNum, deqBuf, idxRSS, idxRRZ, i,
                             &resultAppend);  // get FSC resultAppend
      }
#endif
      mLastSofIdx = act->sofIdx;
      onProcessResult(&qAct, deqBuf, result3A, resultAppend, i);
      /* DO NOT use this P1QueAct/P1Act after onProcessResult() */
      ret = OK;
    }
  }
  if (IS_PORT(CONFIG_PORT_EISO, mConfigPort) && getActive()) {
    if (IS_LMV(mpConnectLMV)) {
      mpConnectLMV->processDequeFrame(&deqBuf);
    }
  }
  //
  inflightMonitoring(IMT_DEQ);

  FUNCTION_OUT;

  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::onHandleFlush(MBOOL wait, MBOOL isInitReqOff) {
  FUNCTION_IN;
  P1_TRACE_S_BEGIN(SLG_S, "P1:HandleFlush");

  // wake up cb thread.
  {
    std::lock_guard<std::mutex> _l(mStartLock);
    mStartCond.notify_all();
  }

  // stop hardware
  if (CC_LIKELY(!wait)) {
    hardwareOps_stop();  // include hardware and 3A
  }

  // check the state
  if (CC_UNLIKELY(!mFirstReceived)) {
    if (EN_START_CAP) {
      MY_LOGE(
          "REQUEST-NOT-READY in start capture flow - "
          "enableCaptureFlow(%d)",
          mEnableCaptureFlow);
      mLogInfo.inspect(LogInfo::IT_STOP_NO_REQ_IN_CAPTURE);
    }
    if (EN_INIT_REQ_RUN) {
      MY_LOGE(
          "REQUEST-NOT-READY in initial request flow - "
          "initRequest(%d) : ReceivedCnt(%d) < RequiredNum(%d)",
          mInitReqSet, mInitReqCnt, mInitReqNum);
      mLogInfo.inspect(LogInfo::IT_STOP_NO_REQ_IN_REQUEST);
    }
    if ((!EN_START_CAP) && (!EN_INIT_REQ_RUN)) {
      MY_LOGI("Request Not Received");
      mLogInfo.inspect(LogInfo::IT_STOP_NO_REQ_IN_GENERAL);
    }
  }

  {
    // by flush() or uninit() or eventStreamingOff()
    if (EN_INIT_REQ_CFG) {  // in flush() -> queue() flow, for fast switching,
                            // it will run InitReq flow again,
      mInitReqNum =
          mInitReqSet *
          mBurstNum;  // exclude eventStreamingOn case (by mInitReqOff = MTRUE)
      mInitReqCnt = 0;
      mInitReqOff = isInitReqOff;
      if (!EN_INIT_REQ_RUN) {
        MY_LOGI("Disable - InitReq Set:%d Num:%d Cnt:%d Off:%d", mInitReqSet,
                mInitReqNum, mInitReqCnt, mInitReqOff);
      }
    }
  }

  //
  {
    mpTaskCtrl->sessionLock();
    MINT cnt = mpTaskCollector->remainder();
    while (cnt > 0) {
      P1QueAct qAct;
      cnt = mpTaskCollector->requireAct(&qAct);
      if (qAct.id() > P1ACT_ID_NULL) {
        onReturnFrame(&qAct, FLUSH_COLLECTOR, MFALSE);
        /* DO NOT use this P1QueAct after onReturnFrame() */
      }
    }
    mTagList.set(cnt);
    mpTaskCtrl->sessionUnLock();
  }

  // (1) clear request queue
  {
    std::lock_guard<std::mutex> _l(mRequestQueueLock);
    // P1_LOGD("Check-RQ (%d)", mRequestQueue.size());
    while (!mRequestQueue.empty()) {
      P1QueJob job = *mRequestQueue.begin();
      mRequestQueue.erase(mRequestQueue.begin());
      for (MUINT8 i = 0; i < job.size(); i++) {
        P1QueAct qAct = job.edit(i);
        onReturnFrame(&qAct, FLUSH_REQUESTQ, MFALSE);
        /* DO NOT use this P1QueAct after onReturnFrame() */
      }
    }
  }

  // (2) clear processing queue
  //     wait until processing frame coming out
  if (CC_UNLIKELY(wait)) {
    std::unique_lock<std::mutex> _lck(mProcessingQueueLock);
    while (!mProcessingQueue.empty()) {
      mProcessingQueueCond.wait(_lck);
    }
  } else {
    // must guarantee hardware has been stopped.
    std::lock_guard<std::mutex> _l(mProcessingQueueLock);
    while (!mProcessingQueue.empty()) {
      P1QueJob job = *mProcessingQueue.begin();
      mProcessingQueue.erase(mProcessingQueue.begin());
      for (MUINT8 i = 0; i < job.size(); i++) {
        P1QueAct qAct = job.edit(i);
        onReturnFrame(&qAct, FLUSH_PROCESSQ, MFALSE);
        /* DO NOT use this P1QueAct after onReturnFrame() */
      }
    }
  }

  // (3) TODO:clear dummy queue

  // (4) clear drop frame queue
  onProcessDropFrame();

  if (CC_UNLIKELY(mpDeliverMgr != nullptr && !mpDeliverMgr->waitFlush(MTRUE))) {
    MY_LOGW("request not done");
  }

  // (5) clear all
  mRequestQueue.clear();     // suppose already clear
  mProcessingQueue.clear();  // suppose already clear
  mpTaskCtrl->reset();
  mLastNum = 1;

  P1_TRACE_C_END(SLG_S);  // "P1:HandleFlush"
  FUNCTION_OUT;
}

/******************************************************************************
 *
 ******************************************************************************/
void P1NodeImp::doNotifyCb(MINT32 _msgType,
                           MINTPTR _ext1,
                           MINTPTR _ext2,
                           MINTPTR _ext3) {
  FUNCTION_IN;
  //
  if (_msgType == IHal3ACb::eID_NOTIFY_3APROC_FINISH) {
    MINT32 magicNum = P1_MAGIC_NUM_NULL;
    NS3Av3::RequestSet_T* pReqSet =
        reinterpret_cast<NS3Av3::RequestSet_T*>(_ext1);
    if (pReqSet != nullptr && pReqSet->vNumberSet.size() > 0) {
      magicNum = pReqSet->vNumberSet[0];
    }
    MUINT32 sofIdx = (MUINT32)(_ext2);
    mLogInfo.setMemo(LogInfo::CP_CB_PROC_REV, _msgType, magicNum, sofIdx);
  } else if (_msgType == IHal3ACb::eID_NOTIFY_VSYNC_DONE) {
    mLogInfo.setMemo(LogInfo::CP_CB_SYNC_REV, _msgType);
  }
  MY_LOGD("P1 doNotifyCb(%d) %zd %zd %zd", _msgType, _ext1, _ext2, _ext3);
  //
  if (CC_UNLIKELY(!getActive())) {
    MY_LOGI("not-active-return");
    if (_msgType == IHal3ACb::eID_NOTIFY_3APROC_FINISH) {
      mLogInfo.setMemo(LogInfo::CP_CB_PROC_RET, _msgType, MTRUE);
    } else if (_msgType == IHal3ACb::eID_NOTIFY_VSYNC_DONE) {
      mLogInfo.setMemo(LogInfo::CP_CB_SYNC_RET, _msgType, MTRUE);
    }
    return;
  }
  switch (_msgType) {
    case IHal3ACb::eID_NOTIFY_3APROC_FINISH:
      if (_ext3 == 0) {
        MY_LOGE("CapParam NULL (%d) %zd %zd", _msgType, _ext1, _ext2);
      } else {
        NS3Av3::RequestSet_T set =
            *reinterpret_cast<NS3Av3::RequestSet_T*>(_ext1);
        NS3Av3::CapParam_T param =
            *reinterpret_cast<NS3Av3::CapParam_T*>(_ext3);
        onSyncBegin(MFALSE, &set, (MUINT32)_ext2, &param);
      }
      mLogInfo.setMemo(LogInfo::CP_CB_PROC_RET, _msgType, MFALSE);
      break;
    case IHal3ACb::eID_NOTIFY_CURR_RESULT:
      break;
    case IHal3ACb::eID_NOTIFY_VSYNC_DONE:
      onSyncEnd();
      mLogInfo.setMemo(LogInfo::CP_CB_SYNC_RET, _msgType, MFALSE);
      break;
    default:
      break;
  }
  //
  FUNCTION_OUT;
}

/******************************************************************************
 *
 ******************************************************************************/
void P1NodeImp::doNotifyDropframe(MUINT magicNum, void* cookie) {
  MY_LOGI("notify drop frame (%d)", magicNum);

  if (cookie == nullptr) {
    MY_LOGE("return cookie is NULL");
    return;
  }
  MINT32 mSysLevel = reinterpret_cast<P1NodeImp*>(cookie)->mSysLevel;
  P1_TRACE_F_BEGIN(SLG_E, "P1:DRV-drop(%d)", magicNum);

  {
    std::lock_guard<std::mutex> _l(
        reinterpret_cast<P1NodeImp*>(cookie)->mDropQueueLock);
    reinterpret_cast<P1NodeImp*>(cookie)->mDropQueue.push_back(magicNum);
    MY_LOGI("[Cam::%d] receive drop frame (%d)",
            reinterpret_cast<P1NodeImp*>(cookie)->getOpenId(), magicNum);
  }

  if ((reinterpret_cast<P1NodeImp*>(cookie)->mpDeliverMgr != nullptr) &&
      (reinterpret_cast<P1NodeImp*>(cookie)->mpDeliverMgr->runningGet())) {
    MY_LOGI("[Cam::%d] process drop frame (%d)",
            reinterpret_cast<P1NodeImp*>(cookie)->getOpenId(), magicNum);
    reinterpret_cast<P1NodeImp*>(cookie)->mpDeliverMgr->trigger();
  }
  P1_TRACE_C_END(SLG_E);  // "P1:DRV-drop"
}

#if (USING_DRV_IO_PIPE_EVENT)
/******************************************************************************
 *
 ******************************************************************************/
NSCam::NSIoPipe::IoPipeEventCtrl P1NodeImp::onEvtCtrlAcquiring(
    P1NodeImp* user, NSCam::NSIoPipe::IpRawP1AcquiringEvent* evt) {
  if (CC_UNLIKELY(user == nullptr)) {
    MY_LOGW("user is NULL");
    (*evt).setResult((NSCam::NSIoPipe::IoPipeEvent::ResultType)
                         NSCam::NSIoPipe::IoPipeEvent::RESULT_ERROR);
    return NSCam::NSIoPipe::IoPipeEventCtrl::STOP_BROADCASTING;
  }
  std::lock_guard<std::mutex> _l(user->mIoPipeEvtOpLock);
  if (CC_UNLIKELY(user->mIoPipeEvtOpLeaving)) {
    MY_LOGI("[Cam::%d] IoPipeEvtOpLeaving return", user->mOpenId);
    return NSCam::NSIoPipe::IoPipeEventCtrl::OK;
  }
  if (CC_UNLIKELY(user->mIoPipeEvtOpAcquired == MTRUE)) {
    MY_LOGI("[Cam::%d] IoPipeEvtOpAcquired:1 return", user->mOpenId);
    (*evt).setResult((NSCam::NSIoPipe::IoPipeEvent::ResultType)
                         NSCam::NSIoPipe::IoPipeEvent::RESULT_REJECT);
    return NSCam::NSIoPipe::IoPipeEventCtrl::STOP_BROADCASTING;
  }
  user->eventStreamingOff();
  user->mIoPipeEvtOpAcquired = MTRUE;
  return NSCam::NSIoPipe::IoPipeEventCtrl::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
NSCam::NSIoPipe::IoPipeEventCtrl P1NodeImp::onEvtCtrlReleasing(
    P1NodeImp* user, NSCam::NSIoPipe::IpRawP1ReleasedEvent* evt) {
  if (CC_UNLIKELY(user == nullptr)) {
    MY_LOGW("user is NULL");
    (*evt).setResult((NSCam::NSIoPipe::IoPipeEvent::ResultType)
                         NSCam::NSIoPipe::IoPipeEvent::RESULT_ERROR);
    return NSCam::NSIoPipe::IoPipeEventCtrl::STOP_BROADCASTING;
  }
  std::lock_guard<std::mutex> _l(user->mIoPipeEvtOpLock);
  if (CC_UNLIKELY(user->mIoPipeEvtOpLeaving)) {
    MY_LOGI("[Cam::%d] IoPipeEvtOpLeaving return", user->mOpenId);
    return NSCam::NSIoPipe::IoPipeEventCtrl::OK;
  }
  if (CC_UNLIKELY(user->mIoPipeEvtOpAcquired == MFALSE)) {
    MY_LOGI("[Cam::%d] IoPipeEvtOpAcquired:0 return", user->mOpenId);
    (*evt).setResult((NSCam::NSIoPipe::IoPipeEvent::ResultType)
                         NSCam::NSIoPipe::IoPipeEvent::RESULT_REJECT);
    return NSCam::NSIoPipe::IoPipeEventCtrl::STOP_BROADCASTING;
  }
  user->eventStreamingOn();
  user->mIoPipeEvtOpAcquired = MFALSE;
  return NSCam::NSIoPipe::IoPipeEventCtrl::OK;
}
#endif

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::createStuffBuffer(
    std::shared_ptr<IImageBuffer>* imageBuffer,
    std::shared_ptr<IImageStreamInfo> const& streamInfo,
    NSCam::MSize::value_type const changeHeight) {
  std::vector<MUINT32> vStride;
  vStride.clear();
  vStride.reserve(streamInfo->getBufPlanes().size());
  for (size_t i = 0; i < streamInfo->getBufPlanes().size(); i++) {
    vStride.push_back(
        (MUINT32)(streamInfo->getBufPlanes()[i].rowStrideInBytes));
  }
  //
  MSize size = streamInfo->getImgSize();
  // change the height while changeHeight > 0
  if (changeHeight > 0) {
    size.h = changeHeight;
  }
  //
  return createStuffBuffer(imageBuffer, streamInfo->getStreamName(),
                           streamInfo->getImgFormat(), size, vStride);
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::createStuffBuffer(std::shared_ptr<IImageBuffer>* imageBuffer,
                             char const* szName,
                             MINT32 format,
                             MSize size,
                             std::vector<MUINT32> vStride) {
  return mStuffBufMgr.acquireStoreBuffer(
      imageBuffer, szName, format, size, vStride, mBurstNum,
      (mDebugScanLineMask != 0) ? MTRUE : MFALSE);
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::destroyStuffBuffer(std::shared_ptr<IImageBuffer>* imageBuffer) {
  if (imageBuffer == nullptr) {
    MY_LOGW("Stuff ImageBuffer not exist");
    return BAD_VALUE;
  }
  return mStuffBufMgr.releaseStoreBuffer(imageBuffer);
}

#if (USING_DRV_IO_PIPE_EVENT)
/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::eventStreamingInform() {
  {
    pthread_rwlock_rdlock(&mIoPipeEvtStateLock);
    if (CC_LIKELY(mIoPipeEvtState != IO_PIPE_EVT_STATE_ACQUIRING)) {
      pthread_rwlock_unlock(&mIoPipeEvtStateLock);
      return;
    }
    pthread_rwlock_unlock(&mIoPipeEvtStateLock);
  }
  //
  {
    std::lock_guard<std::mutex> _l(mIoPipeEvtWaitLock);
    if (CC_LIKELY(mIoPipeEvtWaiting)) {
      if ((mpDeliverMgr != nullptr) && (mpDeliverMgr->runningGet()) &&
          (mpDeliverMgr->isActListEmpty())) {
        mIoPipeEvtWaitCond.notify_all();
        MY_LOGI("action list is empty");
      }
    }
  }
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::eventStreamingOn() {
  P1_TRACE_AUTO(SLG_E, "P1:eventStreamingOn");
  MY_LOGI("StreamingOn +");
  std::lock_guard<std::mutex> _l(mPublicLock);
  {
    pthread_rwlock_rdlock(&mIoPipeEvtStateLock);
    if (CC_UNLIKELY(mIoPipeEvtState != IO_PIPE_EVT_STATE_ACQUIRED)) {
      MY_LOGI("StreamingOn return - state(%d)", mIoPipeEvtState);
      pthread_rwlock_unlock(&mIoPipeEvtStateLock);
      return;
    }
    pthread_rwlock_unlock(&mIoPipeEvtStateLock);
  }
  {
    pthread_rwlock_wrlock(&mIoPipeEvtStateLock);
    mIoPipeEvtState = IO_PIPE_EVT_STATE_NONE;
    pthread_rwlock_unlock(&mIoPipeEvtStateLock);
  }
  //
  beckonRequest();
  MY_LOGI("StreamingOn -");
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::eventStreamingOff() {
  P1_TRACE_AUTO(SLG_E, "P1:eventStreamingOff");
  MY_LOGI("StreamingOff +");
  std::lock_guard<std::mutex> _l(mPublicLock);
  {
    pthread_rwlock_rdlock(&mIoPipeEvtStateLock);
    if (CC_UNLIKELY(mIoPipeEvtState != IO_PIPE_EVT_STATE_NONE)) {
      MY_LOGI("StreamingOff return - state(%d)", mIoPipeEvtState);
      pthread_rwlock_unlock(&mIoPipeEvtStateLock);
      return;
    }
    pthread_rwlock_unlock(&mIoPipeEvtStateLock);
  }
  {
    pthread_rwlock_wrlock(&mIoPipeEvtStateLock);
    mIoPipeEvtState = IO_PIPE_EVT_STATE_ACQUIRING;
    pthread_rwlock_unlock(&mIoPipeEvtStateLock);
  }
  //
  // wait for the last NORMAL/BYPASS action done
  MBOOL bWaitDrain = MTRUE;
#if 1
  {
    MINT32 nWaitDrain =
        property_get_int32("vendor.debug.camera.log.p1nodefasthqc", 0);
    if (nWaitDrain > 0) {
      MY_LOGI("p1node-fast-hqc:%d", nWaitDrain);
      bWaitDrain = MFALSE;
    }
  }
#endif
  if (bWaitDrain && (mpDeliverMgr != nullptr) && (mpDeliverMgr->runningGet())) {
    std::unique_lock<std::mutex> lck(mIoPipeEvtWaitLock);
    mIoPipeEvtWaiting = MTRUE;
    while (!mpDeliverMgr->isActListEmpty()) {
      std::cv_status res = mIoPipeEvtWaitCond.wait_for(
          lck, std::chrono::nanoseconds(P1NODE_EVT_DRAIN_WAIT_INV_NS));
      if (res != std::cv_status::timeout) {
        MY_LOGI("all actions done");
        break;
      } else {
        MY_LOGI("actions not finish - res(%d) empty(%d)", res,
                mpDeliverMgr->isActListEmpty());
        mpDeliverMgr->dumpInfo();
        mLogInfo.inspect(LogInfo::IT_EVT_WAIT_DRAIN_TIMEOUT);
      }
    }
    mIoPipeEvtWaiting = MFALSE;
  } else {
    MY_LOGI("stop and flush directly, WaitDrain(%d)", bWaitDrain);
  }
  // In InitReq Flow (EN_INIT_REQ), mInitReqOff re-assign by eventStreamingOff()
  // via onHandleFlush(). While eventStreamingOn(), by mInitReqOff to disable
  // the init-request-flow as the next first request arrival.
  onHandleFlush(MFALSE, MTRUE);  // disable InitReq flow
  //
  {
    pthread_rwlock_wrlock(&mIoPipeEvtStateLock);
    mIoPipeEvtState = IO_PIPE_EVT_STATE_ACQUIRED;
    pthread_rwlock_unlock(&mIoPipeEvtStateLock);
  }
  MY_LOGI("StreamingOff -");
  return;
}
#endif

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::hardwareOps_start() {
#if SUPPORT_ISP
  FUNCTION_IN;
  P1_TRACE_AUTO(SLG_B, "P1:hardwareOps_start");
  MERROR err = OK;
  mLogInfo.setMemo(LogInfo::CP_OP_START_BGN, mBurstNum, mEnableCaptureFlow,
                   (EN_INIT_REQ_RUN) ? mInitReqSet : 0);

  std::lock_guard<std::mutex> _l(mHardwareLock);

  mTagReq.clear();
  mTagSet.clear();
  mTagEnq.clear();
  mTagDeq.clear();
  mTagOut.clear();
  mTagList.clear();

  {
    MINT64 currentTime = (MINT64)(NSCam::Utils::getTimeInNs());
    {
      std::lock_guard<std::mutex> _l(mMonitorLock);
      mMonitorTime = currentTime;
    }
  }

  {
    std::lock_guard<std::mutex> _l(mThreadLock);
    setActive(MTRUE);
    mThreadCond.notify_all();
  }
  setStartState(NSP1Node::START_STATE_NULL);
#if USING_CTRL_3A_LIST_PREVIOUS
  mPreviousCtrlList.clear();
#endif
  setInit(MTRUE);
  mLastSofIdx = P1SOFIDX_NULL_VAL;
  mLastSetNum = 0;
  {
    std::lock_guard<std::mutex> _ll(mTransferJobLock);
    mTransferJobIdx = P1ACT_ID_NULL;
    mTransferJobWaiting = MFALSE;
  }

  mConfigPort = CONFIG_PORT_NONE;
  mConfigPortNum = 0;

  mFirstReceived = MFALSE;
  //
  {
    std::lock_guard<std::mutex> _ll(mFrameSetLock);
    mFrameSetAlready = MFALSE;
  }
  //
  mDequeThreadProfile.reset();
  //
  EImageFormat resizer_fmt = eImgFmt_FG_BAYER10;
  //
  if (mspResConCtrl != nullptr) {
    P1NODE_RES_CON_ACQUIRE(mspResConCtrl, mResConClient, mIsResConGot);
  }
  //
  {
    EPipeSelect ps = EPipeSelect_Normal;
    if (mPipeMode == PIPE_MODE_NORMAL_SV) {
      ps = EPipeSelect_NormalSv;
    }

    int selectedVersion = 0;
    const unsigned int* version = nullptr;
    size_t count = 0;
    err = getNormalPipeModule()->get_sub_module_api_version(&version, &count);
    if (err < 0 || !count || !version) {
      MY_LOGE(
          "[%d] INormalPipeModule::get_sub_module_api_version - err:%#x "
          "count:%zu version:%p",
          getOpenId(), err, count, version);
    } else {
      selectedVersion = *(version + count - 1);  // Select max. version
    }

    MY_LOGI("[%d] count:%zu Selected CamIO Version:%0#x", getOpenId(), count,
            selectedVersion);

    mpCamIO = getNormalPipeModule()->getSubModule(
        kPipeNormal, getOpenId(), getNodeName(), selectedVersion);

    LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_OP_START_DRV_INIT_BGN,
                         LogInfo::CP_OP_START_DRV_INIT_END);
  }

#if SUPPORT_LCS
  err = lcsInit();
  if (err != OK) {
    MY_LOGE("lcsInit fail");
    return err;
  }
#endif
  MSize sensorSize = mSensorParams.size;
  MSize rrzoSize = mvStreamImg[STREAM_IMG_OUT_RESIZE]->getImgSize();
  err = lmvInit(sensorSize, rrzoSize);
  if (err != OK) {
    MY_LOGE("lmvInit fail");
    return err;
  }

  MUINT32 AETargetMode = MFALSE;  // vHDR OFF mode
  NS3Av3::AEInitExpoSetting_T initExpoSetting;
  ::memset(&initExpoSetting, 0, sizeof(initExpoSetting));
  initExpoSetting.u4SensorMode = mSensorParams.mode;
  initExpoSetting.u4AETargetMode = AETargetMode;
  // set shutter/gain 0 as ::memset
#if SUPPORT_3A
  err = getAEInitExpoSetting(&initExpoSetting);
  if (err != OK) {
    MY_LOGE("getAEInitExpoSetting fail");
    return err;
  }
#endif

#if MTKCAM_HAVE_SANDBOX_SUPPORT
  v4l2DeviceStart();
#endif
  //
  PipeTag pipe_tag = kPipeTag_Out2_Tuning;
  std::vector<PortInfo> vPortInfo;
  vPortInfo.clear();
  vPortInfo.reserve(P1_OUTPUT_PORT_TOTAL);
  addConfigPort(&vPortInfo, &resizer_fmt);
  //
  IHalSensor::ConfigParam sensorCfg;
  ::memset(&sensorCfg, 0, sizeof(IHalSensor::ConfigParam));
  QInitParam halCamIOinitParam =
      prepareQInitParam(&sensorCfg, initExpoSetting, vPortInfo);
  //
  MSize binInfoSize = mSensorParams.size;
  setCurrentBinSize(mSensorParams.size);
  mIsBinEn = false;
  MSize rawSize[2];
  MSize* pSizeProc = &rawSize[0];  // 0 = EPipe_PROCESSED_RAW
  MSize* pSizePure = &rawSize[1];  // 1 = EPipe_PURE_RAW
  *pSizeProc = MSize(0, 0);
  *pSizePure = MSize(0, 0);

  std::map<int, std::vector<std::shared_ptr<IImageBuffer>>> buffers;
  err =
      startCamIO(halCamIOinitParam, &binInfoSize, rawSize, &pipe_tag, &buffers);
  if (err != OK) {
    MY_LOGE("startCamIO fail");
    return err;
  }

  if (auto pModule = getNormalPipeModule()) {
#if 1  // query height ratio from DRV
    NormalPipe_QueryInfo info;
    pModule->query(PORT_RRZO.index, ENPipeQueryCmd_BS_RATIO, resizer_fmt, 0,
                   &info);
    mResizeRatioMax = info.bs_ratio;
    MY_LOGI("ResizeRatioMax = info.bs_ratio(%d)", info.bs_ratio);
#endif
  }

#if SUPPORT_3A
  {
    P1_TIMING_CHECK("P1:3A-notifyPwrOn", 10, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:3A-notifyPwrOn");
    mLogInfo.setMemo(LogInfo::CP_OP_START_3A_PWRON_BGN);
    if (mp3A->notifyP1PwrOn()) {  // CCU DRV power on after ISP configPipe
      setPowerNotify(MTRUE);
    } else {
      MY_LOGI("3A->notifyP1PwrOn() return FALSE");
    }
    mLogInfo.setMemo(LogInfo::CP_OP_START_3A_PWRON_END);
    P1_TRACE_C_END(SLG_S);  // "P1:3A-notifyPwrOn"
  }
  {
    P1_TIMING_CHECK("P1:3A-setSensorMode", 10, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:3A-setSensorMode");
    mp3A->setSensorMode(mSensorParams.mode);
    P1_TRACE_C_END(SLG_S);  // "P1:3A-setSensorMode"
  }
#endif
  if (IS_LMV(mpConnectLMV)) {
    P1_TIMING_CHECK("P1:LMV-config", 20, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:LMV-config");
    MY_LOGD("mpConnectLMV->config");
    mpConnectLMV->config(buffers[PORT_EISO.index]);
    P1_TRACE_C_END(SLG_S);  // "P1:LMV-config"
  }

#if SUPPORT_LCS
  if (mpLCS) {
    LCS_HAL_CONFIG_DATA lcsConfig;
    lcsConfig.cameraVer = LCS_CAMERA_VER_3;
    if (mvStreamImg[STREAM_IMG_OUT_LCS] != nullptr) {
      lcsConfig.lcsOutWidth = mvStreamImg[STREAM_IMG_OUT_LCS]->getImgSize().w;
      lcsConfig.lcsOutHeight = mvStreamImg[STREAM_IMG_OUT_LCS]->getImgSize().h;
      lcsConfig.tgWidth = mSensorParams.size.w;
      lcsConfig.tgHeight = mSensorParams.size.h;
    } else {
      MY_LOGI("LCS enable but no LCS stream info");
      lcsConfig.lcsOutWidth = 0;
      lcsConfig.lcsOutHeight = 0;
    }
    //
    P1_TIMING_CHECK("P1:LCS-config", 20, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:LCS-config");
    mpLCS->ConfigLcsHal(lcsConfig);
    P1_TRACE_C_END(SLG_S);  // "P1:LCS-config"
  }
#endif

  if (mpConCtrl != nullptr && mpConCtrl->getStageCtrl() != nullptr) {
    mpConCtrl->getStageCtrl()->done((MUINT32)STAGE_DONE_START, MTRUE);
  }

#if SUPPORT_3A
  if (mp3A) {
    P1_TIMING_CHECK("P1:3A-config", 300, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:3A-sendCtrl-attachCb");
    mp3A->attachCb(IHal3ACb::eID_NOTIFY_3APROC_FINISH, this);
    mp3A->attachCb(IHal3ACb::eID_NOTIFY_CURR_RESULT, this);
    mp3A->attachCb(IHal3ACb::eID_NOTIFY_VSYNC_DONE, this);
    P1_TRACE_C_END(SLG_S);  // "P1:3A-sendCtrl-attachCb"
    NS3Av3::ConfigInfo_T config;
    NS3Av3::EBitMode_T b = NS3Av3::EBitMode_12Bit;
    switch (mPipeBit) {
      case CAM_Pipeline_10BITS:
        b = NS3Av3::EBitMode_10Bit;
        break;
      case CAM_Pipeline_12BITS:
        b = NS3Av3::EBitMode_12Bit;
        break;
      case CAM_Pipeline_14BITS:
        b = NS3Av3::EBitMode_14Bit;
        break;
      case CAM_Pipeline_16BITS:
        b = NS3Av3::EBitMode_16Bit;
        break;
      default:
        MY_LOGW("CANNOT map the pipeline bit mode");
        break;
    }
    config.i4BitMode = b;
    config.i4SubsampleCount = (MINT32)(MAX(mBurstNum, 1));
    config.i4HlrOption = (mDisableHLR) ? (NS3Av3::EHlrOption_ForceOff)
                                       : (NS3Av3::EHlrOption_Auto);
    config.CfgAppMeta = mCfgAppMeta;
    config.CfgHalMeta = mCfgHalMeta;
    //
    LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_OP_START_3A_CFG_BGN,
                         LogInfo::CP_OP_START_3A_CFG_END);

    /* getMatrix to active and from active here */
    NSCamHW::HwTransHelper helper(getOpenId());
    NSCamHW::HwMatrix matFromActive;
    if (!helper.getMatrixFromActive(mSensorParams.mode, &matFromActive))
      MY_LOGE("Get hw matFromActive failed");
    NSCamHW::HwMatrix matToActive;
    if (!helper.getMatrixToActive(mSensorParams.mode, &matToActive))
      MY_LOGE("Get hw matToActive failed");
    config.matFromAct = matFromActive;
    config.matToAct = matToActive;

#if MTKCAM_HAVE_SANDBOX_SUPPORT
    /* update static metadata for sandbox */
    std::shared_ptr<IMetadataProvider> pMetadataProvider =
        NSMetadataProviderManager::valueFor(getOpenId());
    if (pMetadataProvider.get()) {
      const IMetadata& metaStaticInfo =
          pMetadataProvider->getMtkStaticCharacteristics();
      NS3Av3::IpcMetaStaticInfo_T ipcMetaStaticInfo;

      const IMetadata::IEntry& entryAvailableScene =
          metaStaticInfo.entryFor(MTK_CONTROL_AVAILABLE_SCENE_MODES);
      ipcMetaStaticInfo.availableSceneModesCount = entryAvailableScene.count();
      for (int i = 0; i < entryAvailableScene.count(); i++)
        ipcMetaStaticInfo.availableSceneModes[i] =
            entryAvailableScene.itemAt(i, Type2Type<MUINT8>());

      const IMetadata::IEntry& entryScnOvrd =
          metaStaticInfo.entryFor(MTK_CONTROL_SCENE_MODE_OVERRIDES);
      ipcMetaStaticInfo.sceneModeOverridesCount = entryScnOvrd.count();
      for (int i = 0; i < entryScnOvrd.count(); i++)
        ipcMetaStaticInfo.sceneModeOverrides[i] =
            entryScnOvrd.itemAt(i, Type2Type<MUINT8>());

      NS3Av3::QUERY_ENTRY_SINGLE(metaStaticInfo,
                                 MTK_CONTROL_AE_COMPENSATION_STEP,
                                 &ipcMetaStaticInfo.aeCompensationStep);
      NS3Av3::GET_ENTRY_ARRAY(metaStaticInfo, MTK_CONTROL_MAX_REGIONS,
                              ipcMetaStaticInfo.maxRegions, 3);
      NS3Av3::QUERY_ENTRY_SINGLE(metaStaticInfo,
                                 MTK_SENSOR_INFO_ACTIVE_ARRAY_REGION,
                                 &ipcMetaStaticInfo.activeArrayRegion);
      NS3Av3::QUERY_ENTRY_SINGLE(metaStaticInfo, MTK_LENS_INFO_SHADING_MAP_SIZE,
                                 &ipcMetaStaticInfo.shadingMapSize);
      NS3Av3::QUERY_ENTRY_SINGLE(metaStaticInfo,
                                 MTK_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
                                 &ipcMetaStaticInfo.availableFocalLengths);
      NS3Av3::QUERY_ENTRY_SINGLE(metaStaticInfo,
                                 MTK_LENS_INFO_AVAILABLE_APERTURES,
                                 &ipcMetaStaticInfo.availableApertures);

      mp3A->send3ACtrl(NS3Av3::E3ACtrl_IPC_Set_MetaStaticInfo,
                       reinterpret_cast<MINTPTR>(&ipcMetaStaticInfo), 0);
    }
#endif
    P1_TRACE_S_BEGIN(SLG_S, "P1:3A-config");
    MY_LOGI("mp3A->config +++");
    mp3A->config(config);
    MY_LOGI("mp3A->config ---");
    P1_TRACE_C_END(SLG_S);  // "P1:3A-config"
  }
#endif
  //
  if (mpHwStateCtrl != nullptr) {
    MBOOL isLegacy =
        (mIsLegacyStandbyMode || mForceStandbyMode > 0) ? MTRUE : MFALSE;
    mpHwStateCtrl->config(getOpenId(), mLogLevel, mLogLevelI, mSysLevel,
                          mBurstNum, mpCamIO.get(), mp3A, isLegacy);
  }
  //
#ifdef P1_START_INFO_STR
#undef P1_START_INFO_STR
#endif
#define P1_START_INFO_STR                                                \
  "Cam::%d "                                                             \
  "Sensor(%dx%d) Raw(%d,0x%x)-Proc(%dx%d)-Pure(%dx%d) "                  \
  "Bin(%dx%d) BinEn=%d TG(%d:%d) DTwin(%d@%d)=%d LSM(%d) QLV(%d) "       \
  "Ratio(%d) SensorCfg(i:%d %dx%d s:%d b:%d c:%d, h:%d f:%d t:%d d:%d) " \
  "ConfigPort[%d]:(0x%x) InitParam[R:%d B:%d D:%d Nd:%d )]"

#ifdef P1_START_INFO_VAR
#undef P1_START_INFO_VAR
#endif
#define P1_START_INFO_VAR                                                      \
  getOpenId(), mSensorParams.size.w, mSensorParams.size.h, mRawDefType,        \
      mRawOption, pSizeProc->w, pSizeProc->h, pSizePure->w, pSizePure->h,      \
      binInfoSize.w, binInfoSize.h, mIsBinEn, mTgNum, mCfg.mSensorNum,         \
      mDisableDynamicTwin, mCfg.mSupportDynamicTwin, mIsDynamicTwinEn,         \
      mIsLegacyStandbyMode, mCfg.mQualityLv, mResizeRatioMax, sensorCfg.index, \
      sensorCfg.crop.w, sensorCfg.crop.h, sensorCfg.scenarioId,                \
      sensorCfg.isBypassScenario, sensorCfg.isContinuous, sensorCfg.HDRMode,   \
      sensorCfg.framerate, sensorCfg.twopixelOn, sensorCfg.debugMode,          \
      mConfigPortNum, mConfigPort, halCamIOinitParam.mRawType,                 \
      halCamIOinitParam.m_pipelinebitdepth, halCamIOinitParam.m_DynamicTwin,   \
      halCamIOinitParam.m_IQlv
  //
  if (EN_INIT_REQ_RUN) {
    MY_LOGI("InitRqeFlow return %d %d %d - " P1_START_INFO_STR, mInitReqSet,
            mInitReqNum, mInitReqCnt, P1_START_INFO_VAR);
    mLogInfo.setMemo(LogInfo::CP_OP_START_REQ_WAIT_BGN,
                     LogInfo::START_SET_REQUEST);
    return OK;
  }
  //
  if (EN_START_CAP) {
    std::lock_guard<std::mutex> _l(mStartCaptureLock);
    mStartCaptureState = START_CAP_STATE_WAIT_REQ;
    mStartCaptureType = NS3Av3::E_CAPTURE_NORMAL;
    mStartCaptureIdx = 0;
    mStartCaptureExp = 0;
    MY_LOGI("EnableCaptureFlow(%d) return - " P1_START_INFO_STR,
            mEnableCaptureFlow, P1_START_INFO_VAR);
    mLogInfo.setMemo(LogInfo::CP_OP_START_REQ_WAIT_BGN,
                     LogInfo::START_SET_CAPTURE);
    return OK;
  }

#if MTKCAM_HAVE_SANDBOX_SUPPORT

  {
    MY_LOGI("V4L2TuningPipeMgr start +++");
    mpV4L2TuningPipe =
        std::make_shared<v4l2::V4L2TuningPipeMgr>(pipe_tag, getOpenId());
    mpV4L2TuningPipe->startWorker();
    MY_LOGI("V4L2TuningPipeMgr start ---");
    // stt pipe should be started after normalpipe started.

    MY_LOGI("V4L2SttPipeMgr start +++");

    // checks if need META2 or not
    const int enableMeta2 = [&]() {
      if (mpV4L2LensMgr == nullptr) {
        return v4l2::V4L2SttPipeMgr::DISABLE_META2;
      }
      return mpV4L2LensMgr->isLensDriverOpened()
                 ? v4l2::V4L2SttPipeMgr::ENABLE_META2
                 : v4l2::V4L2SttPipeMgr::DISABLE_META2;
    }();

    mpV4L2SttPipe = std::make_shared<v4l2::V4L2SttPipeMgr>(
        pipe_tag, getOpenId(), enableMeta2);

    mpV4L2SttPipe->start();
    MY_LOGI("V4L2SttPipeMgr start ---");

    // HWEvent workers
    // Notice: HwEventWorkers must be started after INormalPipe started.
    MY_LOGI("V4L2HwEventWorker start +++");
    auto _create_hw_event_mgrs = [this](size_t idx, EPipeSignal signal,
                                        const char* caller) {
      mpV4L2HwEventMgr[idx] = std::make_shared<v4l2::V4L2HwEventWorker>(
          getOpenId(), signal, caller);

      mpV4L2HwEventMgr[idx]->start();
    };
    MY_LOGI("V4L2HwEventWorker start ---");
    _create_hw_event_mgrs(0, EPipeSignal_SOF, "evtmgr_sof");
    _create_hw_event_mgrs(1, EPipeSignal_AFDONE, "evtmgr_afdone");
    _create_hw_event_mgrs(2, EPipeSignal_EOF, "evtmgr_eof");
  }
#endif

#if SUPPORT_3A
  if (mp3A) {
    LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_OP_START_3A_START_BGN,
                         LogInfo::CP_OP_START_3A_START_END);
    P1_TIMING_CHECK("P1:3A-start", 100, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:3A-start");
    MY_LOGI("mp3A->start +++");
    mp3A->start();
    MY_LOGI("mp3A->start ---");
    P1_TRACE_C_END(SLG_S);  // "P1:3A-start"
  }
#endif

#if MTKCAM_HAVE_SANDBOX_SUPPORT
  {
    /* wait until a buffer enqueued, and stream on tuning pipe */
    CAM_LOGD("V4L2TuningPipeMgr: wait until enqued [+]");
    mpV4L2TuningPipe->waitUntilEnqued();
    CAM_LOGD("V4L2TuningPipeMgr: wait until enqued [-]");
    mpV4L2TuningPipe->startPipe();
  }
#endif

  //
  {
    if (mpConCtrl != nullptr && mpConCtrl->getStageCtrl() != nullptr) {
      MBOOL success = MFALSE;
      mpConCtrl->getStageCtrl()->wait((MUINT32)STAGE_DONE_INIT_ITEM, &success);
      if (!success) {
        MY_LOGE("stage - init item fail");
        return BAD_VALUE;
      }
    }
    //
    MERROR status = hardwareOps_enque(
        &mProcessingQueue.at(mProcessingQueue.size() - 1), ENQ_TYPE_INITIAL);
    if (status != OK) {
      MY_LOGE("hardware init-enque fail (%d)", status);
      return status;
    }
  }
  //
  if (mpConCtrl != nullptr) {
    mpConCtrl->cleanAidStage();
  }
  //
  setStartState(NSP1Node::START_STATE_DRV_START);
  if (mpCamIO != nullptr) {
    LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_OP_START_DRV_START_BGN,
                         LogInfo::CP_OP_START_DRV_START_END);
    P1_TIMING_CHECK("P1:DRV-start", 100, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:DRV-start");
    MY_LOGI("mpCamIO->start +++");
    if (!mpCamIO->start()) {
      MY_LOGE("mpCamIO->start fail");
      P1_TRACE_C_END(SLG_S);  // "P1:DRV-start"
      return BAD_VALUE;
    }
    MY_LOGI("mpCamIO->start ---");
    P1_TRACE_C_END(SLG_S);  // "P1:DRV-start"
  }
  setStartState(NSP1Node::START_STATE_LMV_SENSOR_EN);
  if (IS_LMV(mpConnectLMV)) {
    P1_TIMING_CHECK("P1:LMV-sensor", 100, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:LMV-sensor");
    MY_LOGD("mpConnectLMV->enableSensor +++");
    mpConnectLMV->enableSensor();
    MY_LOGD("mpConnectLMV->enableSensor ---");
    P1_TRACE_C_END(SLG_S);  // "P1:LMV-sensor"
  }
  {
    std::lock_guard<std::mutex> _l(mThreadLock);
    setReady(MTRUE);
    mThreadCond.notify_all();
  }
  {
    std::lock_guard<std::mutex> _l(mStartLock);
    setStartState(NSP1Node::START_STATE_READY);
    mStartCond.notify_all();
  }
  syncHelperStart();
  MY_LOGI("End - " P1_START_INFO_STR, P1_START_INFO_VAR);
#undef P1_START_INFO_STR
#undef P1_START_INFO_VAR

  mLogInfo.setMemo(LogInfo::CP_OP_START_END, mBurstNum, mEnableCaptureFlow,
                   mInitReqSet, LogInfo::START_SET_GENERAL);

  FUNCTION_OUT;

  return OK;
#else
  return OK;
#endif
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::hardwareOps_request() {
#if SUPPORT_ISP
  FUNCTION_IN;
  P1_TRACE_AUTO(SLG_B, "P1:hardwareOps_request");
  //
  mLogInfo.setMemo(LogInfo::CP_OP_START_REQ_WAIT_END,
                   LogInfo::START_SET_REQUEST);
  //
  std::lock_guard<std::mutex> _l(mHardwareLock);
  //
#if USING_CTRL_3A_LIST
  List<NS3Av3::MetaSet_T> ctrlList;
#else
  std::vector<NS3Av3::MetaSet_T*> ctrlQueue;
  ctrlQueue.clear();
  ctrlQueue.reserve(mInitReqNum);
#endif
  //
  MUINT32 total = mpTaskCollector->remainder();
  MUINT32 initNum = mInitReqNum - 1;
  if (CC_UNLIKELY(total < mInitReqNum)) {
    MY_LOGE("init request set is not enough (%d < %d)", total, mInitReqSet);
    return BAD_VALUE;
  }
  // Prepare for DRV
  for (MUINT32 index = 0; index < initNum; index++) {
    P1QueJob job(mBurstNum);
    mpTaskCtrl->sessionLock();
    mpTaskCollector->requireJob(&job);
    mpTaskCtrl->sessionUnLock();
    {
      std::lock_guard<std::mutex> _l(mProcessingQueueLock);
      mProcessingQueue.push_back(job);
    }
    if (job.size() > 0 && job.edit(0).ptr() != nullptr) {
#if USING_CTRL_3A_LIST
      ctrlList.push_back(job.edit(0).ptr()->metaSet);
#else
      ctrlQueue.push_back(&(job.edit(0).ptr()->metaSet));
#endif
    }
  }
  // Set to 3A
  {
    P1QueJob job(mBurstNum);
    mpTaskCtrl->sessionLock();
    mpTaskCollector->requireJob(&job);
    mpTaskCtrl->sessionUnLock();
    {
      std::lock_guard<std::mutex> _l(mRequestQueueLock);
      mRequestQueue.push_back(job);
    }
    if (job.size() > 0 && job.edit(0).ptr() != nullptr) {
#if USING_CTRL_3A_LIST
      ctrlList.push_back(job.edit(0).ptr()->metaSet);
#else
      ctrlQueue.push_back(&(job.edit(0).ptr()->metaSet));
#endif
    }
    mLastSetNum = job.getLastNum();
    mTagSet.set(mLastSetNum);
    //
    {
      std::lock_guard<std::mutex> _ll(mFrameSetLock);
#if SUPPORT_3A
      if (mp3A) {
        P1_TIMING_CHECK("P1:3A-startRequest", 200, TC_W);
        mLogInfo.setMemo(LogInfo::CP_START_SET_BGN, LogInfo::START_SET_REQUEST,
                         mLastSetNum);
        P1_TRACE_S_BEGIN(SLG_S, "P1:3A-startRequest");
        MY_LOGI("mp3A->startRequestQ +++");
#if USING_CTRL_3A_LIST
        mp3A->startRequestQ(ctrlList);  // mp3A->start();
#else
        mp3A->startRequestQ(ctrlQueue);        // mp3A->start();
#endif
        MY_LOGI("mp3A->startRequestQ ---");
        P1_TRACE_C_END(SLG_S);  // "P1:3A-startRequest"
        mLogInfo.setMemo(LogInfo::CP_START_SET_END, LogInfo::START_SET_REQUEST,
                         mLastSetNum);
      }
#endif
      mFrameSetAlready = MTRUE;
    }
  }
  // EnQ to DRV
  {
    for (MUINT32 idx = 0; idx < initNum; idx++) {
      P1QueJob job;
      {
        // clone the QueJob from the ProcessingQueue
        std::lock_guard<std::mutex> _l(mProcessingQueueLock);
        job = mProcessingQueue.at(idx);
      }
      MY_LOGD("InitReqEnQ (%d/%d) +++", idx, initNum);
      MERROR status = hardwareOps_enque(&job, ENQ_TYPE_INITIAL);
      if (status != OK) {
        MY_LOGE("hardware req-init-enque fail (%d)@(%d)", status, idx);
        return status;
      }
      MY_LOGD("InitReqEnQ (%d/%d) ---", idx, initNum);
    }
  }
  //
  if (mpConCtrl != nullptr) {
    mpConCtrl->cleanAidStage();
  }
  //
  setStartState(NSP1Node::START_STATE_DRV_START);
#if 1
  if (mpCamIO) {
    LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_OP_START_DRV_START_BGN,
                         LogInfo::CP_OP_START_DRV_START_END);
    P1_TIMING_CHECK("P1:DRV-start", 100, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:DRV-start");
    MY_LOGI("mpCamIO->start +++");
    if (!mpCamIO->start()) {
      MY_LOGE("hardware start fail");
      P1_TRACE_C_END(SLG_S);  // "P1:DRV-start"
      return BAD_VALUE;
    }
    MY_LOGI("mpCamIO->start ---");
    P1_TRACE_C_END(SLG_S);  // "P1:DRV-start"
  }
#endif
  setStartState(NSP1Node::START_STATE_LMV_SENSOR_EN);
  if (IS_LMV(mpConnectLMV)) {
    P1_TIMING_CHECK("P1:LMV-sensor", 100, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:LMV-sensor");
    MY_LOGD("mpConnectLMV->enableSensor +++");
    mpConnectLMV->enableSensor();
    MY_LOGD("mpConnectLMV->enableSensor ---");
    P1_TRACE_C_END(SLG_S);  // "P1:LMV-sensor"
  }
  {
    std::lock_guard<std::mutex> _l(mThreadLock);
    setReady(MTRUE);
    mThreadCond.notify_all();
  }
  {
    std::lock_guard<std::mutex> _l(mStartLock);
    setStartState(NSP1Node::START_STATE_READY);
    mStartCond.notify_all();
  }
  syncHelperStart();
  MY_LOGI("Cam::%d BinEn:%d ConfigPort[%d]:0x%x", getOpenId(), mIsBinEn,
          mConfigPortNum, mConfigPort);

  mLogInfo.setMemo(LogInfo::CP_OP_START_END, mBurstNum, mEnableCaptureFlow,
                   mInitReqSet, LogInfo::START_SET_REQUEST);

  FUNCTION_OUT;

  return OK;
#else
  return OK;
#endif
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::hardwareOps_capture() {
#if SUPPORT_ISP
  FUNCTION_IN;
  P1_TRACE_AUTO(SLG_B, "P1:hardwareOps_capture");
  //
  mLogInfo.setMemo(LogInfo::CP_OP_START_REQ_WAIT_END,
                   LogInfo::START_SET_CAPTURE);
  //
  std::lock_guard<std::mutex> _l(mHardwareLock);
  //
  MINT32 num = 0;
  MBOOL isManualCap = MFALSE;
  //
  if (EN_START_CAP) {
    std::lock_guard<std::mutex> _l(mStartCaptureLock);
    mStartCaptureState = START_CAP_STATE_WAIT_CB;
  }
  //
  {
    MINT32 type = NS3Av3::ESTART_CAP_NORMAL;
    {
#if 1
      P1QueJob job(mBurstNum);
      mpTaskCtrl->sessionLock();
      mpTaskCollector->requireJob(&job);
      mpTaskCtrl->sessionUnLock();
#endif
#if USING_CTRL_3A_LIST
      List<NS3Av3::MetaSet_T> ctrlList;
      generateCtrlList(&ctrlList, &job);
#else
      std::vector<NS3Av3::MetaSet_T*> ctrlQueue;
      ctrlQueue.clear();
      ctrlQueue.reserve(job.size());
      generateCtrlQueue(&ctrlQueue, &job);
#endif
      {
        std::lock_guard<std::mutex> _l(mRequestQueueLock);
        mRequestQueue.push_back(job);
      }
      std::lock_guard<std::mutex> _ll(mFrameSetLock);
#if SUPPORT_3A
      if (mp3A) {
        P1_TIMING_CHECK("P1:3A-startCapture", 200, TC_W);
        mLogInfo.setMemo(LogInfo::CP_START_SET_BGN, LogInfo::START_SET_CAPTURE,
                         job.getIdx());
        P1_TRACE_S_BEGIN(SLG_S, "P1:3A-startCapture");
        MY_LOGI("mp3A->startCapture +++");
#if USING_CTRL_3A_LIST
        type = mp3A->startCapture(ctrlList);  // mp3A->start();
#else
        type = mp3A->startCapture(ctrlQueue);  // mp3A->start();
#endif
        MY_LOGI("mp3A->startCapture ---");
        P1_TRACE_C_END(SLG_S);  // "P1:3A-startCapture"
        mLogInfo.setMemo(LogInfo::CP_START_SET_END, LogInfo::START_SET_CAPTURE,
                         job.getIdx());
      }
#endif
      mFrameSetAlready = MTRUE;
      MY_LOGI("start-capture-type %d", type);
    }
    if (type != NS3Av3::ESTART_CAP_NORMAL) {
      isManualCap = MTRUE;
      MY_LOGI("capture in manual flow %d", type);
    }
  }
  //
  if (mpConCtrl != nullptr && mpConCtrl->getStageCtrl() != nullptr) {
    MBOOL success = MFALSE;
    mpConCtrl->getStageCtrl()->wait((MUINT32)STAGE_DONE_INIT_ITEM, &success);
    if (!success) {
      MY_LOGE("stage - cap init item fail");
      return BAD_VALUE;
    }
  }
  //
  {
    MERROR status = hardwareOps_enque(
        &mProcessingQueue.at(mProcessingQueue.size() - 1), ENQ_TYPE_INITIAL);
    if (status != OK) {
      MY_LOGE("hardware cap-init-enque fail (%d)", status);
      return status;
    }
  }
  //
  if (!isManualCap) {
    P1_TRACE_S_BEGIN(SLG_S, "Cap Normal EnQ");
    P1QueJob job(mBurstNum);
    {
      {
        std::lock_guard<std::mutex> _l(mRequestQueueLock);
        if (CC_LIKELY(mRequestQueue.size() > 0)) {
          Que_T::iterator it = mRequestQueue.begin();
          job = *it;
          mRequestQueue.erase(it);
        } else {
          MY_LOGE("NormalCap RequestQueue is empty");
          return BAD_VALUE;
        }
      }
      MERROR status = onProcessEnqueFrame(&job);
      if (status != OK) {
        MY_LOGE("hardware cap-enque-normal fail (%d)", status);
        return status;
      }
      num = job.edit(0).getNum();
    }
    P1_TRACE_C_END(SLG_S);  // "Cap Normal EnQ"
    //
    if (num > 0) {
      mLastSetNum = job.getLastNum();
      mTagSet.set(mLastSetNum);
    }
  }
  //
  if (mpConCtrl != nullptr) {
    mpConCtrl->cleanAidStage();
  }
  //
  setStartState(NSP1Node::START_STATE_DRV_START);
#if 1
  if (mpCamIO) {
    LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_OP_START_DRV_START_BGN,
                         LogInfo::CP_OP_START_DRV_START_END);
    P1_TIMING_CHECK("P1:DRV-start", 100, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:DRV-start");
    MY_LOGI("mpCamIO->start +++");
    if (!mpCamIO->start()) {
      MY_LOGE("hardware start fail");
      P1_TRACE_C_END(SLG_S);  // "P1:DRV-start"
      return BAD_VALUE;
    }
    MY_LOGI("mpCamIO->start ---");
    P1_TRACE_C_END(SLG_S);  // "P1:DRV-start"
  }
#endif
  //
  if (isManualCap) {
    setStartState(NSP1Node::START_STATE_CAP_MANUAL_ENQ);
    P1_TRACE_S_BEGIN(SLG_S, "Cap Manual EnQ");
    P1QueJob job(mBurstNum);
    {
      {
        std::lock_guard<std::mutex> _l(mRequestQueueLock);
        if (CC_LIKELY(mRequestQueue.size() > 0)) {
          Que_T::iterator it = mRequestQueue.begin();
          job = *it;
          mRequestQueue.erase(it);
        } else {
          MY_LOGE("ManualCap RequestQueue is empty");
          return BAD_VALUE;
        }
      }
      MERROR status = onProcessEnqueFrame(&job);
      if (status != OK) {
        MY_LOGE("hardware cap-enque-manual fail (%d)", status);
        return status;
      }
      num = job.edit(0).getNum();
    }
    P1_TRACE_C_END(SLG_S);  // "Cap Manual EnQ"
    //
    if (num > 0) {
      mLastSetNum = job.getLastNum();
      mTagSet.set(mLastSetNum);
    }
  }
  setStartState(NSP1Node::START_STATE_LMV_SENSOR_EN);
  if (IS_LMV(mpConnectLMV)) {
    P1_TIMING_CHECK("P1:LMV-sensor", 100, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:LMV-sensor");
    MY_LOGD("mpConnectLMV->enableSensor +++");
    mpConnectLMV->enableSensor();
    MY_LOGD("mpConnectLMV->enableSensor ---");
    P1_TRACE_C_END(SLG_S);  // "P1:LMV-sensor"
  }
  {
    std::lock_guard<std::mutex> _l(mThreadLock);
    setReady(MTRUE);
    mThreadCond.notify_all();
  }
  {
    std::lock_guard<std::mutex> _l(mStartLock);
    setStartState(NSP1Node::START_STATE_READY);
    mStartCond.notify_all();
  }
  syncHelperStart();
  MY_LOGI("Cam::%d BinEn:%d ConfigPort[%d]:0x%x", getOpenId(), mIsBinEn,
          mConfigPortNum, mConfigPort);

  mLogInfo.setMemo(LogInfo::CP_OP_START_END, mBurstNum, mEnableCaptureFlow,
                   mInitReqSet, LogInfo::START_SET_CAPTURE);

  FUNCTION_OUT;

  return OK;
#else
  return OK;
#endif
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::procedureAid_start() {
  FUNCTION_IN;
  P1_TRACE_AUTO(SLG_S, "P1:aid_start");
  MERROR status = OK;
  if (mpConCtrl != nullptr && mpConCtrl->getStageCtrl() != nullptr) {
    MBOOL success = MFALSE;
    mpConCtrl->getStageCtrl()->wait((MUINT32)STAGE_DONE_START, &success);
    if (!success) {
      MY_LOGE("stage - aid start fail");
      return BAD_VALUE;
    }
  }
  //
  MBOOL init_success = MTRUE;
  status = buildInitItem();
  if (OK != status) {
    init_success = MFALSE;
    MY_LOGE("CANNOT build init item");
  }
  if (mpConCtrl != nullptr && mpConCtrl->getStageCtrl() != nullptr) {
    mpConCtrl->getStageCtrl()->done((MUINT32)STAGE_DONE_INIT_ITEM,
                                    init_success);
  }
  //
  FUNCTION_OUT;
  return status;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::buildInitItem() {
  FUNCTION_IN;
  P1_TRACE_AUTO(SLG_S, "P1:reserve_init");
  if (getReady()) {
    MY_LOGW("it should be executed before start ready");
    return BAD_VALUE;
  }
  //
  if (mpTaskCtrl == nullptr || mpTaskCollector == nullptr) {
    return BAD_VALUE;
  }
  P1QueJob job(mBurstNum);
  mpTaskCtrl->sessionLock();
  P1TaskCollector initCollector(mpTaskCtrl);
  for (int i = 0; i < mBurstNum; i++) {
    P1QueAct initAct;
    initCollector.enrollAct(&initAct);
    createAction(&initAct, nullptr, REQ_TYPE_INITIAL);
    initCollector.verifyAct(&initAct);
  }
  initCollector.requireJob(&job);
  mpTaskCtrl->sessionUnLock();
  //
  if (!job.ready()) {
    MY_LOGE("init-job-not-ready");
    mpTaskCtrl->dumpActPool();
    return BAD_VALUE;
  } else {
    std::lock_guard<std::mutex> _l(mProcessingQueueLock);
    mProcessingQueue.push_back(job);
  }
  //
  P1QueJob& p_job = mProcessingQueue.at(mProcessingQueue.size() - 1);
  QBufInfo* pEnBuf = nullptr;
  if (mpConCtrl == nullptr || (!mpConCtrl->initBufInfo_create(&pEnBuf)) ||
      pEnBuf == nullptr) {
    MY_LOGE("CANNOT create the initBufInfo");
    return BAD_VALUE;
  }
  for (size_t i = 0; i < p_job.size(); i++) {
    MY_LOGD("p_job(%d)(%zu/%zu)", p_job.getIdx(), i, p_job.size());
    if (OK != setupAction(&p_job.edit(i), pEnBuf)) {
      MY_LOGE("setup enque act fail");
      return BAD_VALUE;
    }
    P1Act act = GET_ACT_PTR(act, p_job.edit(i), BAD_VALUE);
    act->exeState = EXE_STATE_PROCESSING;
  }
  //
  FUNCTION_OUT;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::generateAppMeta(P1QueAct* rAct,
                           NS3Av3::MetaSet_T const& result3A,
                           QBufInfo const& deqBuf,
                           IMetadata* appMetadata,
                           MUINT32 const index) {
  P1Act act = GET_ACT_PTR(act, *rAct, RET_VOID);
  if (act->appFrame == nullptr) {
    MY_LOGW("pipeline frame is NULL (%d)", act->magicNum);
    return;
  }
  std::shared_ptr<IPipelineFrame> request = act->appFrame;

  // [3A/Flash/sensor section]
  (*appMetadata) = result3A.appMeta;

  MBOOL needOverrideTimestamp = MFALSE;
  if (tryGetMetadata<MBOOL>(&result3A.halMeta, MTK_EIS_NEED_OVERRIDE_TIMESTAMP,
                            &needOverrideTimestamp) &&
      needOverrideTimestamp) {
    IMetadata::IEntry entry(MTK_EIS_FEATURE_ISNEED_OVERRIDE_TIMESTAMP);
    entry.push_back(1, Type2Type<MUINT8>());  // Need Override timestamp
    entry.push_back(0, Type2Type<MUINT8>());  // timestamp not overrided yet
    (*appMetadata).update(MTK_EIS_FEATURE_ISNEED_OVERRIDE_TIMESTAMP, entry);
  }

  // [request section]
  // android.request.frameCount
  {
    IMetadata::IEntry entry(MTK_REQUEST_FRAME_COUNT);
    entry.push_back(request->getFrameNo(), Type2Type<MINT32>());
    (*appMetadata).update(MTK_REQUEST_FRAME_COUNT, entry);
  }
  // android.request.metadataMode
  {
    IMetadata::IEntry entry(MTK_REQUEST_METADATA_MODE);
    entry.push_back(MTK_REQUEST_METADATA_MODE_FULL, Type2Type<MUINT8>());
    (*appMetadata).update(MTK_REQUEST_METADATA_MODE, entry);
  }

  // [sensor section]
  // android.sensor.timestamp
  {
    MINT64 frame_duration = 0;
#if 1  // modify timestamp
    frame_duration = act->frameExpDuration;
#endif
    // ISP SOF : ISP get the first line from sensor
    MINT64 Sof = (deqBuf.mvOut[index].mMetaData.mTimeStamp_B != 0)
                     ? deqBuf.mvOut[index].mMetaData.mTimeStamp_B
                     : deqBuf.mvOut[index].mMetaData.mTimeStamp;
    MINT64 timestamp = (Sof != 0) ? (Sof - frame_duration) : 0;
    IMetadata::IEntry entry(MTK_SENSOR_TIMESTAMP);
    entry.push_back(timestamp, Type2Type<MINT64>());
    (*appMetadata).update(MTK_SENSOR_TIMESTAMP, entry);
  }

  // [sensor section]
  // android.sensor.rollingshutterskew
  {
    MINT64 skew = 0;
    queryRollingSkew(getOpenId(), &skew, mLogLevelI);
    IMetadata::IEntry entry(MTK_SENSOR_ROLLING_SHUTTER_SKEW);
    entry.push_back(skew, Type2Type<MINT64>());
    (*appMetadata).update(MTK_SENSOR_ROLLING_SHUTTER_SKEW, entry);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::generateAppTagIndex(IMetadata* appMetadata, IMetadata* appTagIndex) {
  IMetadata::IEntry entryTagIndex(MTK_P1NODE_METADATA_TAG_INDEX);

  for (size_t i = 0; i < (*appMetadata).count(); ++i) {
    IMetadata::IEntry entry = (*appMetadata).entryAt(i);
    entryTagIndex.push_back((MINT32)entry.tag(), Type2Type<MINT32>());
  }

  if (OK != (*appTagIndex).update(entryTagIndex.tag(), entryTagIndex)) {
    MY_LOGE("fail to update index");
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::generateHalMeta(P1QueAct* rAct,
                           NS3Av3::MetaSet_T const& result3A,
                           QBufInfo const& deqBuf,
                           IMetadata const& resultAppend,
                           IMetadata const& inHalMetadata,
                           IMetadata* halMetadata,
                           MUINT32 const index) {
  P1Act act = GET_ACT_PTR(act, *rAct, RET_VOID);
  if (deqBuf.mvOut.size() == 0) {
    MY_LOGE("deqBuf is empty");
    return;
  }

  // 3a tuning
  *halMetadata = result3A.halMeta;

  // append
  *halMetadata += resultAppend;

  // in hal meta
  *halMetadata += inHalMetadata;

  {
    IMetadata::IEntry entry(MTK_P1NODE_SENSOR_MODE);
    entry.push_back(mSensorParams.mode, Type2Type<MINT32>());
    (*halMetadata).update(MTK_P1NODE_SENSOR_MODE, entry);
  }

  {
    IMetadata::IEntry entry(MTK_P1NODE_SENSOR_VHDR_MODE);
    entry.push_back(mSensorParams.vhdrMode, Type2Type<MINT32>());
    (*halMetadata).update(MTK_P1NODE_SENSOR_VHDR_MODE, entry);
  }

  {
    IMetadata::IEntry entry(MTK_PIPELINE_FRAME_NUMBER);
    entry.push_back(act->appFrame->getFrameNo(), Type2Type<MINT32>());
    (*halMetadata).update(MTK_PIPELINE_FRAME_NUMBER, entry);
  }

  // rrzo
  MUINT32 port_index = act->portBufIndex[P1_OUTPUT_PORT_RRZO];
  if (port_index != P1_PORT_BUF_IDX_NONE) {
    ResultMetadata const* result = &(deqBuf.mvOut[port_index].mMetaData);
    if (result == nullptr) {
      MY_LOGE("CANNOT get result at (%d) for (%d)", port_index, index);
      return;
    }
    // crop region
    MRect crop = result->mCrop_s;
    MBOOL bIsBinEn = (act->refBinSize == mSensorParams.size) ? MFALSE : MTRUE;
    {
      IMetadata::IEntry entry_br(MTK_P1NODE_BIN_CROP_REGION);
      entry_br.push_back(result->mCrop_s, Type2Type<MRect>());
      (*halMetadata).update(MTK_P1NODE_BIN_CROP_REGION, entry_br);
      IMetadata::IEntry entry_bs(MTK_P1NODE_BIN_SIZE);
      entry_bs.push_back(act->refBinSize, Type2Type<MSize>());
      (*halMetadata).update(MTK_P1NODE_BIN_SIZE, entry_bs);
      //
      if (bIsBinEn) {
        BIN_REVERT(crop.p.x);
        BIN_REVERT(crop.p.y);
        BIN_REVERT(crop.s.w);
        BIN_REVERT(crop.s.h);
      }
      IMetadata::IEntry entry(MTK_P1NODE_SCALAR_CROP_REGION);
      entry.push_back(crop, Type2Type<MRect>());
      (*halMetadata).update(MTK_P1NODE_SCALAR_CROP_REGION, entry);
    }
    //
    {
      IMetadata::IEntry entry(MTK_P1NODE_DMA_CROP_REGION);
      entry.push_back(result->mCrop_d, Type2Type<MRect>());
      (*halMetadata).update(MTK_P1NODE_DMA_CROP_REGION, entry);
    }
    //
    {
      IMetadata::IEntry entry(MTK_P1NODE_RESIZER_SIZE);
      entry.push_back(result->mDstSize, Type2Type<MSize>());
      (*halMetadata).update(MTK_P1NODE_RESIZER_SIZE, entry);
    }
    //
    MINT32 quality = MTK_P1_RESIZE_QUALITY_LEVEL_UNKNOWN;
    {
      if (result->eIQlv == eCamIQ_L) {
        quality = MTK_P1_RESIZE_QUALITY_LEVEL_L;
      } else if (result->eIQlv == eCamIQ_H) {
        quality = MTK_P1_RESIZE_QUALITY_LEVEL_H;
      }
      IMetadata::IEntry entry(MTK_P1NODE_RESIZE_QUALITY_LEVEL);
      entry.push_back(quality, Type2Type<MINT32>());
      (*halMetadata).update(MTK_P1NODE_RESIZE_QUALITY_LEVEL, entry);
    }
    MY_LOGI("[CropInfo] Bin(%d) Sensor" P1_SIZE_STR "ActRef" P1_SIZE_STR
            "CROP_REGION" P1_RECT_STR "CropS" P1_RECT_STR "CropD" P1_RECT_STR
            "DstSize" P1_SIZE_STR "- [BinQty] QUALITY_LEVEL(%d) IQlv(%d)",
            bIsBinEn, P1_SIZE_VAR(mSensorParams.size),
            P1_SIZE_VAR(act->refBinSize), P1_RECT_VAR(crop),
            P1_RECT_VAR(result->mCrop_s), P1_RECT_VAR(result->mCrop_d),
            P1_SIZE_VAR(result->mDstSize), quality, result->eIQlv);
  }
  //
  {
    MINT64 timestamp = deqBuf.mvOut[index].mMetaData.mTimeStamp;
    IMetadata::IEntry entry(MTK_P1NODE_FRAME_START_TIMESTAMP);
    entry.push_back(timestamp, Type2Type<MINT64>());
    (*halMetadata).update(MTK_P1NODE_FRAME_START_TIMESTAMP, entry);
  }

  {
    MINT64 timestamp_boot = deqBuf.mvOut[index].mMetaData.mTimeStamp_B;
    IMetadata::IEntry entry(MTK_P1NODE_FRAME_START_TIMESTAMP_BOOT);
    entry.push_back(timestamp_boot, Type2Type<MINT64>());
    (*halMetadata).update(MTK_P1NODE_FRAME_START_TIMESTAMP_BOOT, entry);
  }
  //
  if ((mIsDynamicTwinEn) && (mpCamIO != nullptr)) {
    MBOOL ret = MFALSE;
    MINT32 status = MTK_P1_TWIN_STATUS_NONE;
    E_CamHwPathCfg curCfg = eCamHwPathCfg_Num;
    ret = mpCamIO->sendCommand(ENPipeCmd_GET_HW_PATH_CFG, (MINTPTR)(&curCfg),
                               (MINTPTR)NULL, (MINTPTR)NULL);
    if (ret) {
      switch (curCfg) {
        case eCamHwPathCfg_One_TG:
          status = MTK_P1_TWIN_STATUS_TG_MODE_1;
          break;
        case eCamHwPathCfg_Two_TG:
          status = MTK_P1_TWIN_STATUS_TG_MODE_2;
          break;
          // case eCamHwPathCfg_Num:
        default:
          MY_LOGI("CamHwPathCfg_Num(%d) not defined", curCfg);
          break;
      }
      IMetadata::IEntry entry(MTK_P1NODE_TWIN_STATUS);
      entry.push_back(status, Type2Type<MINT32>());
      (*halMetadata).update(MTK_P1NODE_TWIN_STATUS, entry);
    } else {
      MY_LOGI("cannot get ENPipeCmd_GET_HW_PATH_CFG (%d)", ret);
    }
    MY_LOGI("(%d)=GET_HW_PATH_CFG(%d) TWIN_STATUS[%d] @ (%d)(%d:%d)", ret,
            curCfg, status, act->magicNum, act->frmNum, act->reqNum);
  }
  //
  MINT32 qtyStatus = MTK_P1_RESIZE_QUALITY_STATUS_NONE;
  if (act->qualitySwitchState != QUALITY_SWITCH_STATE_NONE) {
    switch (act->qualitySwitchState) {
      case QUALITY_SWITCH_STATE_DONE_ACCEPT:
        qtyStatus = MTK_P1_RESIZE_QUALITY_STATUS_ACCEPT;
        break;
      case QUALITY_SWITCH_STATE_DONE_IGNORE:
        qtyStatus = MTK_P1_RESIZE_QUALITY_STATUS_IGNORE;
        break;
      case QUALITY_SWITCH_STATE_DONE_REJECT:
        qtyStatus = MTK_P1_RESIZE_QUALITY_STATUS_REJECT;
        break;
      case QUALITY_SWITCH_STATE_DONE_ILLEGAL:
        qtyStatus = MTK_P1_RESIZE_QUALITY_STATUS_ILLEGAL;
        break;
      default:
        break;
    }
    IMetadata::IEntry entry(MTK_P1NODE_RESIZE_QUALITY_STATUS);
    entry.push_back(qtyStatus, Type2Type<MINT32>());
    (*halMetadata).update(MTK_P1NODE_RESIZE_QUALITY_STATUS, entry);
  }
  //
  MBOOL qtySwitch = getQualitySwitching();
  {
    IMetadata::IEntry entry(MTK_P1NODE_RESIZE_QUALITY_SWITCHING);
    entry.push_back(qtySwitch, Type2Type<MBOOL>());
    (*halMetadata).update(MTK_P1NODE_RESIZE_QUALITY_SWITCHING, entry);
  }
  //
  MY_LOGI("QUALITY_STATUS[%d](%d) - QUALITY_SWITCHING[%d] - " P1NUM_ACT_STR,
          qtyStatus, act->qualitySwitchState, qtySwitch, P1NUM_ACT_VAR(*act));
  //
  if (CC_UNLIKELY(act->isRawTypeChanged)) {
    MINT32 rawType = act->fullRawType;
    IMetadata::IEntry entry(MTK_P1NODE_RAW_TYPE);
    entry.push_back(rawType, Type2Type<MINT32>());
    (*halMetadata).update(MTK_P1NODE_RAW_TYPE, entry);
    MY_LOGI("MTK_P1NODE_RAW_TYPE(%d) - full raw type change - " P1NUM_ACT_STR,
            rawType, P1NUM_ACT_VAR(*act));
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::setupAction(P1QueAct* rAct, QBufInfo* info) {
  FUNCTION_IN;
  P1Act act = GET_ACT_PTR(act, *rAct, BAD_VALUE);
#if SUPPORT_ISP
  MUINT32 out = 0;
  //
  std::shared_ptr<IImageStreamInfo> pImgStreamInfo = nullptr;
  std::shared_ptr<IImageBuffer> pImgBuf = nullptr;

  //
  NSCam::NSIoPipe::PortID portID = NSCam::NSIoPipe::PortID();
  MSize dstSize = MSize(0, 0);
  MRect cropRect = MRect(MPoint(0, 0), MSize(0, 0));
  MUINT32 rawOutFmt = 0;
  //
  STREAM_IMG streamImg = STREAM_IMG_NUM;
  //
  if ((act->reqType == REQ_TYPE_UNKNOWN) || (act->reqType == REQ_TYPE_REDO) ||
      (act->reqType == REQ_TYPE_YUV)) {
    MY_LOGW("mismatch act type " P1INFO_ACT_STR, P1INFO_ACT_VAR(*act));
    return BAD_VALUE;
  }
  //
  P1_TRACE_F_BEGIN(SLG_I, "P1:setup|Mnum:%d SofIdx:%d Fnum:%d Rnum:%d",
                   act->magicNum, act->sofIdx, act->frmNum, act->reqNum);
  //
#if (IS_P1_LOGI)
  std::string strInfo("");
#endif
  //
  for (out = 0; out < REQ_OUT_MAX; out++) {
    if (!(IS_OUT(out, act->reqOutSet))) {
      continue;
    }
    P1_TRACE_F_BEGIN(SLG_I, "REQ_OUT_%d", out);
    pImgStreamInfo = nullptr;
    pImgBuf = nullptr;
    streamImg = STREAM_IMG_NUM;
    switch (out) {
      case REQ_OUT_LCSO:
      case REQ_OUT_LCSO_STUFF:
        streamImg = STREAM_IMG_OUT_LCS;
        portID = PORT_LCSO;
        dstSize = mvStreamImg[streamImg]->getImgSize();
        cropRect = MRect(MPoint(0, 0), mvStreamImg[streamImg]->getImgSize());
        rawOutFmt = (EPipe_PROCESSED_RAW);
        if (out == REQ_OUT_LCSO_STUFF) {
          // not use stuff buffer with height:1
          cropRect.s = dstSize;
        }
        break;

      case REQ_OUT_RSSO:
      case REQ_OUT_RSSO_STUFF:
        streamImg = STREAM_IMG_OUT_RSS;
        portID = PORT_RSSO;
        dstSize = mvStreamImg[streamImg]->getImgSize();
        cropRect = MRect(MPoint(0, 0), mvStreamImg[streamImg]->getImgSize());
        rawOutFmt = (EPipe_PROCESSED_RAW);
        if (out == REQ_OUT_RSSO_STUFF) {
          // not use stuff buffer with height:1
          cropRect.s = dstSize;
        }
        break;

      case REQ_OUT_RESIZER:
      case REQ_OUT_RESIZER_STUFF:
        streamImg = STREAM_IMG_OUT_RESIZE;
        portID = PORT_RRZO;
        dstSize = act->dstSize_resizer;
        cropRect = act->cropRect_resizer;
        rawOutFmt = (EPipe_PROCESSED_RAW);
        if (out == REQ_OUT_RESIZER_STUFF) {
          // use stuff buffer with height:1
          cropRect.s = dstSize;
        }
        break;

      case REQ_OUT_FULL_PROC:
      case REQ_OUT_FULL_PURE:
      case REQ_OUT_FULL_OPAQUE:
      case REQ_OUT_FULL_STUFF:
        streamImg = STREAM_IMG_OUT_FULL;
        if (out == REQ_OUT_FULL_OPAQUE ||
            (out == REQ_OUT_FULL_STUFF &&
             act->streamBufImg[STREAM_IMG_OUT_OPAQUE].bExist)) {
          streamImg = STREAM_IMG_OUT_OPAQUE;
        } else if (mvStreamImg[STREAM_IMG_OUT_FULL] != nullptr) {
          streamImg = STREAM_IMG_OUT_FULL;
        } else if (mvStreamImg[STREAM_IMG_OUT_OPAQUE] != nullptr) {
          streamImg = STREAM_IMG_OUT_OPAQUE;
        }
        portID = PORT_IMGO;
        dstSize = act->dstSize_full;
        cropRect = act->cropRect_full;
        rawOutFmt = act->fullRawType;
        //
        if (out == REQ_OUT_FULL_STUFF) {
          cropRect.s = dstSize;
        }
        break;
    }
    //
    if (streamImg < STREAM_IMG_NUM) {
      pImgStreamInfo = mvStreamImg[streamImg];
    } else {
      MY_LOGW(
          "cannot find the StreamImg num:%d out:%d "
          "streamImg:%d",
          act->magicNum, out, streamImg);
      return BAD_VALUE;
    }
    if (pImgStreamInfo == nullptr) {
      MY_LOGW(
          "cannot find the ImgStreamInfo num:%d out:%d "
          "streamImg:%d",
          act->magicNum, out, streamImg);
      return BAD_VALUE;
    }
    //
    MERROR err = OK;
    if (out == REQ_OUT_FULL_STUFF || out == REQ_OUT_RESIZER_STUFF ||
        out == REQ_OUT_LCSO_STUFF || out == REQ_OUT_RSSO_STUFF) {
      err = act->stuffImageGet(streamImg, dstSize, &pImgBuf);
    } else if (act->reqType == REQ_TYPE_INITIAL) {
      // the initial act with the pool, it do not use the stuff buffer
      err = act->poolImageGet(streamImg, &pImgBuf);
    } else {  // REQ_TYPE_NORMAL
      if (OK != act->frameImageGet(streamImg, &pImgBuf)) {
        // keep en-queue/de-queue processing
        if (out == REQ_OUT_LCSO || out == REQ_OUT_RSSO ||
            ((mEnableDumpRaw || mCamDumpEn) &&
             (out == REQ_OUT_FULL_PURE || out == REQ_OUT_FULL_PROC ||
              out == REQ_OUT_FULL_OPAQUE))) {
          MY_LOGI("keep the output size out:%d", out);
        } else {
          cropRect.s.h = dstSize.h;
        }
        err = act->stuffImageGet(streamImg, dstSize, &pImgBuf);
        if (out == REQ_OUT_RESIZER) {
          act->expRec |= EXP_REC(EXP_EVT_NOBUF_RRZO);
        } else if (out == REQ_OUT_LCSO) {
          act->expRec |= EXP_REC(EXP_EVT_NOBUF_LCSO);
        } else if (out == REQ_OUT_RSSO) {
          act->expRec |= EXP_REC(EXP_EVT_NOBUF_RSSO);
        } else {
          act->expRec |= EXP_REC(EXP_EVT_NOBUF_IMGO);
        }
        MY_LOGI(
            "underway-stuff-buffer status(%d) out[%s](%d) "
            "stream(%#" PRIx64 ") " P1INFO_ACT_STR,
            err, P1_PORT_TO_STR(portID), out, pImgStreamInfo->getStreamId(),
            P1INFO_ACT_VAR(*act));
      }
    }
    //
    if (CC_UNLIKELY((pImgBuf == nullptr) || (err != OK))) {
      MY_LOGE("Cannot get ImgBuf status(%d) out[%s](%d)" P1INFO_ACT_STR, err,
              P1_PORT_TO_STR(portID), out, P1INFO_ACT_VAR(*act));
      mLogInfo.inspect(LogInfo::IT_BUFFER_EXCEPTION);
      return BAD_VALUE;
    }
    //
    if ((out == REQ_OUT_RESIZER || out == REQ_OUT_RESIZER_STUFF) ||
        (out == REQ_OUT_FULL_PURE || out == REQ_OUT_FULL_PROC ||
         out == REQ_OUT_FULL_OPAQUE || out == REQ_OUT_FULL_STUFF)) {
      std::shared_ptr<IImageBufferHeap> pHeap = pImgBuf->getImageBufferHeap();
      if (pHeap != nullptr) {
        pHeap->setColorArrangement((MINT32)mSensorFormatOrder);
      }
    }
    //
#if (IS_P1_LOGI)
    if (1 <= mLogLevelI) {
      strInfo += base::StringPrintf(
          "[%s][%d](x%x)"
          "(Buf)(%dx%d)(S:%d:%d P:%p V:%p F:0x%x)"
          "(Crop)(%d,%d-%dx%d)(%dx%d) ",
          P1_PORT_TO_STR(portID), out, rawOutFmt, pImgBuf->getImgSize().w,
          pImgBuf->getImgSize().h,
          static_cast<int>(pImgBuf->getBufStridesInBytes(0)),
          static_cast<int>(pImgBuf->getBufSizeInBytes(0)),
          reinterpret_cast<void*>(pImgBuf->getBufPA(0)),
          reinterpret_cast<void*>(pImgBuf->getBufVA(0)),
          pImgBuf->getImgFormat(), cropRect.p.x, cropRect.p.y, cropRect.s.w,
          cropRect.s.h, dstSize.w, dstSize.h);
    }
#endif
    BufInfo rBufInfo(portID, pImgBuf.get(), dstSize, cropRect, act->magicNum,
                     act->sofIdx, rawOutFmt);
    (*info).mvOut.push_back(rBufInfo);
    P1_TRACE_C_END(SLG_I);  // "REQ_OUT"
  }                         // end of the loop for each out
  {
    if (IS_PORT(CONFIG_PORT_EISO, mConfigPort)) {
      std::shared_ptr<IImageBuffer> pImgBuf = NULL;
      MY_LOGD("mpConnectLMV->getBuf +++");
      if (IS_LMV(mpConnectLMV)) {
        mpConnectLMV->getBuf(&pImgBuf);
      }
      MY_LOGD("mpConnectLMV->getBuf ---");
      if (pImgBuf == NULL) {
        MY_LOGE("(%d) Cannot get LMV buffer", act->magicNum);
        return BAD_VALUE;
      }
      MY_LOGD(" get LMV out[%s](%d) P(%p) V(%p)" P1INFO_ACT_STR,
              P1_PORT_TO_STR(PORT_EISO), out, (void*)pImgBuf->getBufPA(0),
              (void*)pImgBuf->getBufVA(0), P1INFO_ACT_VAR(*act));
      act->buffer_eiso = pImgBuf;
      BufInfo rBufInfo(PORT_EISO, pImgBuf.get(), pImgBuf->getImgSize(),
                       MRect(MPoint(0, 0), pImgBuf->getImgSize()),
                       act->magicNum, act->sofIdx);
      (*info).mvOut.push_back(rBufInfo);
    }
  }
  mTagEnq.set(rAct->getNum());
  if (1 <= mLogLevelI) {
    P1_TRACE_F_BEGIN(SLG_PFL,
                     "P1::ENQ_LOG|Mnum:%d SofIdx:%d Fnum:%d "
                     "Rnum:%d FlushSet:0x%x",
                     act->magicNum, act->sofIdx, act->frmNum, act->reqNum,
                     act->flushSet);
    P1_LOGI(1, "[P1::ENQ]" P1INFO_ACT_STR " %s", P1INFO_ACT_VAR(*act),
            strInfo.c_str());
    P1_TRACE_C_END(SLG_PFL);  // "P1::ENQ_LOG"
  }
  //
  P1_TRACE_C_END(SLG_I);  // "P1:setup"
#endif
  FUNCTION_OUT;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::hardwareOps_enque(P1QueJob* job, ENQ_TYPE type, MINT64 data) {
  FUNCTION_IN;
  P1_TRACE_AUTO(SLG_I, "P1:enque");

  if (!getActive()) {
    return BAD_VALUE;
  }
  if (mpCamIO == nullptr) {
    MY_LOGE("NormalPipe is NULL");
    return BAD_VALUE;
  }
  MY_LOGD("EnQ[%d](%" PRId64 ") @ (%d)", type, data, job->getIdx());
  MBOOL toPush = (type == ENQ_TYPE_INITIAL) ? MFALSE : MTRUE;
  MBOOL toSwitchUNI = MFALSE;
  MUINT8 toSwtTgNum = 0;
  MUINT32 toSwitchQuality = QUALITY_SWITCH_STATE_NONE;
#if SUPPORT_ISP
  //
  QBufInfo enBuf;
  QBufInfo* pEnBuf = &enBuf;
  if ((type == ENQ_TYPE_INITIAL) && (!EN_INIT_REQ_RUN)) {
    if (mpConCtrl == nullptr || (!mpConCtrl->initBufInfo_get(&pEnBuf)) ||
        pEnBuf == nullptr) {
      MY_LOGE("CANNOT get the initBufInfo");
      return BAD_VALUE;
    }
  } else {
    for (size_t i = 0; i < job->size(); i++) {
      MY_LOGD("job(%d)(%zu/%zu)", job->getIdx(), i, job->size());
      P1QueAct& rAct = job->edit(i);
      P1Act act = GET_ACT_PTR(act, rAct, BAD_VALUE);
      if (OK != setupAction(&rAct, &enBuf)) {
        MY_LOGE("setup enque act fail");
        return BAD_VALUE;
      }
      if (i == 0 && act->reqType == REQ_TYPE_NORMAL) {
        if (type == ENQ_TYPE_DIRECTLY) {
          act->frameExpDuration = data * ONE_US_TO_NS;
        }
        enBuf.mShutterTimeNs = act->frameExpDuration;
      }
      if (act->uniSwitchState == UNI_SWITCH_STATE_REQ) {
        toSwitchUNI = MTRUE;
      }
      if (act->tgSwitchState == TG_SWITCH_STATE_REQ) {
        toSwtTgNum = act->tgSwitchNum;
      }
      if ((act->qualitySwitchState & QUALITY_SWITCH_STATE_REQ_NON) > 0) {
        toSwitchQuality = act->qualitySwitchState;
      }
      act->exeState = EXE_STATE_PROCESSING;
    }
  }
  //
  if (EN_START_CAP && (!getReady()) && (type == ENQ_TYPE_NORMAL)) {
    std::unique_lock<std::mutex> lck(mStartCaptureLock);
    MUINT32 cnt = 0;
    std::cv_status res;
    MY_LOGD("StartCaptureState(%d) Cnt(%d)", mStartCaptureState, cnt);
    while (mStartCaptureState == START_CAP_STATE_WAIT_CB) {
      P1_TRACE_F_BEGIN(SLG_S, "StartCapture wait [%d]", cnt);
      res = mStartCaptureCond.wait_for(
          lck, std::chrono::nanoseconds(P1_CAPTURE_CHECK_INV_NS));
      P1_TRACE_C_END(SLG_S);  // "StartCapture wait"
      if (res == std::cv_status::timeout) {
        MY_LOGI("StartCap(%d) Cnt(%d) Res(%d)", mStartCaptureState, cnt, res);
        mLogInfo.inspect(LogInfo::IT_WAIT_CATURE);
      } else {
        break;
      }
    }
    P1Act act = GET_ACT_PTR(act, job->edit(0), BAD_VALUE);
    act->capType = mStartCaptureType;
    act->frameExpDuration = mStartCaptureExp;
    act->sofIdx = mStartCaptureIdx;
    for (size_t i = 0; i < pEnBuf->mvOut.size(); i++) {
      pEnBuf->mvOut[i].FrameBased.mSOFidx = mStartCaptureIdx;
    }
    pEnBuf->mShutterTimeNs = mStartCaptureExp;
  }
  //
  if (toSwitchUNI) {
    UNI_SWITCH_STATE uniState = UNI_SWITCH_STATE_REQ;
    MUINT32 switchState = 0;
    MBOOL res = MFALSE;
    if (mpCamIO->sendCommand(ENPipeCmd_GET_UNI_SWITCH_STATE,
                             (MINTPTR)(&switchState), (MINTPTR)NULL,
                             (MINTPTR)NULL) &&
        switchState == 0) {  // DRV: If switch state is NULL, then do switch.
      res = mpCamIO->sendCommand(ENPipeCmd_UNI_SWITCH, (MINTPTR)NULL,
                                 (MINTPTR)NULL, (MINTPTR)NULL);
      if (res) {
        uniState = UNI_SWITCH_STATE_ACT_ACCEPT;
      } else {
        uniState = UNI_SWITCH_STATE_ACT_IGNORE;
      }
    } else {
      uniState = UNI_SWITCH_STATE_ACT_REJECT;
    }
    //
    for (size_t i = 0; i < job->size(); i++) {
      P1Act act = GET_ACT_PTR(act, job->edit(i), BAD_VALUE);
      if (act->uniSwitchState == UNI_SWITCH_STATE_REQ) {
        act->uniSwitchState = uniState;
        MY_LOGD("UNI-Switch(%d)(%d,%d) drv(%d,%d):(%d)", act->magicNum,
                act->frmNum, act->reqNum, switchState, res, uniState);
      }
    }
  }
  //
  if (toSwtTgNum) {
    TG_SWITCH_STATE tgState = TG_SWITCH_STATE_DONE_IGNORE;
    MBOOL res = MFALSE;
    MBOOL ret = MFALSE;
    MBOOL rev = MFALSE;
    MBOOL isOn = MFALSE;
    E_CamHwPathCfg curCfg = eCamHwPathCfg_Num;
    E_CamHwPathCfg tarCfg = eCamHwPathCfg_Num;
    switch (toSwtTgNum) {
      case 1:
        tarCfg = eCamHwPathCfg_One_TG;
        break;
      case 2:
        tarCfg = eCamHwPathCfg_Two_TG;
        break;
      default:
        MY_LOGI("check act TG state num (%d)", toSwtTgNum);
        break;
    }
    if (mpCamIO != nullptr) {
      res = mpCamIO->sendCommand(ENPipeCmd_GET_DTwin_INFO, (MINTPTR)(&isOn),
                                 (MINTPTR)NULL, (MINTPTR)NULL);
      if (res && isOn) {
        ret =
            mpCamIO->sendCommand(ENPipeCmd_GET_HW_PATH_CFG, (MINTPTR)(&curCfg),
                                 (MINTPTR)NULL, (MINTPTR)NULL);
      }
      if (!res) {
        MY_LOGI("sendCmd ENPipeCmd_GET_DTwin_INFO (%d)", res);
      } else if (!isOn) {
        MY_LOGI("DynamicTwin not ready (%d)", isOn);
      } else if (!ret) {
        MY_LOGI("sendCmd ENPipeCmd_GET_HW_PATH_CFG (%d)", ret);
      } else if (curCfg == eCamHwPathCfg_Num) {
        MY_LOGI("check current num (%d)", curCfg);
      } else if (tarCfg == eCamHwPathCfg_Num) {
        MY_LOGI("check target num (%d)", tarCfg);
      } else if (curCfg == tarCfg) {
        MY_LOGI("CamHwPathCfg is ready (%d) == (%d)", curCfg, tarCfg);
      } else {
        rev = mpCamIO->sendCommand(ENPipeCmd_SET_HW_PATH_CFG, (MINTPTR)(tarCfg),
                                   (MINTPTR)NULL, (MINTPTR)NULL);
        if (!rev) {
          MY_LOGI("sendCmd ENPipeCmd_SET_HW_PATH_CFG (%d)", rev);
          tgState = TG_SWITCH_STATE_DONE_REJECT;
        } else {
          tgState = TG_SWITCH_STATE_DONE_ACCEPT;
        }
      }
    }
    //
    for (size_t i = 0; i < job->size(); i++) {
      P1Act act = GET_ACT_PTR(act, job->edit(i), BAD_VALUE);
      if (act->tgSwitchState == TG_SWITCH_STATE_REQ) {
        act->tgSwitchState = tgState;
        act->tgSwitchNum = 0;
        MY_LOGI("TG(%d)(%d,%d) Drv(%d) Swt(%d)(%d,%d)(%d,%d,%d):%d",
                act->magicNum, act->frmNum, act->reqNum, isOn, toSwtTgNum,
                curCfg, tarCfg, res, ret, rev, tgState);
      }
    }
  }
  //
  if (toSwitchQuality != QUALITY_SWITCH_STATE_NONE) {
    QUALITY_SWITCH_STATE switchQuality = QUALITY_SWITCH_STATE_DONE_REJECT;
    MBOOL ret = MFALSE;
    if (mpCamIO != nullptr && mpRegisterNotify != nullptr) {
      E_CamIQLevel CamLvA = eCamIQ_L;
      E_CamIQLevel CamLvB = eCamIQ_L;
      if ((toSwitchQuality & QUALITY_SWITCH_STATE_REQ_H_A) > 0) {
        CamLvA = eCamIQ_H;
      }
      if ((toSwitchQuality & QUALITY_SWITCH_STATE_REQ_H_B) > 0) {
        CamLvB = eCamIQ_H;
      }
      ret =
          mpCamIO->sendCommand(ENPipeCmd_SET_QUALITY,
                               (MINTPTR)(mpRegisterNotify->getNotifyQuality()),
                               (MINTPTR)CamLvA, (MINTPTR)CamLvB);
      if (!ret) {
        MY_LOGI("sendCommand ENPipeCmd_SET_QUALITY fail(%d)", ret);
        switchQuality = QUALITY_SWITCH_STATE_DONE_REJECT;
        setQualitySwitching(MFALSE);
      } else {
        switchQuality = QUALITY_SWITCH_STATE_DONE_ACCEPT;
      }
    }
    for (size_t i = 0; i < job->size(); i++) {
      P1Act act = GET_ACT_PTR(act, job->edit(i), BAD_VALUE);
      if ((act->qualitySwitchState & QUALITY_SWITCH_STATE_REQ_NON) > 0) {
        MY_LOGI("ResizeQ (%d)(%d,%d) Ret(%d) QualitySwt(%d => %d)",
                act->magicNum, act->frmNum, act->reqNum, ret,
                act->qualitySwitchState, switchQuality);
        act->qualitySwitchState = switchQuality;
      }
    }
  }
  //
  if (toPush) {
    std::lock_guard<std::mutex> _l(mProcessingQueueLock);
    mProcessingQueue.push_back(*job);
    MY_LOGD("Push(%d) to ProQ(%zu)", job->getIdx(), mProcessingQueue.size());
  }
  //
  MBOOL isErr = MFALSE;
  P1Act act = GET_ACT_PTR(act, job->edit(0), BAD_VALUE);
  MINT32 numF = act->frmNum;
  MINT32 numR = act->reqNum;
  // check CtrlSync before EnQ
  if ((IS_BURST_OFF) &&  // exclude burst mode
      (type != ENQ_TYPE_INITIAL) && (job->size() >= 1)) {
    attemptCtrlSync(&(job->edit(0)));
  }
  //
  if ((mspSyncHelper != nullptr) && (type != ENQ_TYPE_INITIAL)) {
    IMetadata ctrlMeta;
    act->frameMetadataGet(STREAM_META_IN_HAL, &ctrlMeta);
    mspSyncHelper->syncEnqHW(getOpenId(), &ctrlMeta);
  }
  //
  if (type == ENQ_TYPE_DIRECTLY) {
#if (MTKCAM_HAVE_SANDBOX_SUPPORT == 0)
    P1_TRACE_F_BEGIN(SLG_E,
                     "P1:DRV-resume|"
                     "Mnum:%d SofIdx:%d Fnum:%d Rnum:%d",
                     act->magicNum, act->sofIdx, numF, numR);
    MY_LOGI("mpCamIO->resume +++");
    if (!mpCamIO->resume((QBufInfo const*)(pEnBuf))) {
      MY_LOGE("[SUS-RES] DRV resume fail");
      if (mpHwStateCtrl != nullptr) {
        mpHwStateCtrl->dump();
      }
      isErr = MTRUE;
    }
    MY_LOGI("mpCamIO->resume ---");
    P1_TRACE_C_END(SLG_E);  // "P1:DRV-resume"
#endif
  } else {  // ENQ_TYPE_NORMAL / ENQ_TYPE_INITIAL
    mLogInfo.setMemo(LogInfo::CP_ENQ_BGN, act->magicNum, numF, numR,
                     act->sofIdx);
    P1_TRACE_F_BEGIN(SLG_I,
                     "P1:DRV-enque|"
                     "Mnum:%d SofIdx:%d Fnum:%d Rnum:%d",
                     act->magicNum, act->sofIdx, numF, numR);
    {
      IHalSensorList* pHalSensorList = GET_HalSensorList();
      IHalSensor* pHalSensor =
          pHalSensorList->createSensor(LOG_TAG, getOpenId());
      MUINT32 sensorDevId = pHalSensorList->querySensorDevIdx(getOpenId());
      MY_LOGD("openId %d, sensorDevId %d, mMeta_PatMode %d", getOpenId(),
              sensorDevId, mMeta_PatMode);
      MINT ret = pHalSensor->sendCommand(
          sensorDevId, SENSOR_CMD_SET_TEST_PATTERN_OUTPUT,
          (MUINTPTR)&mMeta_PatMode, sizeof(MUINT32), 0, sizeof(MUINT32), 0,
          sizeof(MUINT32));
      if (ret != 0) {
        MY_LOGE("sendCommand set pattern output fail(%d)", ret);
      }
    }
    MY_LOGI("mpCamIO->enque +++");
    if (!mpCamIO->enque(*pEnBuf)) {
      MY_LOGE("DRV-enque fail");
      isErr = MTRUE;
    }
    MY_LOGI("mpCamIO->enque ---");
    P1_TRACE_C_END(SLG_I);  // "P1:DRV-enque"
    mLogInfo.setMemo(LogInfo::CP_ENQ_END, act->magicNum, numF, numR,
                     act->sofIdx);
  }
  //
  if (isErr) {
    if (toPush) {
      std::lock_guard<std::mutex> _l(mProcessingQueueLock);
      Que_T::iterator it = mProcessingQueue.begin();
      for (; it != mProcessingQueue.end(); it++) {
        if ((*it).getIdx() == job->getIdx()) {
          break;
        }
      }
      if (it != mProcessingQueue.end()) {
        mProcessingQueue.erase(it);
      }
      MY_LOGD("Erase(%d) from ProQ(%zu)", job->getIdx(),
              mProcessingQueue.size());
    }
    return BAD_VALUE;
  }
  //
  if (type == ENQ_TYPE_INITIAL) {
    mpConCtrl->initBufInfo_clean();
  }
#endif
  FUNCTION_OUT;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::hardwareOps_deque(QBufInfo* deqBuf) {
#if SUPPORT_ISP

  FUNCTION_IN;
  P1_TRACE_AUTO(SLG_I, "P1:deque");

  if (!getActive()) {
    return BAD_VALUE;
  }

  std::lock_guard<std::mutex> _l(mHardwareLock);
  if (!getActive()) {
    return BAD_VALUE;
  }

  {
    // deque buffer, and handle frame and metadata
    MY_LOGD("%" PRId64 ", %f", mDequeThreadProfile.getAvgDuration(),
            mDequeThreadProfile.getFps());
    QPortID PortID;
    if (IS_PORT(CONFIG_PORT_IMGO,
                mConfigPort)) {  // (mvOutImage_full.size() > 0) {
      PortID.mvPortId.push_back(PORT_IMGO);
    }
    if (IS_PORT(CONFIG_PORT_RRZO,
                mConfigPort)) {  // (mOutImage_resizer != NULL) {
      PortID.mvPortId.push_back(PORT_RRZO);
    }
    if (IS_PORT(CONFIG_PORT_EISO, mConfigPort)) {
      PortID.mvPortId.push_back(PORT_EISO);
    }
    if (IS_PORT(CONFIG_PORT_LCSO, mConfigPort)) {
      PortID.mvPortId.push_back(PORT_LCSO);
    }
    if (IS_PORT(CONFIG_PORT_RSSO, mConfigPort)) {
      PortID.mvPortId.push_back(PORT_RSSO);
    }

    // for mBurstNum: 4 and port: I+R+E+L, the buffer is as
    // [I1][I2][I3][I4][R1][R2][R3][R4][E1][E2][E3][E4][L1][L2][L3][L4]
    mDequeThreadProfile.pulse_down();
    //
    P1_TRACE_F_BEGIN(SLG_I, "P1:DRV-deque@[0x%X]", mConfigPort);
    mLogInfo.setMemo(LogInfo::CP_DEQ_BGN);
    MY_LOGI("mpCamIO->deque +++");
    if (!mpCamIO->deque(PortID, deqBuf)) {
      if (getActive()) {
        MY_LOGE("DRV-deque fail");
      } else {
        MY_LOGI("DRV-deque fail - after stop");
        P1_TRACE_C_END(SLG_I);  // "P1:DRV-deque"
        return OK;
      }
      P1_TRACE_C_END(SLG_I);  // "P1:DRV-deque"
      return BAD_VALUE;
    }
    MY_LOGI("mpCamIO->deque ---");
    mLogInfo.setMemo(LogInfo::CP_DEQ_END,
                     (deqBuf->mvOut.size() > 0)
                         ? (deqBuf->mvOut[0].mMetaData.mMagicNum_hal)
                         : 0);
    P1_TRACE_C_END(SLG_I);  // "P1:DRV-deque"
    //
    mDequeThreadProfile.pulse_up();
  }
  for (size_t i = 0; i < deqBuf->mvOut.size(); i++) {
    MY_LOGI(
        "P1 width*height:%d*%d, mvPortId %d, mSize %d, getBufSizeInBytes(0) "
        "%d, mMetaData.mDstSize.w %d, mMetaData.mDstSize.h %d",
        deqBuf->mvOut[i].mBuffer->getImgSize().w,
        deqBuf->mvOut[i].mBuffer->getImgSize().h,
        deqBuf->mvOut[i].mPortID.index, deqBuf->mvOut[i].mSize,
        deqBuf->mvOut[i].mBuffer->getBufSizeInBytes(0),
        deqBuf->mvOut[i].mMetaData.mDstSize.w,
        deqBuf->mvOut[i].mMetaData.mDstSize.h);
  }
  //
  if (mDebugScanLineMask != 0 && mpDebugScanLine != nullptr) {
    P1_TRACE_AUTO(SLG_E, "DrawScanLine");
    for (size_t i = 0; i < deqBuf->mvOut.size(); i++) {
      if ((deqBuf->mvOut[i].mPortID.index == PORT_RRZO.index &&
           mDebugScanLineMask & DRAWLINE_PORT_RRZO) ||
          (deqBuf->mvOut[i].mPortID.index == PORT_IMGO.index &&
           mDebugScanLineMask & DRAWLINE_PORT_IMGO)) {
        mpDebugScanLine->drawScanLine(
            deqBuf->mvOut[i].mBuffer->getImgSize().w,
            deqBuf->mvOut[i].mBuffer->getImgSize().h,
            reinterpret_cast<void*>(deqBuf->mvOut[i].mBuffer->getBufVA(0)),
            deqBuf->mvOut[i].mBuffer->getBufSizeInBytes(0),
            deqBuf->mvOut[i].mBuffer->getBufStridesInBytes(0));
      }
    }
  }
#if 1
  if (mEnableDumpRaw && deqBuf->mvOut.size() > 0) {
    MUINT32 magicNum = deqBuf->mvOut.at(0).mMetaData.mMagicNum_hal;

    /* Record previous "debug.p1.pureraw_dump" prop value.
     * When current prop value is not equal to previous prop value, it will
     * start dump raw. When current prop value is > 0 value, it will dump
     * continuous raw. For example, assume current prop value is 10 ,it will
     * start continuous 10 raw dump.
     */
    static MINT32 prevDumpProp = 0;
    static MUINT32 continueDumpCount = 0;

    /* If current "debug.p1.pureraw_dump" prop value < 0, this variable will
     * save it. This variable is used to continuous magic number dump raws. For
     * example, assume current prop value is -20. When pipeline starts, it will
     * dump frames with magic num < 20.
     */
    static MUINT32 indexRawDump = 0;

    MINT32 currentDumpProp =
        property_get_int32("vendor.debug.p1.pureraw_dump", 0);

    if (prevDumpProp != currentDumpProp) {
      if (currentDumpProp == 0) {
        prevDumpProp = 0;
        indexRawDump = 0;
        continueDumpCount = 0;
      } else if (currentDumpProp < 0) {
        indexRawDump = (MUINT32)(-currentDumpProp);
      } else if (currentDumpProp > 0) {
        continueDumpCount = (MUINT32)currentDumpProp;
      }
      prevDumpProp = currentDumpProp;
    }

    if ((magicNum <= indexRawDump) || continueDumpCount > 0) {
      if (continueDumpCount > 0) {
        continueDumpCount--;
      }

      for (size_t i = 0; i < deqBuf->mvOut.size(); i++) {
        char filename[256] = {0};
        snprintf(filename, sizeof(filename),
                 "%s/p1_%u_%d_%04dx%04d_%04d_%d.raw", P1NODE_DUMP_PATH,
                 magicNum,
                 ((deqBuf->mvOut.at(i).mPortID.index == PORT_RRZO.index) ? (0)
                                                                         : (1)),
                 static_cast<int>(deqBuf->mvOut.at(i).mBuffer->getImgSize().w),
                 static_cast<int>(deqBuf->mvOut.at(i).mBuffer->getImgSize().h),
                 static_cast<int>(
                     deqBuf->mvOut.at(i).mBuffer->getBufStridesInBytes(0)),
                 static_cast<int>(mSensorFormatOrder));
        P1_TRACE_AUTO(SLG_E, filename);
        deqBuf->mvOut.at(i).mBuffer->saveToFile(filename);
        MY_LOGI("save to file : %s", filename);
      }
    }
  }
#endif

  FUNCTION_OUT;

  return OK;
#else
  return OK;
#endif
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::hardwareOps_stop() {
#if SUPPORT_ISP
  P1_TRACE_AUTO(SLG_B, "P1:hardwareOps_stop");

  // (1) handle active flag
  if (!getActive()) {
    MY_LOGD("active=%d - return", getActive());
    return OK;
  }

  FUNCTION_IN;
  MY_LOGI("Cam::%d Req=%d Set=%d Enq=%d Deq=%d Out=%d", getOpenId(),
          mTagReq.get(), mTagSet.get(), mTagEnq.get(), mTagDeq.get(),
          mTagOut.get());

  MINT32 frmNum = P1_FRM_NUM_NULL;
  MINT32 reqNum = P1_REQ_NUM_NULL;
  MINT32 cnt = lastFrameRequestInfoNotice(&frmNum, &reqNum);
  mLogInfo.setMemo(LogInfo::CP_OP_STOP_BGN, frmNum, reqNum, cnt);

  setActive(MFALSE);
  setReady(MFALSE);
  setStartState(NSP1Node::START_STATE_NULL);
  //
  {
    std::lock_guard<std::mutex> _ll(mFrameSetLock);
    mFrameSetAlready = MFALSE;
  }
  //
  if (getInit()) {
    MY_LOGI("mHardwareLock waiting +++");
    std::lock_guard<std::mutex> _l(mHardwareLock);
    MY_LOGI("mHardwareLock waiting ---");
  }

  if (mpHwStateCtrl != nullptr) {
    mpHwStateCtrl->reset();
  }

  // (2.2) stop 3A stt
#if SUPPORT_3A
  if (mp3A) {
    std::lock_guard<std::mutex> _sl(mStopSttLock);
    LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_OP_STOP_3A_STOPSTT_BGN,
                         LogInfo::CP_OP_STOP_3A_STOPSTT_END);
    P1_TRACE_S_BEGIN(SLG_S, "P1:3A-stopStt");
    MY_LOGI("mp3A->stopStt +++");
    mp3A->stopStt();
    MY_LOGI("mp3A->stopStt ---");
    P1_TRACE_C_END(SLG_S);  // "P1:3A-stopStt"
  }
#endif

#if MTKCAM_HAVE_SANDBOX_SUPPORT
  // stop v4l2sttpipe after 3A
  MY_LOGI("stop V4L2SttPipeMgr +++");
  if (CC_LIKELY(mpV4L2SttPipe.get() != nullptr)) {
    mpV4L2SttPipe->stop();
    mpV4L2SttPipe = nullptr;
  }
  MY_LOGI("stop V4L2SttPipeMgr ---");

  // stop Event listener
  MY_LOGI("stop V4L2HwEventWorker +++");
  auto _stopHwEventMgr = [this](size_t idx) -> void {
    if (mpV4L2HwEventMgr[idx].get() == nullptr) {
      return;
    }

    mpV4L2HwEventMgr[idx]->requestExit();  // request to exit first,
    mpV4L2HwEventMgr[idx]->signal();       // send a fake signal ASAP
    mpV4L2HwEventMgr[idx]->stop();         // stop loop
    mpV4L2HwEventMgr[idx] = nullptr;       // release resource
  };
  _stopHwEventMgr(0);
  _stopHwEventMgr(1);
  _stopHwEventMgr(2);
  MY_LOGI("stop V4L2HwEventWorker ---");
#endif

  // (2.3) stop isp
  if (CC_UNLIKELY(mpCamIO == nullptr)) {
    MY_LOGE("hardware CamIO not exist");
    return BAD_VALUE;
  }
  {
    // std::lock_guard<std::mutex> _l(mHardwareLock);
    if (mLongExp.get()) {
      LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_OP_STOP_DRV_STOP_BGN,
                           LogInfo::CP_OP_STOP_DRV_STOP_END,
                           MTRUE /* call abort */);
#if (MTKCAM_HAVE_SANDBOX_SUPPORT == 0)
      P1_TRACE_S_BEGIN(SLG_S, "P1:DRV-abort");
      MY_LOGI("mpCamIO->abort +++");
      if (!mpCamIO->abort()) {
        MY_LOGE("hardware abort fail");
        // return BAD_VALUE;
      }
      MY_LOGI("mpCamIO->abort ---");
      P1_TRACE_C_END(SLG_S);  // "P1:DRV-abort"
#endif
    } else {
      LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_OP_STOP_DRV_STOP_BGN,
                           LogInfo::CP_OP_STOP_DRV_STOP_END,
                           MFALSE /* not abort */);
      P1_TRACE_S_BEGIN(SLG_S, "P1:DRV-stop");
      MY_LOGI("mpCamIO->stop +++");
      if (!mpCamIO->stop(/*MTRUE*/)) {
        MY_LOGE("hardware stop fail");
      }
      MY_LOGI("mpCamIO->stop ---");
      P1_TRACE_C_END(SLG_S);  // "P1:DRV-stop"
    }
  }

  mLogInfo.setMemo(LogInfo::CP_OP_STOP_HW_LOCK_BGN);
  MY_LOGI("HwLockStopWait +++");
  std::lock_guard<std::mutex> _l(mHardwareLock);
  MY_LOGI("HwLockStopWait ---");
  mLogInfo.setMemo(LogInfo::CP_OP_STOP_HW_LOCK_END);

  // (3.0) stop 3A
#if SUPPORT_3A
  if (mp3A) {
    P1_TRACE_C_END(SLG_S);  // "P1:LMV-enableOIS"
#if SUPPORT_FSC
    if (mpFSC != nullptr) {
      mpFSC->Uninit(mp3A);  // detach 3A CB before 3A->stop()
    }
#endif
    P1_TRACE_S_BEGIN(SLG_S, "P1:3A-sendCtrl-detachCb");
    //
    mp3A->detachCb(IHal3ACb::eID_NOTIFY_3APROC_FINISH, this);
    mp3A->detachCb(IHal3ACb::eID_NOTIFY_CURR_RESULT, this);
    mp3A->detachCb(IHal3ACb::eID_NOTIFY_VSYNC_DONE, this);
    P1_TRACE_C_END(SLG_S);  // "P1:3A-sendCtrl-detachCb"
    LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_OP_STOP_3A_STOP_BGN,
                         LogInfo::CP_OP_STOP_3A_STOP_END);
    P1_TRACE_S_BEGIN(SLG_S, "P1:3A-stop");
    MY_LOGI("mp3A->stop +++");
    mp3A->stop();
    MY_LOGI("mp3A->stop ---");
    P1_TRACE_C_END(SLG_S);  // "P1:3A-stop"
  }
#endif

  // (3.1) destroy 3A
#if SUPPORT_3A
  if (mp3A) {
    if (getPowerNotify()) {
      LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_OP_STOP_3A_PWROFF_BGN,
                           LogInfo::CP_OP_STOP_3A_PWROFF_END);
      P1_TRACE_S_BEGIN(SLG_S, "P1:3A-notifyPwrOff");
      MY_LOGI("mp3A->notifyP1PwrOff +++");
      mp3A->notifyP1PwrOff();  // CCU DRV power off before ISP uninit.
      MY_LOGI("mp3A->notifyP1PwrOff ---");
      P1_TRACE_C_END(SLG_S);  // "P1:3A-notifyPwrOff"
    } else {
      MY_LOGI("3A->notifyP1PwrOff() no need");
    }
    setPowerNotify(MFALSE);
    //
    mp3A = nullptr;
  }
#endif

  // (3.1.1) stop v4l2 pipe (IPC only)
#if MTKCAM_HAVE_SANDBOX_SUPPORT
  if (mpV4L2LensMgr.get()) {
    MY_LOGI("stop V4L2LensMgr +++");
    mpV4L2LensMgr->stop();
    mpV4L2LensMgr = nullptr;
    MY_LOGI("stop V4L2LensMgr ---");
  }
  if (mpV4L2SensorMgr.get()) {
    MY_LOGI("stop V4L2SensorWorker +++");
    mpV4L2SensorMgr->stop();
    mpV4L2SensorMgr = nullptr;  // clear resource
    MY_LOGI("stop V4L2SensorWorker ---");
  }
  if (mpV4L2P13ACallback.get()) {
    MY_LOGI("stop V4L2P13ACallback +++");
    mpV4L2P13ACallback->stop();
    mpV4L2P13ACallback = nullptr;
    MY_LOGI("stop V4L2P13ACallback ---");
  }
  if (mpV4L2TuningPipe.get()) {
    MY_LOGI("stop V4L2TuningPipeMgr +++");
    mpV4L2TuningPipe->stop();
    mpV4L2TuningPipe = nullptr;
    MY_LOGI("stop V4L2TuningPipeMgr ---");
  }
#endif
  if (IS_LMV(mpConnectLMV)) {
    mpConnectLMV->uninit();
  }

  // (3.2) destroy isp
  {
    //
#if SUPPORT_LCS
    if (mpLCS) {
      mpLCS->Uninit();
      mpLCS->DestroyInstance(
          LOG_TAG);  // instance always exist until process kill
      mpLCS = nullptr;
    }
#endif
#if SUPPORT_RSS
    if (mpRSS != nullptr) {
      mpRSS->Uninit();
      mpRSS = nullptr;
    }
#endif
#if SUPPORT_FSC
    mpFSC = nullptr;
#endif
    //
    mLogInfo.setMemo(LogInfo::CP_OP_STOP_DRV_UNINIT_BGN);
    P1_TRACE_S_BEGIN(SLG_S, "P1:DRV-uninit");
    MY_LOGI("mpCamIO->uninit +++");
    if (!mpCamIO->uninit()) {
      MY_LOGE("hardware uninit fail");
      // return BAD_VALUE;
    }
    MY_LOGI("mpCamIO->uninit ---");
    P1_TRACE_C_END(SLG_S);  // "P1:DRV-uninit"
    mLogInfo.setMemo(LogInfo::CP_OP_STOP_DRV_UNINIT_END);
    //
    P1_TRACE_S_BEGIN(SLG_S, "P1:DRV-destroyInstance");
    MY_LOGI("mpCamIO->destroyInstance +++");
    mpCamIO = nullptr;
    MY_LOGI("mpCamIO->destroyInstance ---");

    P1_TRACE_C_END(SLG_S);  // "P1:DRV-destroyInstance"
  }
  //
  syncHelperStop();
  //
  if (mspResConCtrl != nullptr) {
    P1NODE_RES_CON_RELEASE(mspResConCtrl, mResConClient, mIsResConGot);
  }
  //
#if USING_CTRL_3A_LIST_PREVIOUS
  mPreviousCtrlList.clear();
#endif
  //
  mLogInfo.setMemo(LogInfo::CP_OP_STOP_END, frmNum, reqNum, cnt);
  //
  FUNCTION_OUT;

  return OK;

#else
  return OK;
#endif
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::hardwareOps_streaming() {
  P1_TRACE_AUTO(SLG_B, "P1:hardwareOps_streaming");
  if (CC_UNLIKELY(mpHwStateCtrl == nullptr)) {
    return BAD_VALUE;
  }
  if (!mpHwStateCtrl->checkReceiveRestreaming()) {
    return BAD_VALUE;
  }
  //
  if (mpHwStateCtrl->isLegacyStandby()) {
    MINT32 nShutterTimeUs = 0;
    mpHwStateCtrl->checkShutterTime(&nShutterTimeUs);
#if (MTKCAM_HAVE_SANDBOX_SUPPORT == 0)
    P1_TRACE_F_BEGIN(SLG_E, "P1:DRV-Resume(%d)", nShutterTimeUs);
    ret = mpCamIO->resume(nShutterTimeUs);
    P1_TRACE_C_END(SLG_E);  // "P1:DRV-Resume"
    if (!ret) {
      MY_LOGE("[SUS-RES] FAIL");
      mpHwStateCtrl->dump();
      mpHwStateCtrl->clean();
      return BAD_VALUE;
    }
#endif
    //
    P1_TRACE_S_BEGIN(SLG_E, "P1:3A-Resume");
    mp3A->resume();
    P1_TRACE_C_END(SLG_E);  // "P1:3A-Resume"
    //
    MY_LOGI("[SUS-RES] Recover-Loop-N");
    //
    mpHwStateCtrl->checkThreadWeakup();
  } else {
    if (CC_UNLIKELY(mpTaskCtrl == nullptr || mpTaskCollector == nullptr)) {
      return BAD_VALUE;
    }
    P1QueJob job(mBurstNum);
    mpTaskCollector->requireJob(&job);
    if (!job.ready()) {
      MY_LOGE("job-require-fail");
      mpTaskCtrl->dumpActPool();
      return BAD_VALUE;
    }
    P1Act pAct = GET_ACT_PTR(pAct, job.edit(0), BAD_VALUE);
    if (pAct->ctrlSensorStatus != SENSOR_STATUS_CTRL_STREAMING) {
      MY_LOGI("status-mismatch(%d)@(%d)", pAct->ctrlSensorStatus,
              pAct->getNum());
    }
    MINT32 nShutterTimeUs = 0;
    mpHwStateCtrl->checkShutterTime(&nShutterTimeUs);
    mpHwStateCtrl->checkRestreamingNum(pAct->getNum());
    {
      P1_TRACE_F_BEGIN(SLG_E, "P1:3A-resume(%d)", pAct->getNum());
      mp3A->resume(pAct->getNum());
      P1_TRACE_C_END(SLG_E);  // "P1:3A-resume"
    }
    MERROR status =
        hardwareOps_enque(&job, ENQ_TYPE_DIRECTLY, (MINT64)nShutterTimeUs);
    if (OK != status) {
      MY_LOGE("streaming en-queue fail (%d)@(%d)", status, job.getIdx());
      return BAD_VALUE;
    }
    //
    mpHwStateCtrl->checkThreadWeakup();
    mpHwStateCtrl->checkFirstSync();
  }
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::prepareCropInfo(P1QueAct* rAct,
                           IMetadata* pAppMetadata,
                           IMetadata* pHalMetadata,
                           PREPARE_CROP_PHASE phase,
                           MBOOL* pCtrlFlush) {
  P1Act act = GET_ACT_PTR(act, *rAct, RET_VOID);
  MSize refSensorSize = getCurrentBinSize();  // mSensorParams.size;
  MBOOL bIsBinEn = (refSensorSize == mSensorParams.size) ? MFALSE : MTRUE;
  MBOOL isFullBin = MFALSE;
  if (bIsBinEn && act->reqType == REQ_TYPE_NORMAL &&
      (/*IS_OUT(REQ_OUT_FULL_PROC, act->reqOutSet) ||*/
       act->fullRawType == EPipe_PROCESSED_RAW)) {
    isFullBin = MTRUE;
  }
  MY_LOGI(
      "[CropInfo][%d] +++ IsBinEn:%d IsFullBin:%d "
      "sensor(%dx%d) ref(%dx%d)",
      phase, mIsBinEn, isFullBin, mSensorParams.size.w, mSensorParams.size.h,
      refSensorSize.w, refSensorSize.h);
  act->refBinSize = refSensorSize;
  if (mvStreamImg[STREAM_IMG_OUT_FULL] != nullptr) {
    act->dstSize_full = mvStreamImg[STREAM_IMG_OUT_FULL]->getImgSize();
    act->cropRect_full =
        MRect(MPoint(0, 0), (isFullBin) ? refSensorSize : mSensorParams.size);
  } else if (mvStreamImg[STREAM_IMG_OUT_OPAQUE] != nullptr) {
    act->dstSize_full = mSensorParams.size;
    act->cropRect_full =
        MRect(MPoint(0, 0), (isFullBin) ? refSensorSize : mSensorParams.size);
  } else {
    act->dstSize_full = MSize(0, 0);
    act->cropRect_full = MRect(MPoint(0, 0), MSize(0, 0));
  }
  if (mvStreamImg[STREAM_IMG_OUT_RESIZE] != nullptr) {
    act->dstSize_resizer = mvStreamImg[STREAM_IMG_OUT_RESIZE]->getImgSize();
    act->cropRect_resizer = MRect(MPoint(0, 0), refSensorSize);
  } else {
    act->dstSize_resizer = MSize(0, 0);
    act->cropRect_resizer = MRect(MPoint(0, 0), MSize(0, 0));
  }
  MY_LOGI("[CropInfo][%d] --- [F] Src" P1_RECT_STR "Dst" P1_SIZE_STR
          "[R] Src" P1_RECT_STR "Dst" P1_SIZE_STR,
          phase, P1_RECT_VAR(act->cropRect_full),
          P1_SIZE_VAR(act->dstSize_full), P1_RECT_VAR(act->cropRect_resizer),
          P1_SIZE_VAR(act->dstSize_resizer));
}

#if USING_CTRL_3A_LIST
/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::generateCtrlList(List<NS3Av3::MetaSet_T>* pList, P1QueJob* rJob) {
  if (CC_UNLIKELY(pList == nullptr)) {
    MY_LOGE("List is nullptr");
    return;
  }
#define P1_3A_LIST_INDEX (2)
  size_t total = (P1_3A_LIST_INDEX * mBurstNum);
#if USING_CTRL_3A_LIST_PREVIOUS  // force to save and set previous metadata
  // #warning "using previously padding 3A Control List"
  // add dummy before first request
  while (mPreviousCtrlList.size() < total) {
    NS3Av3::MetaSet_T set;
    set.MagicNum = 0;
    set.Dummy = 1;
    mPreviousCtrlList.push_back(set);
  }
  // add this request
  for (size_t j = 0; j < (*rJob).size(); j++) {
    if ((*rJob).edit(j).ptr() != nullptr) {
      mPreviousCtrlList.push_back((*rJob).edit(j).ptr()->metaSet);
    }
  }
  // keep list length in (P1_3A_LIST_INDEX + 1)
  while ((mPreviousCtrlList.size() > (total + mBurstNum))) {
    mPreviousCtrlList.erase(mPreviousCtrlList.begin());
  }
  // copy the mPreviousCtrlList to set-CtrlList
  List<NS3Av3::MetaSet_T>::iterator p_it = mPreviousCtrlList.begin();
  for (; p_it != mPreviousCtrlList.end(); p_it++) {
    pList->push_back(*p_it);
  }
#else
  for (size_t i = 0; i < total; i++) {
    NS3Av3::MetaSet_T set;
    pList->push_back(set);
  }
  for (size_t j = 0; j < (*rJob).size(); j++) {
    if ((*rJob).edit(j).ptr() != nullptr) {
      pList->push_back((*rJob).edit(j).ptr()->metaSet);
    }
  }
#endif
  //
  if (mMetaLogOp > 0 && pList->size() > 0 &&
      pList->size() == ((*rJob).size() * (P1_3A_LIST_INDEX + 1))) {
    MY_LOGI("LogMeta List[%zu] Job[%zu]", pList->size(), (*rJob).size());
    List<NS3Av3::MetaSet_T>::iterator it = pList->begin();
    for (size_t j = 0; j < total && it != pList->end(); j++) {
      it++;
    }  // shift to (P1_3A_LIST_INDEX * mBurstNum)
    for (size_t i = 0; i < (*rJob).size() && it != pList->end(); i++, it++) {
      P1Act pAct = GET_ACT_PTR(pAct, (*rJob).edit((MUINT8)i), RET_VOID);
      P1_LOG_META(*pAct, &(it->appMeta), "3A.Set-APP");
      P1_LOG_META(*pAct, &(it->halMeta), "3A.Set-HAL");
      // P1_LOG_META(*pAct, &(pAct->metaSet.appMeta), "3A.Act-APP");
      // P1_LOG_META(*pAct, &(pAct->metaSet.halMeta), "3A.Act-HAL");
    }
  }
  return;
}
#endif

#if SUPPORT_LCS
MERROR P1NodeImp::lcsInit() {
  if (mEnableLCSO) {
    P1_TIMING_CHECK("P1:LCS-init", 10, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:LCS-init");
    MY_LOGI("MAKE_LcsHal +++");
    mpLCS = MAKE_LCSHAL_IPC(LOG_TAG, getOpenId());
    if (mpLCS == nullptr) {
      MY_LOGE("mpLCS is NULL");
      return DEAD_OBJECT;
    }
    if (mpLCS->Init() != LCS_RETURN_NO_ERROR) {
      mpLCS->DestroyInstance(LOG_TAG);
      mpLCS = nullptr;
    }
    MY_LOGI("MAKE_LcsHal ---");
    P1_TRACE_C_END(SLG_S);  // "P1:LCS-init"
  }
  return OK;
}
#endif

#if MTKCAM_HAVE_SANDBOX_SUPPORT
MVOID P1NodeImp::v4l2DeviceStart() {
  // update dynamic info after powered on sensor
  MY_LOGD("setDynamicSensorInfoToIPCHalSensor[+]");
  int err = setDynamicSensorInfoToIPCHalSensor(getOpenId());
  MY_LOGD("setDynamicSensorInfoToIPCHalSensor[-]");

  if (CC_UNLIKELY(err != 0)) {
    MY_LOGE("setDynamicSensorInfoToIPCHalSensor failed");
  }

  MY_LOGI("V4L2SensorWorker start +++");
  mpV4L2SensorMgr = std::make_shared<v4l2::V4L2SensorWorker>(getOpenId());
  mpV4L2SensorMgr->start();
  MY_LOGI("V4L2SensorWorker start ---");

  MY_LOGI("V4L2LensMgr start +++");
  mpV4L2LensMgr = std::make_shared<v4l2::V4L2LensMgr>(getOpenId());
  mpV4L2LensMgr->start();
  MY_LOGI("V4L2LensMgr start ---");

  MY_LOGI("V4L2P13ACallback start +++");
  mpV4L2P13ACallback =
      std::make_shared<v4l2::V4L2P13ACallback>(getOpenId(), this);
  mpV4L2P13ACallback->start();
  MY_LOGI("V4L2P13ACallback start ---");
}
#endif

MVOID P1NodeImp::addConfigPort(std::vector<PortInfo>* vPortInfo,
                               EImageFormat* resizer_fmt) {
  if (mvStreamImg[STREAM_IMG_OUT_FULL] != nullptr) {
    MINT fmt = mvStreamImg[STREAM_IMG_OUT_FULL]->getImgFormat();
    IImageStreamInfo::BufPlanes_t const& planes =
        mvStreamImg[STREAM_IMG_OUT_FULL]->getBufPlanes();
    PortInfo OutPort(PORT_IMGO, (EImageFormat)fmt,
                     mvStreamImg[STREAM_IMG_OUT_FULL]->getImgSize(),
                     MRect(MPoint(0, 0), mSensorParams.size),
                     P1_STRIDE(planes, 0), P1_STRIDE(planes, 1),
                     P1_STRIDE(planes, 2),
                     0,  // pureraw
                     MTRUE /*IS_RAW_FMT_PACK_FULL(fmt) packed*/,
                     10);  // packed
    // by driver request, the PAK should be ON even if un-packed raw
    (*vPortInfo).push_back(OutPort);
    mConfigPort |= CONFIG_PORT_IMGO;
    mConfigPortNum++;
  } else if (mvStreamImg[STREAM_IMG_OUT_OPAQUE] != nullptr) {
    PortInfo OutPort(PORT_IMGO, (EImageFormat)mRawFormat, mSensorParams.size,
                     MRect(MPoint(0, 0), mSensorParams.size), mRawStride,
                     0 /*StrideInByte[1]*/, 0 /*StrideInByte[2]*/,
                     0,                                            // pureraw
                     MTRUE /*IS_RAW_FMT_PACK_FULL(mRawFormat)*/);  // packed
    // by driver request, the PAK should be ON even if un-packed raw
    (*vPortInfo).push_back(OutPort);
    mConfigPort |= CONFIG_PORT_IMGO;
    mConfigPortNum++;
  }
  //
  if (mvStreamImg[STREAM_IMG_OUT_RESIZE] != nullptr) {
    IImageStreamInfo::BufPlanes_t const& planes =
        mvStreamImg[STREAM_IMG_OUT_RESIZE]->getBufPlanes();
    PortInfo OutPort(
        PORT_RRZO,
        (EImageFormat)mvStreamImg[STREAM_IMG_OUT_RESIZE]->getImgFormat(),
        mvStreamImg[STREAM_IMG_OUT_RESIZE]->getImgSize(),
        MRect(MPoint(0, 0), mSensorParams.size), P1_STRIDE(planes, 0),
        P1_STRIDE(planes, 1), P1_STRIDE(planes, 2),
        0,      // pureraw
        MTRUE,  // packed
        10);    // packed
    (*vPortInfo).push_back(OutPort);
    mConfigPort |= CONFIG_PORT_RRZO;
    mConfigPortNum++;
    //
    *resizer_fmt =
        (EImageFormat)mvStreamImg[STREAM_IMG_OUT_RESIZE]->getImgFormat();
  }

  if (mEnableLCSO && mvStreamImg[STREAM_IMG_OUT_LCS] != nullptr) {
    IImageStreamInfo::BufPlanes_t const& planes =
        mvStreamImg[STREAM_IMG_OUT_LCS]->getBufPlanes();
    PortInfo OutPort(
        PORT_LCSO,
        (EImageFormat)mvStreamImg[STREAM_IMG_OUT_LCS]->getImgFormat(),
        mvStreamImg[STREAM_IMG_OUT_LCS]->getImgSize(),
        MRect(MPoint(0, 0), mvStreamImg[STREAM_IMG_OUT_LCS]->getImgSize()),
        P1_STRIDE(planes, 0), P1_STRIDE(planes, 1), P1_STRIDE(planes, 2),
        0,      // pureraw
        MTRUE,  // packed
        10);
    (*vPortInfo).push_back(OutPort);
    mConfigPort |= CONFIG_PORT_LCSO;
    mConfigPortNum++;
  }

  if (mEnableRSSO && mvStreamImg[STREAM_IMG_OUT_RSS] != nullptr) {
    IImageStreamInfo::BufPlanes_t const& planes =
        mvStreamImg[STREAM_IMG_OUT_RSS]->getBufPlanes();
    PortInfo OutPort(
        PORT_RSSO,
        (EImageFormat)mvStreamImg[STREAM_IMG_OUT_RSS]->getImgFormat(),
        mvStreamImg[STREAM_IMG_OUT_RSS]->getImgSize(),
        MRect(MPoint(0, 0), mvStreamImg[STREAM_IMG_OUT_RSS]->getImgSize()),
        P1_STRIDE(planes, 0), P1_STRIDE(planes, 1), P1_STRIDE(planes, 2),
        0,       // pureraw
        MTRUE);  // packed
    (*vPortInfo).push_back(OutPort);
    mConfigPort |= CONFIG_PORT_RSSO;
    mConfigPortNum++;
  }
  if (mEnableEISO) {
    PortInfo OutPort(PORT_EISO, eImgFmt_BLOB, MSize(LMVO_MEMORY_SIZE, 1),
                     MRect(LMVO_MEMORY_SIZE, 1), LMVO_MEMORY_SIZE,
                     0,  // pPortCfg->mStrideInByte[1],
                     0,  // pPortCfg->mStrideInByte[2],
                     0,  // pureraw
                     MTRUE,
                     10);  // packed

    (*vPortInfo).push_back(OutPort);
    mConfigPort |= CONFIG_PORT_EISO;
    mConfigPortNum++;
  }
}

MERROR P1NodeImp::startCamIO(
    QInitParam halCamIOinitParam,
    MSize* binInfoSize,
    MSize rawSize[2],
    PipeTag* pipe_tag,
    std::map<int, std::vector<std::shared_ptr<IImageBuffer>>>* buffers) {
  {
    MERROR err = OK;
    P1_TIMING_CHECK("P1:DRV-init", 20, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:DRV-init");
    MY_LOGI("mpCamIO->init +++");
    if ((mConfigPort & CONFIG_PORT_RRZO) && (mConfigPort & CONFIG_PORT_IMGO)) {
      *pipe_tag = kPipeTag_Out2_Tuning;
    } else if ((mConfigPort & CONFIG_PORT_RRZO) ||
               (mConfigPort & CONFIG_PORT_IMGO)) {
      *pipe_tag = kPipeTag_Out1_Tuning;
    }
    if (err < 0 || !mpCamIO || !mpCamIO->init(*pipe_tag)) {
      MY_LOGE("hardware init fail - err:%#x mpCamIO:%p", err, mpCamIO.get());
      return DEAD_OBJECT;
    }
    MY_LOGI("mpCamIO->init ---");
    P1_TRACE_C_END(SLG_S);  // "P1:DRV-init"
  }
  MSize rawSize_l[2];
  MSize* pSizeProc = &rawSize_l[0];  // 0 = EPipe_PROCESSED_RAW
  MSize* pSizePure = &rawSize_l[1];  // 1 = EPipe_PURE_RAW
  *pSizeProc = MSize(0, 0);
  *pSizePure = MSize(0, 0);
  if (mConfigPort & CONFIG_PORT_EISO) {
    buffers->emplace(PORT_EISO.index,
                     std::vector<std::shared_ptr<IImageBuffer>>{});
  }
#if MTKCAM_HAVE_SANDBOX_SUPPORT
  IIPCHalSensor::DynamicInfo ipcDynamicInfo;
#endif
  if (mpCamIO != nullptr) {
    P1_TIMING_CHECK("P1:DRV-configPipe", 500, TC_W);
    mLogInfo.setMemo(LogInfo::CP_OP_START_DRV_CFG_BGN);
    P1_TRACE_S_BEGIN(SLG_S, "P1:DRV-configPipe");
    MY_LOGI("mpCamIO->configPipe +++");
    if (!mpCamIO->configPipe(halCamIOinitParam, buffers)) {
      MY_LOGE("mpCamIO->configPipe fail");
      P1_TRACE_C_END(SLG_S);  // "P1:DRV-configPipe"
      mLogInfo.setMemo(LogInfo::CP_OP_START_DRV_CFG_END);
      return BAD_VALUE;
    } else {
      MY_LOGI("mpCamIO->configPipe ---");
      P1_TRACE_C_END(SLG_S);  // "P1:DRV-configPipe"
      mLogInfo.setMemo(LogInfo::CP_OP_START_DRV_CFG_END);
      //
      {
        MBOOL notSupportProc = MFALSE;
        MBOOL notSupportPure = MFALSE;
        MUINT32 newDefType = mRawDefType;
        MUINT32 newOption = mRawOption;
        P1_TRACE_S_BEGIN(SLG_S, "P1:DRV-GetImgoInfo");
        if (mpCamIO->sendCommand(ENPipeCmd_GET_TG_OUT_SIZE,
                                 (MINTPTR)(&rawSize_l), (MINTPTR)NULL,
                                 (MINTPTR)NULL)) {
          P1_TRACE_C_END(SLG_S);  // "P1:DRV-GetImgoInfo"
          if (pSizeProc->w == 0 || pSizeProc->h == 0) {
            notSupportProc = MTRUE;
          }
          if (pSizePure->w == 0 || pSizePure->h == 0) {
            notSupportPure = MTRUE;
          }
        }
        if ((!notSupportProc) && (!notSupportPure)) {
          // both Proc raw and Pure raw are supported
          // not change the raw type setting
#if MTKCAM_HAVE_SANDBOX_SUPPORT
          // update resolution from any RAW type is ok.
          if (pSizePure->w != 0 && pSizePure->h != 0) {
            ipcDynamicInfo.tg_size = *pSizePure;
          }
#endif
        } else if ((!notSupportProc) && (notSupportPure)) {
          // only support Proc raw
          newDefType = EPipe_PROCESSED_RAW;
          newOption = (1 << EPipe_PROCESSED_RAW);
#if MTKCAM_HAVE_SANDBOX_SUPPORT
          ipcDynamicInfo.tg_size = *pSizeProc;
#endif
        } else if ((notSupportProc) && (!notSupportPure)) {
          // only support Pure raw
          newDefType = EPipe_PURE_RAW;
          newOption = (1 << EPipe_PURE_RAW);
#if MTKCAM_HAVE_SANDBOX_SUPPORT
          ipcDynamicInfo.tg_size = *pSizePure;
#endif
        } else {
          // not support Proc raw and Pure raw
          MY_LOGE(
              "Raw(%d,0x%x) Proc(%dx%d) Pure(%dx%d) "
              "- Not Support",
              mRawDefType, mRawOption, pSizeProc->w, pSizeProc->h, pSizePure->w,
              pSizePure->h);
          return BAD_VALUE;
        }
        MY_LOGI_IF((mRawDefType != newDefType) || (mRawOption != newOption),
                   "[RAW_TYPE] Raw(%d,0x%x) => New(%d,0x%x)"
                   "Proc(%dx%d) Pure(%dx%d)",
                   mRawDefType, mRawOption, newDefType, newOption, pSizeProc->w,
                   pSizeProc->h, pSizePure->w, pSizePure->h);
        //
        mRawDefType = newDefType;
        mRawOption = newOption;
      }
    }
  }

#if MTKCAM_HAVE_SANDBOX_SUPPORT
  {
    // after p1 started, update dynamic info again (for TG selection)
    MY_LOGD("setDynamicSensorInfoToIPCHalSensor[+]");
    int err = setDynamicSensorInfoToIPCHalSensor(getOpenId());
    MY_LOGD("setDynamicSensorInfoToIPCHalSensor[-]");
    if (CC_UNLIKELY(err != 0)) {
      MY_LOGE("setDynamicSensorInfoToIPCHalSensor failed");
    }

    // Before update extended dynamic info, check bin_size and hbin_size,
    // if it's 0, give it the same as tg size.
    if (ipcDynamicInfo.bin_size.w == 0 || ipcDynamicInfo.bin_size.h == 0) {
      ipcDynamicInfo.bin_size = ipcDynamicInfo.tg_size;
    }
    if (ipcDynamicInfo.hbin_size.w == 0 || ipcDynamicInfo.hbin_size.h == 0) {
      ipcDynamicInfo.hbin_size = ipcDynamicInfo.tg_size;
    }
    // Update extended dynamic info too.
    err = setDynamicInfoExToIPCHalSensor(getOpenId(), ipcDynamicInfo);
    if (CC_UNLIKELY(err != 0)) {
      MY_LOGE("setDynamicInfoExToIPCHalSensor failed, need check.");
    }
  }
#endif

  rawSize[0] = rawSize_l[0];
  rawSize[1] = rawSize_l[1];

  return OK;
}

QInitParam P1NodeImp::prepareQInitParam(
    IHalSensor::ConfigParam* sensorCfg,
    NS3Av3::AEInitExpoSetting_T initExpoSetting,
    std::vector<PortInfo> vPortInfo) {
  sensorCfg->index = (MUINT)getOpenId();
  sensorCfg->crop = mSensorParams.size;
  sensorCfg->scenarioId = mSensorParams.mode;
  sensorCfg->isBypassScenario = 0;
  sensorCfg->isContinuous = 1;
  sensorCfg->HDRMode = MFALSE;
#if (P1NODE_USING_MTK_LDVT > 0)
  sensorCfg->framerate = 1;
#else
  sensorCfg->framerate = mSensorParams.fps;
#endif
  sensorCfg->twopixelOn = 0;
  sensorCfg->debugMode = 0;
  sensorCfg->exposureTime = initExpoSetting.u4Eposuretime;
  sensorCfg->gain = initExpoSetting.u4AfeGain;
  sensorCfg->exposureTime_se = initExpoSetting.u4Eposuretime_se;
  sensorCfg->gain_se = initExpoSetting.u4AfeGain_se;

  std::vector<IHalSensor::ConfigParam> vSensorCfg;
  vSensorCfg.push_back(*sensorCfg);  // only insert once

  //
  MBOOL bDynamicRawType = MTRUE;  // true:[ON] ; false:[OFF]
  QInitParam halCamIOinitParam(0, /*sensor test pattern */
                               vSensorCfg, vPortInfo, bDynamicRawType);
  halCamIOinitParam.m_IQlv = mCfg.mQualityLv;
  // halCamIOinitParam.m_Func.Bits.SensorNum = mCfg.mSensorNum;
  halCamIOinitParam.m_pipelinebitdepth = (E_CAM_PipelineBitDepth_SEL)mPipeBit;
  halCamIOinitParam.m_DynamicTwin = mIsDynamicTwinEn;
  // halCamIOinitParam.m_DropCB = doNotifyDropframe;
  halCamIOinitParam.mSensorFormatOrder = mSensorFormatOrder;
  halCamIOinitParam.m_returnCookie = this;
  // enable frame sync
  if (mEnableFrameSync) {
    MY_LOGI("P1 node(%d) is in synchroized mode", getOpenId());
    halCamIOinitParam.m_bN3D = MTRUE;
  } else {
    halCamIOinitParam.m_bN3D = MFALSE;
  }

  return halCamIOinitParam;
}

MERROR P1NodeImp::lmvInit(MSize sensorSize, MSize rrzoSize) {
  if (mEnableEISO) {
    P1_TIMING_CHECK("P1:LMV-init", 20, TC_W);
    P1_TRACE_S_BEGIN(SLG_S, "P1:LMV-init");
    if (IS_LMV(mpConnectLMV)) {
      MINT32 mode = EIS::EisInfo::getMode(mPackedEisInfo);
      MINT32 factor = EIS::EisInfo::getFactor(mPackedEisInfo);
      MY_LOGD("mpConnectLMV->init+");
      if (MFALSE == mpConnectLMV->init(mode, factor, sensorSize, rrzoSize)) {
        MY_LOGE("ConnectLMV create fail");
        return BAD_VALUE;
      }
    }
    P1_TRACE_C_END(SLG_S);  // "P1:LMV-init"
  }
  return OK;
}

#if SUPPORT_3A
MERROR P1NodeImp::getAEInitExpoSetting(
    NS3Av3::AEInitExpoSetting_T* initExpoSetting) {
  P1_TIMING_CHECK("P1:3A-create-GetAEInitExpoSetting", 10, TC_W);
  P1_TRACE_S_BEGIN(SLG_S, "P1:3A-create-GetAEInitExpoSetting");
  MY_LOGI("MAKE_Hal3A +++");
  MAKE_Hal3A(
      mp3A, [](NS3Av3::IHal3A* p) { p->destroyInstance(LOG_TAG); }, getOpenId(),
      LOG_TAG);
  if (mp3A == nullptr) {
    MY_LOGE("mp3A is NULL");
    return DEAD_OBJECT;
  }
  MY_LOGI("MAKE_Hal3A ---");
  mp3A->send3ACtrl(NS3Av3::E3ACtrl_GetAEInitExpoSetting,
                   (MINTPTR)initExpoSetting, 0);
  MY_LOGI(
      "GetAEInitExpoSetting: u4Eposuretime(le:%d/se:%d) "
      "u4AfeGain(le:%d/se:%d)",
      (*initExpoSetting).u4Eposuretime, (*initExpoSetting).u4Eposuretime_se,
      (*initExpoSetting).u4AfeGain, (*initExpoSetting).u4AfeGain_se);
  P1_TRACE_C_END(SLG_S);  // "P1:3A-create-GetAEInitExpoSetting"
  return OK;
}
#endif

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::generateCtrlQueue(std::vector<NS3Av3::MetaSet_T*>* rQue,
                             P1QueJob* rJob) {
  for (size_t j = 0; j < (*rJob).size(); j++) {
    if ((*rJob).edit(j).ptr() != nullptr) {
      (*rQue).push_back(&((*rJob).edit(j).ptr()->metaSet));
    }
  }
  //
  if (mMetaLogOp > 0 && (*rQue).size() > 0 &&
      (*rQue).size() == (*rJob).size()) {
    MY_LOGI("LogMeta Que[%zu] Job[%zu]", (*rQue).size(), (*rJob).size());
    std::vector<NS3Av3::MetaSet_T*>::iterator it = (*rQue).begin();
    for (size_t i = 0; i < (*rJob).size() && it != (*rQue).end(); i++, it++) {
      P1Act pAct = GET_ACT_PTR(pAct, (*rJob).edit((MUINT8)i), RET_VOID);
      P1_LOG_META(*pAct, &((*it)->appMeta), "3A.Set-APP");
      P1_LOG_META(*pAct, &((*it)->halMeta), "3A.Set-HAL");
    }
  }
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::createAction(P1QueAct* rAct,
                        std::shared_ptr<IPipelineFrame> appFrame,
                        REQ_TYPE eType) {
  P1Act act = GET_ACT_PTR(act, *rAct, RET_VOID);
  // create queue act
  // MUINT32 newNum = get_and_increase_magicnum();
  NS3Av3::MetaSet_T* metaInfo = &(act->metaSet);
  IMetadata* pAppMeta = &(act->metaSet.appMeta);
  IMetadata* pHalMeta = &(act->metaSet.halMeta);
  if (pAppMeta == nullptr) {
    MY_LOGE("pAppMeta == NULL");
  }
  act->metaSet.PreSetKey = rAct->id();
  //
  MINT32 meta_raw_type = (MINT32)mRawDefType;
  MBOOL meta_raw_exist = MFALSE;
  MBOOL meta_zsl_req = MFALSE;
  //
  P1_TRACE_F_BEGIN(SLG_I, "P1:create|Fnum:%d Rnum:%d", P1GET_FRM_NUM(appFrame),
                   P1GET_REQ_NUM(appFrame));
  MINT32 meta_ZslEn = P1_META_GENERAL_EMPTY_INT;
  MINT32 meta_CapIntent = P1_META_GENERAL_EMPTY_INT;
  MINT32 meta_RawType = P1_META_GENERAL_EMPTY_INT;
  MINT32 meta_TgNum = P1_META_GENERAL_EMPTY_INT;
  MINT32 meta_QualityCtrl = P1_META_GENERAL_EMPTY_INT;
  MINT32 meta_FmtImgo = P1_META_GENERAL_EMPTY_INT;  // eImgFmt_UNKNOWN
  MINT32 meta_FmtRrzo = P1_META_GENERAL_EMPTY_INT;  // eImgFmt_UNKNOWN
  //
  if (appFrame != nullptr) {
    if (CC_UNLIKELY(eType != REQ_TYPE_UNKNOWN)) {
      MY_LOGE("Type-Mismatching (%d) on (%d, %d)", eType,
              P1GET_FRM_NUM(appFrame), P1GET_REQ_NUM(appFrame));
      return;
    }
    if (act->appFrame != appFrame) {  // act->appFrame == NULL
      act->appFrame = appFrame;
      act->frmNum = appFrame->getFrameNo();
      act->reqNum = appFrame->getRequestNo();
      act->mapFrameStream();
      MY_LOGI("CreateAct(%d,%d) assign frame", act->frmNum, act->reqNum);
    }
    //
    P1_TRACE_S_BEGIN(SLG_O, "createMeta");
    if (mvStreamMeta[STREAM_META_IN_APP] != nullptr) {
      if (OK == act->frameMetadataGet(STREAM_META_IN_APP, pAppMeta)) {
        P1_LOG_META(*act, pAppMeta, "RequestIn-APP");
      } else {
        MY_LOGI("can not lock the app metadata");
        pAppMeta = nullptr;
      }
    }
    if (mvStreamMeta[STREAM_META_IN_HAL] != nullptr) {
      if (OK == act->frameMetadataGet(STREAM_META_IN_HAL, pHalMeta)) {
        P1_LOG_META(*act, pHalMeta, "RequestIn-HAL");
      } else {
        MY_LOGI("can not lock the hal metadata");
        pHalMeta = nullptr;
      }
    }
    P1_TRACE_C_END(SLG_O);  // "createMeta"
    // check info from APP meta
    if (pAppMeta != nullptr) {
      MUINT8 zsl_en = MTK_CONTROL_ENABLE_ZSL_FALSE;
      if (tryGetMetadata<MUINT8>(pAppMeta, MTK_CONTROL_ENABLE_ZSL, &zsl_en)) {
        meta_ZslEn = zsl_en;
      }
      MINT32 patternMode = 0;
      if (tryGetMetadata<MINT32>(pAppMeta, MTK_SENSOR_TEST_PATTERN_MODE,
                                 &patternMode)) {
        mMeta_PatMode = patternMode;
        MY_LOGD("p1 createAction pattern mode %d", mMeta_PatMode);
      }
      MUINT8 cap_intent = MTK_CONTROL_CAPTURE_INTENT_CUSTOM;
      if (tryGetMetadata<MUINT8>(pAppMeta, MTK_CONTROL_CAPTURE_INTENT,
                                 &cap_intent)) {
        meta_CapIntent = cap_intent;
      }
      if (zsl_en == MTK_CONTROL_ENABLE_ZSL_TRUE &&
          cap_intent == MTK_CONTROL_CAPTURE_INTENT_STILL_CAPTURE
#if 1  // check-bypass-request
          && appFrame->IsReprocessFrame()
#endif
      ) {
        meta_zsl_req = MTRUE;
      }
    }
    // check info from HAL meta
    if (pHalMeta != nullptr) {
      MINT32 raw_type = meta_raw_type;
      if (tryGetMetadata<MINT32>(pHalMeta, MTK_P1NODE_RAW_TYPE, &raw_type)) {
        meta_RawType = raw_type;
        MY_LOGD("raw type set from outside %d", raw_type);
        if (meta_raw_type != raw_type) {
          MY_LOGI("Metadata-Raw(%d) - Config-Raw(%d)(%d-0x%x)", raw_type,
                  meta_raw_type, mRawDefType, mRawOption);
        }
        if ((mRawOption & (MUINT32)(1 << raw_type)) > 0) {
          meta_raw_type = raw_type;
          meta_raw_exist = MTRUE;
        } else {
          MY_LOGI(
              "raw type (%d) set from outside, but not accept "
              "RawOption(0x%x)",
              raw_type, mRawOption);
        }
      }
      if (IS_LMV(mpConnectLMV) && mpConnectLMV->checkSwitchOut(pHalMeta)) {
        act->uniSwitchState = UNI_SWITCH_STATE_REQ;
      }
      // for Twin Switch TG Number control
      if (mIsDynamicTwinEn) {
        MINT32 tg_num = MTK_P1_TWIN_SWITCH_NONE;
        if (tryGetMetadata<MINT32>(pHalMeta, MTK_P1NODE_TWIN_SWITCH, &tg_num)) {
          meta_TgNum = tg_num;
          if (tg_num != MTK_P1_TWIN_SWITCH_NONE) {
            act->tgSwitchState = TG_SWITCH_STATE_REQ;
            switch (tg_num) {
              case MTK_P1_TWIN_SWITCH_ONE_TG:
                act->tgSwitchNum = 1;
                break;
              case MTK_P1_TWIN_SWITCH_TWO_TG:
                act->tgSwitchNum = 2;
                break;
              default:
                MY_LOGI("check MTK_P1NODE_TWIN_SWITCH %d", tg_num);
                break;
            }
          }
        }
      }
      // for Standby Mode control
      if (mpHwStateCtrl != nullptr) {
        act->ctrlSensorStatus = mpHwStateCtrl->checkReceiveFrame(pHalMeta);
      }
      // for Quality Switch control
      if (mpRegisterNotify != nullptr) {
        MINT32 quality_ctrl = MTK_P1_RESIZE_QUALITY_SWITCH_NONE;
        act->qualitySwitchState = QUALITY_SWITCH_STATE_NONE;
        if (tryGetMetadata<MINT32>(pHalMeta, MTK_P1NODE_RESIZE_QUALITY_SWITCH,
                                   &quality_ctrl)) {
          meta_QualityCtrl = quality_ctrl;
          if (getQualitySwitching() &&
              quality_ctrl != MTK_P1_RESIZE_QUALITY_SWITCH_NONE) {
            act->qualitySwitchState = QUALITY_SWITCH_STATE_DONE_ILLEGAL;
          } else {
            switch (quality_ctrl) {
              case MTK_P1_RESIZE_QUALITY_SWITCH_H_H:
                act->qualitySwitchState = QUALITY_SWITCH_STATE_REQ_H_H;
                break;
              case MTK_P1_RESIZE_QUALITY_SWITCH_H_L:
                act->qualitySwitchState = QUALITY_SWITCH_STATE_REQ_H_L;
                break;
              case MTK_P1_RESIZE_QUALITY_SWITCH_L_H:
                act->qualitySwitchState = QUALITY_SWITCH_STATE_REQ_L_H;
                break;
              case MTK_P1_RESIZE_QUALITY_SWITCH_L_L:
                act->qualitySwitchState = QUALITY_SWITCH_STATE_REQ_L_L;
                break;
              default:
                break;
            }
          }
        }
        if (act->qualitySwitchState != QUALITY_SWITCH_STATE_NONE) {
          E_CamIQLevel CamLvA = eCamIQ_MAX;
          E_CamIQLevel CamLvB = eCamIQ_MAX;
          if (mpCamIO != nullptr &&
              mpCamIO->sendCommand(ENPipeCmd_GET_QUALITY, (MINTPTR)NULL,
                                   (MINTPTR)&CamLvA, (MINTPTR)&CamLvB)) {
            MBOOL ignore = MFALSE;
            switch (quality_ctrl) {
              case MTK_P1_RESIZE_QUALITY_SWITCH_H_H:
                if (CamLvA == eCamIQ_H && CamLvB == eCamIQ_H) {
                  ignore = MTRUE;
                }
                break;
              case MTK_P1_RESIZE_QUALITY_SWITCH_H_L:
                if (CamLvA == eCamIQ_H && CamLvB == eCamIQ_L) {
                  ignore = MTRUE;
                }
                break;
              case MTK_P1_RESIZE_QUALITY_SWITCH_L_H:
                if (CamLvA == eCamIQ_L && CamLvB == eCamIQ_H) {
                  ignore = MTRUE;
                }
                break;
              case MTK_P1_RESIZE_QUALITY_SWITCH_L_L:
                if (CamLvA == eCamIQ_L && CamLvB == eCamIQ_L) {
                  ignore = MTRUE;
                }
                break;
              default:
                break;
            }
            if (ignore) {
              act->qualitySwitchState = QUALITY_SWITCH_STATE_DONE_IGNORE;
            }
          }
        }
        if ((act->qualitySwitchState & QUALITY_SWITCH_STATE_REQ_NON) > 0) {
          setQualitySwitching(MTRUE);
        }
      }

      if (tryGetMetadata<MINT32>(pHalMeta, MTK_HAL_REQUEST_IMG_IMGO_FORMAT,
                                 &(act->mReqFmt_Imgo))) {
        meta_FmtImgo = act->mReqFmt_Imgo;
        MY_LOGI("MTK_REQUEST_IMG_IMGO_FORMAT : 0x%x", act->mReqFmt_Imgo);
      }

      if (tryGetMetadata<MINT32>(pHalMeta, MTK_HAL_REQUEST_IMG_RRZO_FORMAT,
                                 &(act->mReqFmt_Rrzo))) {
        meta_FmtRrzo = act->mReqFmt_Rrzo;
        MY_LOGI("MTK_REQUEST_IMG_RRZO_FORMAT : 0x%x", act->mReqFmt_Rrzo);
      }
    }
    //
    if (meta_zsl_req) {
      act->reqType = REQ_TYPE_ZSL;
    } else if (act->streamBufImg[STREAM_IMG_IN_YUV].bExist) {
      act->reqType = REQ_TYPE_YUV;
    } else if (act->streamBufImg[STREAM_IMG_IN_OPAQUE].bExist) {
      act->reqType = REQ_TYPE_REDO;
    } else {
      act->reqType = REQ_TYPE_NORMAL;
      if (IS_PORT(CONFIG_PORT_IMGO, mConfigPort) &&
          act->streamBufImg[STREAM_IMG_OUT_OPAQUE].bExist) {
        act->reqOutSet |= REQ_SET(REQ_OUT_FULL_OPAQUE);
      }
      if (IS_PORT(CONFIG_PORT_IMGO, mConfigPort) &&
          act->streamBufImg[STREAM_IMG_OUT_FULL].bExist) {
        if (meta_raw_type == EPipe_PROCESSED_RAW) {
          act->reqOutSet |= REQ_SET(REQ_OUT_FULL_PROC);
        } else {
          act->reqOutSet |= REQ_SET(REQ_OUT_FULL_PURE);
        }
      }
      if (IS_PORT(CONFIG_PORT_RRZO, mConfigPort) &&
          act->streamBufImg[STREAM_IMG_OUT_RESIZE].bExist) {
        act->reqOutSet |= REQ_SET(REQ_OUT_RESIZER);
      }
      if (IS_PORT(CONFIG_PORT_LCSO, mConfigPort) &&
          act->streamBufImg[STREAM_IMG_OUT_LCS].bExist) {
        act->reqOutSet |= REQ_SET(REQ_OUT_LCSO);
      }
      if (IS_PORT(CONFIG_PORT_RSSO, mConfigPort) &&
          act->streamBufImg[STREAM_IMG_OUT_RSS].bExist) {
        act->reqOutSet |= REQ_SET(REQ_OUT_RSSO);
      }
    }
  } else {
    switch (eType) {
      case REQ_TYPE_INITIAL:
      case REQ_TYPE_PADDING:
      case REQ_TYPE_DUMMY:
        act->reqType = eType;
        break;
      default:  // REQ_TYPE_UNKNOWN/REQ_TYPE_NORMAL/REQ_TYPE_REDO/REQ_TYPE_YUV
        MY_LOGE("Type-Mismatching (%d)", eType);
        return;
    }
    pAppMeta = nullptr;
    pHalMeta = nullptr;
    if (act->reqType ==
        REQ_TYPE_INITIAL) {  // using pool buffer only in initial act
      if (IS_PORT(CONFIG_PORT_IMGO, mConfigPort) &&
          mpStreamPool_full != nullptr) {
        if (meta_raw_type == EPipe_PROCESSED_RAW) {
          act->reqOutSet |= REQ_SET(REQ_OUT_FULL_PROC);
        } else {
          act->reqOutSet |= REQ_SET(REQ_OUT_FULL_PURE);
        }
      }
      if (IS_PORT(CONFIG_PORT_RRZO, mConfigPort) &&
          mpStreamPool_resizer != nullptr) {
        act->reqOutSet |= REQ_SET(REQ_OUT_RESIZER);
      }
      if (IS_PORT(CONFIG_PORT_LCSO, mConfigPort) &&
          mpStreamPool_lcso != nullptr) {
        act->reqOutSet |= REQ_SET(REQ_OUT_LCSO);
      }
      if (IS_PORT(CONFIG_PORT_RSSO, mConfigPort) &&
          mpStreamPool_rsso != nullptr) {
        act->reqOutSet |= REQ_SET(REQ_OUT_RSSO);
      }
    }
  }
  //
  act->fullRawType = meta_raw_type;
  if (act->reqType == REQ_TYPE_NORMAL) {
#if 1  // add raw type to hal meta
    if (act->reqType == REQ_TYPE_NORMAL && !meta_raw_exist) {
      IMetadata::IEntry entryRawType(MTK_P1NODE_RAW_TYPE);
      entryRawType.push_back(meta_raw_type, Type2Type<MINT32>());
      metaInfo->halMeta.update(MTK_P1NODE_RAW_TYPE, entryRawType);
    }
#endif
  }
  //
  if (act->reqType == REQ_TYPE_NORMAL || act->reqType == REQ_TYPE_INITIAL ||
      act->reqType == REQ_TYPE_PADDING || act->reqType == REQ_TYPE_DUMMY) {
    if (IS_PORT(CONFIG_PORT_IMGO, mConfigPort) &&
        (0 == (IS_OUT(REQ_OUT_FULL_PROC, act->reqOutSet) ||
               IS_OUT(REQ_OUT_FULL_PURE, act->reqOutSet) ||
               IS_OUT(REQ_OUT_FULL_OPAQUE, act->reqOutSet)))) {
      act->reqOutSet |= REQ_SET(REQ_OUT_FULL_STUFF);
    }
    if (IS_PORT(CONFIG_PORT_RRZO, mConfigPort) &&
        (0 == IS_OUT(REQ_OUT_RESIZER, act->reqOutSet))) {
      act->reqOutSet |= REQ_SET(REQ_OUT_RESIZER_STUFF);
    }
    if (IS_PORT(CONFIG_PORT_LCSO, mConfigPort) &&
        (0 == IS_OUT(REQ_OUT_LCSO, act->reqOutSet))) {
      act->reqOutSet |= REQ_SET(REQ_OUT_LCSO_STUFF);
    }
    if (IS_PORT(CONFIG_PORT_RSSO, mConfigPort) &&
        (0 == IS_OUT(REQ_OUT_RSSO, act->reqOutSet))) {
      act->reqOutSet |= REQ_SET(REQ_OUT_RSSO_STUFF);
    }
    //
    prepareCropInfo(rAct, pAppMeta, pHalMeta,
                    PREPARE_CROP_PHASE_RECEIVE_CREATE);
    act->exeState = EXE_STATE_REQUESTED;
  } else if (act->reqType == REQ_TYPE_REDO || act->reqType == REQ_TYPE_YUV ||
             act->reqType == REQ_TYPE_ZSL) {
    act->exeState = EXE_STATE_DONE;
  }
  //
  // mTagReq.set(rAct.id()); // set number while act register
  //
  if (1 <= mLogLevelI) {  // P1_LOGI(1)
    std::string info(act->msg);
    act->msg.clear();
    act->msg += base::StringPrintf(
        "[P1::REQ]" P1INFO_ACT_STR
        " [META ze:%d ci:%d rt:%d tn:%d qc:%d fi:%d fr:%d] [%s][%d] ",
        P1INFO_ACT_VAR(*act), meta_ZslEn, meta_CapIntent, meta_RawType,
        meta_TgNum, meta_QualityCtrl, meta_FmtImgo, meta_FmtRrzo,
        (appFrame != nullptr) ? "New-Request" : "New-Dummy", eType);
    act->msg += info;
    if ((eType != REQ_TYPE_UNKNOWN) ||  // if the eType is assigned, it should
                                        // be an internal act,
        (EN_INIT_REQ_RUN &&
         mInitReqCnt < mInitReqNum)) {  // print the string since no more
                                        // message appending
      P1_TRACE_F_BEGIN(SLG_PFL,
                       "P1::REQ_LOG|Mnum:%d SofIdx:%d Fnum:%d "
                       "Rnum:%d FlushSet:0x%x",
                       act->magicNum, act->sofIdx, act->frmNum, act->reqNum,
                       act->flushSet);
      P1_LOGI(1, "%s", act->msg.c_str());
      P1_TRACE_C_END(SLG_PFL);  // "P1::REQ_LOG"
    }
  }
  //
  P1_TRACE_C_END(SLG_I);  // "P1:create"
  //
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::onProcessResult(P1QueAct* rAct,
                           QBufInfo const& deqBuf,
                           NS3Av3::MetaSet_T const& result3A,
                           IMetadata const& resultAppend,
                           MUINT32 const index) {
  FUNCTION_IN;
  //
  P1Act act = GET_ACT_PTR(act, *rAct, RET_VOID);
  //
  P1_TRACE_F_BEGIN(SLG_I, "P1:result|Mnum:%d SofIdx:%d Fnum:%d Rnum:%d",
                   act->magicNum, act->sofIdx, act->frmNum, act->reqNum);
  //
  if (act->appFrame != 0) {
    if ((mvStreamMeta[STREAM_META_OUT_APP] != nullptr) &&
        (mvStreamMeta[STREAM_META_OUT_HAL] != nullptr)) {
      // APP out Meta Stream
      IMetadata outAppMetadata;
      generateAppMeta(rAct, result3A, deqBuf, &outAppMetadata, index);
      if ((IS_OUT(REQ_OUT_FULL_OPAQUE, act->reqOutSet)) &&
          (act->streamBufImg[STREAM_IMG_OUT_OPAQUE].spImgBuf != nullptr) &&
          (!IS_EXP(EXP_EVT_NOBUF_IMGO, act->expRec))) {
        // app metadata index
        IMetadata appTagIndex;
        generateAppTagIndex(&outAppMetadata, &appTagIndex);
        std::shared_ptr<IImageBufferHeap> pImageBufferHeap =
            act->streamBufImg[STREAM_IMG_OUT_OPAQUE]
                .spImgBuf->getImageBufferHeap();
        MERROR status = OpaqueReprocUtil::setAppMetadataToHeap(pImageBufferHeap,
                                                               &appTagIndex);
        MY_LOGD("setAppMetadataToHeap (%d)", status);
      }
      //
      // HAL out Meta Stream
      IMetadata inHalMetadata;
      IMetadata outHalMetadata;
      if (OK != act->frameMetadataGet(STREAM_META_IN_HAL, &inHalMetadata)) {
        MY_LOGW("cannot get in-hal-metadata");
      }
      generateHalMeta(rAct, result3A, deqBuf, resultAppend, inHalMetadata,
                      &outHalMetadata, index);
      if ((IS_OUT(REQ_OUT_FULL_OPAQUE, act->reqOutSet)) &&
          (act->streamBufImg[STREAM_IMG_OUT_OPAQUE].spImgBuf != nullptr) &&
          (!IS_EXP(EXP_EVT_NOBUF_IMGO, act->expRec))) {
        std::shared_ptr<IImageBufferHeap> pImageBufferHeap =
            act->streamBufImg[STREAM_IMG_OUT_OPAQUE]
                .spImgBuf->getImageBufferHeap();
        MERROR status = OpaqueReprocUtil::setHalMetadataToHeap(pImageBufferHeap,
                                                               &outHalMetadata);
        MY_LOGD("setHalMetadataToHeap (%d)", status);
        if ((IS_OUT(REQ_OUT_LCSO, act->reqOutSet)) &&
            (act->streamBufImg[STREAM_IMG_OUT_LCS].spImgBuf != nullptr)) {
          MERROR status = OpaqueReprocUtil::setLcsoImageToHeap(
              pImageBufferHeap, act->streamBufImg[STREAM_IMG_OUT_LCS].spImgBuf);
          MY_LOGD("setLcsoImageToHeap (%d)", status);
        }
      }
      //
      MBOOL isChange = MFALSE;
      attemptCtrlReadout(rAct, &outAppMetadata, &outHalMetadata, &isChange);
      //
      if (mspSyncHelper != nullptr) {
        IMetadata ctrlMeta;
        act->frameMetadataGet(STREAM_META_IN_HAL, &ctrlMeta);
        MBOOL res = mspSyncHelper->syncResultCheck(getOpenId(), &ctrlMeta,
                                                   &outHalMetadata);
        if (!res) {
          act->setFlush(FLUSH_MIS_SYNC);
          MY_LOGI("SyncHelper flush this request (%d)" P1INFO_ACT_STR, res,
                  P1INFO_ACT_VAR(*act));
        }
      }
      //
      if (OK != act->frameMetadataGet(STREAM_META_OUT_APP, nullptr, MTRUE,
                                      &outAppMetadata)) {
        MY_LOGW("cannot write out-app-metadata");
      } else {
        P1_LOG_META(*act, &outAppMetadata, "ResultOut-APP");
      }
      if (OK != act->frameMetadataGet(STREAM_META_OUT_HAL, nullptr, MTRUE,
                                      &outHalMetadata)) {
        MY_LOGW("cannot write out-hal-metadata");
      } else {
        P1_LOG_META(*act, &outHalMetadata, "ResultOut-HAL");
      }
    } else {
      MY_LOGW("STREAM_META_OUT not exist - APP(%d) HAL(%d)",
              (mvStreamMeta[STREAM_META_OUT_APP] != nullptr) ? MTRUE : MFALSE,
              (mvStreamMeta[STREAM_META_OUT_HAL] != nullptr) ? MTRUE : MFALSE);
    }
    //
    checkBufferDumping(rAct);
  }
  //
#if 1  // trigger only at the end of this job
  onReturnFrame(
      rAct, FLUSH_NONEED,
      (IS_BURST_OFF || (index == (MUINT32)(mBurstNum - 1))) ? MTRUE : MFALSE);
  /* DO NOT use this P1QueAct after onReturnFrame() */
#else
  onReturnFrame(rAct, FLUSH_NONEED, MTRUE);
  /* DO NOT use this P1QueAct after onReturnFrame() */
#endif
  //
  P1_TRACE_C_END(SLG_I);  // "P1:result"
  //
  FUNCTION_OUT;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::processRedoFrame(P1QueAct* rAct) {
  FUNCTION_IN;
  //
  P1Act act = GET_ACT_PTR(act, *rAct, RET_VOID);
  //
  if (act->getFlush()) {
    MY_LOGD("need to flush, skip frame processing");
    return;
  }
  IMetadata appMeta;
  IMetadata halMeta;
  std::shared_ptr<IImageBuffer> imgBuf;
  std::shared_ptr<IImageBuffer> imgBuf_lcso;
  //
  if (OK != act->frameImageGet(STREAM_IMG_IN_OPAQUE, &imgBuf) ||
      OK != act->frameImageGet(STREAM_IMG_OUT_LCS, &imgBuf_lcso)) {
    MY_LOGE("Can not get in-opaque/lcso buffer from frame");
  } else {
    std::shared_ptr<IImageBufferHeap> pHeap = imgBuf->getImageBufferHeap();
    IMetadata appMetaTagIndex;
    if (OK ==
        OpaqueReprocUtil::getAppMetadataFromHeap(pHeap, &appMetaTagIndex)) {
      // get the input of app metadata
      IMetadata metaInApp;
      if (OK != act->frameMetadataGet(STREAM_META_IN_APP, &metaInApp)) {
        MY_LOGW("cannot get in-app-metadata");
      }
      // get p1node's tags from opaque buffer
      IMetadata::IEntry entryTagIndex =
          appMetaTagIndex.entryFor(MTK_P1NODE_METADATA_TAG_INDEX);
      for (MUINT i = 0; i < entryTagIndex.count(); i++) {
        MUINT32 tag = entryTagIndex.itemAt(
            i, Type2Type<MINT32>());  // get Tag from entryTagIndex
        // update entry from metaInApp to appMeta
        IMetadata::IEntry entryInApp = metaInApp.entryFor(tag);
        appMeta.update(tag, entryInApp);
      }
      // Workaround: do not return the duplicated key for YUV reprocessing
      appMeta.remove(MTK_JPEG_THUMBNAIL_SIZE);
      appMeta.remove(MTK_JPEG_ORIENTATION);
      if (OK != act->frameMetadataGet(STREAM_META_OUT_APP, nullptr, MTRUE,
                                      &appMeta)) {
        MY_LOGW("cannot write out-app-metadata");
      }
    } else {
      MY_LOGW("Can not get app meta from in-opaque buffer");
    }
    if (OK == OpaqueReprocUtil::getHalMetadataFromHeap(pHeap, &halMeta)) {
      IMetadata::IEntry entry(MTK_HAL_REQUEST_REQUIRE_EXIF);
      entry.push_back(1, Type2Type<MUINT8>());
      halMeta.update(entry.tag(), entry);
      if (OK != act->frameMetadataGet(STREAM_META_OUT_HAL, nullptr, MTRUE,
                                      &halMeta)) {
        MY_LOGW("cannot write out-hal-metadata");
      }
    } else {
      MY_LOGW("Can not get hal meta from in-opaque buffer");
    }
    if (OK == OpaqueReprocUtil::getLcsoImageFromHeap(pHeap, imgBuf_lcso)) {
      act->frameImagePut(STREAM_IMG_OUT_LCS);
    } else {
      MY_LOGW("Can not get lcso image from in-opaque buffer");
    }
  }
  //
  FUNCTION_OUT;
  //
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::processYuvFrame(P1QueAct* rAct) {
  FUNCTION_IN;
  //
  P1Act act = GET_ACT_PTR(act, *rAct, RET_VOID);
  //
  if (act->getFlush()) {
    MY_LOGD("need to flush, skip frame processing");
    return;
  }
  IMetadata inAppMetadata;
  IMetadata outAppMetadata;
  IMetadata inHalMetadata;
  IMetadata outHalMetadata;
  MINT64 timestamp = 0;
  MFLOAT aperture = (0.0f);
  MFLOAT focallength = (0.0f);
  MINT64 exposure = 0;
  MINT32 iso = 0;
  MINT32 iso_boost = 0;
  MINT64 duration = 0;
  MUINT8 edge = MTK_EDGE_MODE_OFF;
  MUINT8 noise = MTK_NOISE_REDUCTION_MODE_OFF;
  MFLOAT factor = (1.0f);
  // APP in Meta Stream
  if (OK != act->frameMetadataGet(STREAM_META_IN_APP, &inAppMetadata)) {
    MY_LOGW("cannot get in-app-metadata");
  } else {
    // outAppMetadata = inAppMetadata; // copy all from in-app to out-app
    if (tryGetMetadata<MINT64>(&inAppMetadata, MTK_SENSOR_TIMESTAMP,
                               &timestamp)) {
      MY_LOGD("timestamp from in-app %" PRId64, timestamp);
    } else {
      MY_LOGI("cannot find timestamp from in-app");
      timestamp = 0;
    }
    //
    if (tryGetMetadata<MFLOAT>(&inAppMetadata, MTK_LENS_APERTURE, &aperture)) {
      MY_LOGD1("aperture from in-app %f", aperture);
      if (!trySetMetadata<MFLOAT>(&outAppMetadata, MTK_LENS_APERTURE,
                                  aperture)) {
        MY_LOGW("cannot update MTK_LENS_APERTURE");
      }
    } else {
      MY_LOGI("cannot find aperture from in-app");
      aperture = (0.0f);
    }
    if (tryGetMetadata<MFLOAT>(&inAppMetadata, MTK_LENS_FOCAL_LENGTH,
                               &focallength)) {
      MY_LOGD1("focallength from in-app %f", focallength);
      if (!trySetMetadata<MFLOAT>(&outAppMetadata, MTK_LENS_FOCAL_LENGTH,
                                  focallength)) {
        MY_LOGW("cannot update MTK_LENS_FOCAL_LENGTH");
      }
    } else {
      MY_LOGI("cannot find focallength from in-app");
      focallength = (0.0f);
    }
    if (tryGetMetadata<MINT64>(&inAppMetadata, MTK_SENSOR_EXPOSURE_TIME,
                               &exposure)) {
      MY_LOGD1("exposure from in-app %" PRId64, exposure);
      if (!trySetMetadata<MINT64>(&outAppMetadata, MTK_SENSOR_EXPOSURE_TIME,
                                  exposure)) {
        MY_LOGW("cannot update MTK_SENSOR_EXPOSURE_TIME");
      }
    } else {
      MY_LOGI("cannot find exposure from in-app");
      exposure = 0;
    }
    if (tryGetMetadata<MINT32>(&inAppMetadata, MTK_SENSOR_SENSITIVITY, &iso)) {
      MY_LOGD1("iso from in-app %" PRId32, iso);
      if (!trySetMetadata<MINT32>(&outAppMetadata, MTK_SENSOR_SENSITIVITY,
                                  iso)) {
        MY_LOGW("cannot update MTK_SENSOR_SENSITIVITY");
      }
    } else {
      MY_LOGI("cannot find iso from in-app");
      iso = 0;
    }
    if (tryGetMetadata<MINT32>(&inAppMetadata,
                               MTK_CONTROL_POST_RAW_SENSITIVITY_BOOST,
                               &iso_boost)) {
      MY_LOGD1("iso boost from in-app %" PRId32, iso_boost);
      if (!trySetMetadata<MINT32>(&outAppMetadata,
                                  MTK_CONTROL_POST_RAW_SENSITIVITY_BOOST,
                                  iso_boost)) {
        MY_LOGW("cannot update MTK_CONTROL_POST_RAW_SENSITIVITY_BOOST");
      }
    } else {
      MY_LOGI("cannot find iso boost from in-app");
      iso_boost = 0;
    }
    if (tryGetMetadata<MINT64>(&inAppMetadata, MTK_SENSOR_FRAME_DURATION,
                               &duration)) {
      MY_LOGD1("duration from in-app %" PRId64, duration);
      if (!trySetMetadata<MINT64>(&outAppMetadata, MTK_SENSOR_FRAME_DURATION,
                                  duration)) {
        MY_LOGW("cannot update MTK_SENSOR_FRAME_DURATION");
      }
    } else {
      MY_LOGI("cannot find duration from in-app");
      duration = 0;
    }

    //
    if (tryGetMetadata<MUINT8>(&inAppMetadata, MTK_EDGE_MODE, &edge)) {
      MY_LOGD1("MTK_EDGE_MODE from in-app %d", edge);
      if (!trySetMetadata<MUINT8>(&outAppMetadata, MTK_EDGE_MODE, edge)) {
        MY_LOGW("cannot update MTK_EDGE_MODE");
      }
    } else {
      MY_LOGI("cannot find MTK_EDGE_MODE from in-app");
      edge = MTK_EDGE_MODE_OFF;
    }
    if (tryGetMetadata<MUINT8>(&inAppMetadata, MTK_NOISE_REDUCTION_MODE,
                               &noise)) {
      MY_LOGD1("MTK_NOISE_REDUCTION_MODE from in-app %d", noise);
      if (!trySetMetadata<MUINT8>(&outAppMetadata, MTK_NOISE_REDUCTION_MODE,
                                  noise)) {
        MY_LOGW("cannot update MTK_NOISE_REDUCTION_MODE");
      }
    } else {
      MY_LOGI("cannot find MTK_NOISE_REDUCTION_MODE from in-app");
      noise = MTK_NOISE_REDUCTION_MODE_OFF;
    }
    if (tryGetMetadata<MFLOAT>(
            &inAppMetadata, MTK_REPROCESS_EFFECTIVE_EXPOSURE_FACTOR, &factor)) {
      MY_LOGD1("MTK_REPROCESS_EFFECTIVE_EXPOSURE_FACTOR from in-app %f",
               factor);
      if (!trySetMetadata<MFLOAT>(&outAppMetadata,
                                  MTK_REPROCESS_EFFECTIVE_EXPOSURE_FACTOR,
                                  factor)) {
        MY_LOGW("cannot update MTK_REPROCESS_EFFECTIVE_EXPOSURE_FACTOR");
      }
    } else {
      MY_LOGI(
          "cannot find MTK_REPROCESS_EFFECTIVE_EXPOSURE_FACTOR from in-app");
      factor = (1.0f);
    }
  }
  // APP out Meta Stream
  if (!trySetMetadata<MINT64>(  // always set sensor-timestamp
          &outAppMetadata, MTK_SENSOR_TIMESTAMP, timestamp)) {
    MY_LOGW("cannot update MTK_SENSOR_TIMESTAMP");
  }
  //
  if (OK != act->frameMetadataGet(STREAM_META_OUT_APP, NULL, MTRUE,
                                  &outAppMetadata)) {
    MY_LOGW("cannot write out-app-metadata");
  }
  // HAL in/out Meta Stream
  if (OK != act->frameMetadataGet(STREAM_META_IN_HAL, &inHalMetadata)) {
    MY_LOGW("cannot get in-hal-metadata");
  } else {
    outHalMetadata = inHalMetadata;
    if (!trySetMetadata<MINT32>(&outHalMetadata, MTK_P1NODE_SENSOR_MODE,
                                mSensorParams.mode)) {
      MY_LOGW("cannot update MTK_P1NODE_SENSOR_MODE");
    }
    if (!trySetMetadata<MINT32>(&outHalMetadata, MTK_P1NODE_SENSOR_VHDR_MODE,
                                mSensorParams.vhdrMode)) {
      MY_LOGW("cannot update MTK_P1NODE_SENSOR_MODE");
    }
    if (!trySetMetadata<MRect>(
            &outHalMetadata, MTK_P1NODE_SCALAR_CROP_REGION,
            MRect(mSensorParams.size.w, mSensorParams.size.h))) {
      MY_LOGW("cannot update MTK_P1NODE_SCALAR_CROP_REGION");
    }
    if (!trySetMetadata<MRect>(
            &outHalMetadata, MTK_P1NODE_DMA_CROP_REGION,
            MRect(mSensorParams.size.w, mSensorParams.size.h))) {
      MY_LOGW("cannot update MTK_P1NODE_DMA_CROP_REGION");
    }
    if (!trySetMetadata<MSize>(&outHalMetadata, MTK_P1NODE_RESIZER_SIZE,
                               mSensorParams.size)) {
      MY_LOGW("cannot update MTK_P1NODE_RESIZER_SIZE");
    }
    if (OK != act->frameMetadataGet(STREAM_META_OUT_HAL, NULL, MTRUE,
                                    &outHalMetadata)) {
      MY_LOGW("cannot write out-hal-metadata");
    }
  }
  //
  FUNCTION_OUT;
  //
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::releaseAction(P1QueAct* rAct) {
  FUNCTION_IN;
  //
  P1Act act = GET_ACT_PTR(act, *rAct, RET_VOID);
  //
  P1_TRACE_F_BEGIN(SLG_I,
                   "P1:release|Mnum:%d SofIdx:%d Fnum:%d Rnum:%d "
                   "FlushSet:0x%x",
                   act->magicNum, act->sofIdx, act->frmNum, act->reqNum,
                   act->flushSet);
  //
  MY_LOGD(P1INFO_ACT_STR " begin", P1INFO_ACT_VAR(*act));
  //
  if (!act->getFlush()) {
    if (act->reqType == REQ_TYPE_REDO) {
      processRedoFrame(rAct);
    } else if (act->reqType == REQ_TYPE_YUV) {
      processYuvFrame(rAct);
    }
  }
  //
  for (int stream = STREAM_ITEM_START; stream < STREAM_META_NUM; stream++) {
    if (act->streamBufMeta[stream].bExist) {
      if (OK != act->frameMetadataPut((STREAM_META)stream)) {
        MY_LOGD("cannot put metadata stream(%d)", stream);
      }
    }
  }
  //
  for (int stream = STREAM_ITEM_START; stream < STREAM_IMG_NUM; stream++) {
    if ((!act->streamBufImg[stream].bExist) &&  // for INITIAL act
        (act->streamBufImg[stream].eSrcType == IMG_BUF_SRC_NULL)) {
      continue;  // this stream is not existent and no pool/stuff buffer
    }
    switch (act->streamBufImg[stream].eSrcType) {
      case IMG_BUF_SRC_STUFF:
        if (OK != act->stuffImagePut((STREAM_IMG)stream)) {
          MY_LOGD("cannot put stuff image stream(%d)", stream);
        }
        break;
      case IMG_BUF_SRC_POOL:
        if (OK != act->poolImagePut((STREAM_IMG)stream)) {
          MY_LOGD("cannot put pool image stream(%d)", stream);
        }
        break;
      case IMG_BUF_SRC_FRAME:
      case IMG_BUF_SRC_NULL:  // for flush act, buf src is not decided
        if (OK != act->frameImagePut((STREAM_IMG)stream)) {
          MY_LOGD("cannot put frame image stream(%d)", stream);
        }
        break;
      default:
        MY_LOGW("act buffer source is not defined");
        MY_LOGW("check act exe " P1INFO_ACT_STR, P1INFO_ACT_VAR(*act));
        break;
    }
  }
  //
  if (act->getType() == ACT_TYPE_INTERNAL) {
    mTagOut.set(rAct->getNum());
    if (1 <= mLogLevelI) {
      P1_TRACE_F_BEGIN(SLG_PFL,
                       "P1::DEQ_LOG|Mnum:%d SofIdx:%d Fnum:%d "
                       "Rnum:%d FlushSet:0x%x",
                       act->magicNum, act->sofIdx, act->frmNum, act->reqNum,
                       act->flushSet);
      P1_LOGI(1, "%s [InternalReturn]", act->res.c_str());
      P1_TRACE_C_END(SLG_PFL);  // "P1::DEQ_LOG"
    }
    MY_LOGD(P1INFO_ACT_STR " INTERNAL return", P1INFO_ACT_VAR(*act));
    if (mpTaskCtrl != nullptr) {
      mpTaskCtrl->releaseAct(rAct);
      /* DO NOT use this P1QueAct after releaseAct() */
    }
    P1_TRACE_C_END(SLG_I);  // "P1:release"
    return;
  }
  //
  MY_LOGD(P1INFO_ACT_STR " applyRelease", P1INFO_ACT_VAR(*act));
  //
  // Apply buffers to release
  IStreamBufferSet& rStreamBufferSet = act->appFrame->getStreamBufferSet();
  //
  if (1 <= mLogLevelI) {
    P1_TRACE_F_BEGIN(SLG_PFL,
                     "P1::DEQ_LOG|Mnum:%d SofIdx:%d Fnum:%d "
                     "Rnum:%d FlushSet:0x%x",
                     act->magicNum, act->sofIdx, act->frmNum, act->reqNum,
                     act->flushSet);
    std::string strInfo("");
    base::StringAppendF(&strInfo, "%s [ApplyRelease]", act->res.c_str());
    mNoteRelease.get(&strInfo);
    P1_LOGI(1, "%s", strInfo.c_str());
    P1_TRACE_C_END(SLG_PFL);  // "P1::DEQ_LOG"
  }
  //
  P1_TRACE_S_BEGIN(SLG_I, "P1:applyRelease");
  rStreamBufferSet.applyRelease(getNodeId());
  P1_TRACE_C_END(SLG_I);  // "P1:applyRelease"
  //
  if (1 <= mLogLevelI) {
    mNoteRelease.set(act->frmNum);
  }
  //
  MY_LOGD(P1INFO_ACT_STR " end", P1INFO_ACT_VAR(*act));

  if (mpTaskCtrl != nullptr) {
    mpTaskCtrl->releaseAct(rAct);
    /* DO NOT use this P1QueAct after releaseAct() */
  }

  P1_TRACE_C_END(SLG_I);  // "P1:release"
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::releaseFrame(P1FrameAct* rFrameAct) {
  FUNCTION_IN;
  //
  if (CC_UNLIKELY(rFrameAct->appFrame == nullptr)) {
    MY_LOGE("PipelineFrame is NULL - " P1INFO_ACT_STR,
            P1INFO_ACT_VAR(*rFrameAct));
    return;
  }
  //
#if (USING_DRV_IO_PIPE_EVENT)
  eventStreamingInform();
#endif
  //
  LogInfo::AutoMemo _m(&mLogInfo, LogInfo::CP_OUT_BGN, LogInfo::CP_OUT_END,
                       rFrameAct->magicNum, rFrameAct->frmNum,
                       rFrameAct->reqNum);
  //
  int32_t currReqCnt = 0;
  currReqCnt = std::atomic_fetch_sub_explicit(&mInFlightRequestCnt, 1,
                                              std::memory_order_release);
  P1_TRACE_INT(SLG_B, "P1_request_cnt",
               std::atomic_load_explicit(&mInFlightRequestCnt,
                                         std::memory_order_acquire));
  MY_LOGD("InFlightRequestCount-- (%d) => (%d)", currReqCnt,
          std::atomic_load_explicit(&mInFlightRequestCnt,
                                    std::memory_order_acquire));
  //
#if 1
  // camera display systrace - Dispatch
  if (rFrameAct->reqType == REQ_TYPE_NORMAL && rFrameAct->appFrame != nullptr &&
      rFrameAct->frameTimeStamp > 0) {
    MINT64 const timestamp = rFrameAct->frameTimeStamp;
    P1_TRACE_F_BEGIN(
        SLG_B,  // add information
        "Cam:%d:IspP1:dispatch|timestamp(ns):%" PRId64 " duration(ns):%" PRId64
        " request:%d frame:%d",
        getOpenId(), timestamp, NSCam::Utils::getTimeInNs() - timestamp,
        rFrameAct->appFrame->getRequestNo(), rFrameAct->appFrame->getFrameNo());
    P1_TRACE_C_END(SLG_B);  // "IspP1:dispatch"
  }
#endif
  //
  if (CC_LIKELY(rFrameAct->reqType == REQ_TYPE_NORMAL)) {
    mTagOut.set(rFrameAct->magicNum);
  }
  if (1 <= mLogLevelI) {
    P1_TRACE_F_BEGIN(SLG_PFL,
                     "P1::OUT_LOG|Mnum:%d SofIdx:%d Fnum:%d "
                     "Rnum:%d FlushSet:0x%x",
                     rFrameAct->magicNum, rFrameAct->sofIdx, rFrameAct->frmNum,
                     rFrameAct->reqNum, rFrameAct->flushSet);
    std::string strInfo("");
    base::StringAppendF(
        &strInfo, "[P1::OUT]" P1INFO_ACT_STR " [Release-%d] [DispatchFrame]",
        P1INFO_ACT_VAR(*rFrameAct),
        ((rFrameAct->flushSet == FLUSH_NONEED) ? 0 : 1));
    mNoteDispatch.get(&strInfo);
    P1_LOGI(1, "%s", strInfo.c_str());
    P1_TRACE_C_END(SLG_PFL);  // "P1::OUT_LOG"
  }
  //
  P1_TRACE_F_BEGIN(SLG_I,
                   "onDispatchFrame|Mnum:%d SofIdx:%d Fnum:%d Rnum:%d "
                   "FlushSet:0x%x",
                   rFrameAct->magicNum, rFrameAct->sofIdx, rFrameAct->frmNum,
                   rFrameAct->reqNum, rFrameAct->flushSet);
  //
  // dispatch to next node
  dispatch(rFrameAct->appFrame);
  //
  MY_LOGI("[Dispatch-Return] " P1INFO_ACT_STR " (m_%" PRId64
          ") "
          "(b_%" PRId64 ")",
          P1INFO_ACT_VAR(*rFrameAct), rFrameAct->frameTimeStamp,
          rFrameAct->frameTimeStampBoot);
  //
  if (1 <= mLogLevelI) {
    mNoteDispatch.set(rFrameAct->frmNum);
  }
  //
  P1_TRACE_C_END(SLG_I);  // "onDispatchFrame"
  //
  FUNCTION_OUT;
  //
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::dispatch(std::shared_ptr<IPipelineFrame> pFrame) {
  FUNCTION_IN;
  //
  // dispatch to next node
  MY_LOGI("onDispatchFrame +++ FrameNum(%d) RequestNum(%d)",
          P1GET_FRM_NUM(pFrame), P1GET_REQ_NUM(pFrame));
  onDispatchFrame(pFrame);
  MY_LOGI("onDispatchFrame --- FrameNum(%d) RequestNum(%d)",
          P1GET_FRM_NUM(pFrame), P1GET_REQ_NUM(pFrame));
  //
  FUNCTION_OUT;
  //
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::requestMetadataEarlyCallback(P1QueAct* rAct,
                                        STREAM_META const streamMeta,
                                        IMetadata* pMetadata) {
  P1Act act = GET_ACT_PTR(act, *rAct, BAD_VALUE);
  //
  P1_CHECK_STREAM_SET(META, streamMeta);
  P1_CHECK_MAP_STREAM(Meta, (*act), streamMeta);
  //
  if (pMetadata == nullptr) {
    MY_LOGD("Result Metadata is Null");
    return BAD_VALUE;
  }
  if (pMetadata->count() == 0) {
    MY_LOGD("Result Metadata is Empty");
    return OK;
  }
  MY_LOGD("Meta[%d]=(%d) EarlyCB " P1INFO_ACT_STR, streamMeta,
          pMetadata->count(), P1INFO_ACT_VAR(*act));
  //
  IMetadata outMetadata = *(pMetadata);
  DurationProfile duration("EarlyCB", 5000000LL);  // 5ms
  duration.pulse_up();
  P1_TRACE_S_BEGIN(SLG_I, "EarlyCB");
  onEarlyCallback(act->appFrame, mvStreamMeta[streamMeta]->getStreamId(),
                  outMetadata);
  P1_TRACE_C_END(SLG_I);  // "EarlyCB"
  duration.pulse_down();
  if (duration.isWarning()) {
    MY_LOGI("EarlyCB Meta[%d]=(%d) " P1INFO_ACT_STR, streamMeta,
            pMetadata->count(), P1INFO_ACT_VAR(*act));
  }
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::notifyCtrlSync(P1QueAct* rAct) {
  P1Act act = GET_ACT_PTR(act, *rAct, BAD_VALUE);
  //
  MY_LOGI("CtrlCb_Sync[%d] sof(%d) cap(%d) exp(%" PRId64
          "ns) +++ " P1INFO_ACT_STR,
          IPipelineNodeCallback::eCtrl_Sync, act->sofIdx, act->capType,
          act->frameExpDuration, P1INFO_ACT_VAR(*act));
  DurationProfile duration("CtrlCb_Sync", 3000000LL);  // 3ms
  duration.pulse_up();
  P1_TRACE_F_BEGIN(SLG_I, "CtrlCb_Sync[%d]", IPipelineNodeCallback::eCtrl_Sync);
  onCtrlSync(act->appFrame, act->sofIdx, act->capType, act->frameExpDuration);
  P1_TRACE_C_END(SLG_I);  // "CtrlCb_Sync"
  duration.pulse_down();
  if (duration.isWarning()) {
    MY_LOGI("CtrlCb_Sync[%d] sof(%d) cap(%d) exp(%" PRId64
            "ns) " P1INFO_ACT_STR,
            IPipelineNodeCallback::eCtrl_Sync, act->sofIdx, act->capType,
            act->frameExpDuration, P1INFO_ACT_VAR(*act));
  }
  MY_LOGI("CtrlCb_Sync[%d] sof(%d) cap(%d) exp(%" PRId64
          "ns) --- " P1INFO_ACT_STR,
          IPipelineNodeCallback::eCtrl_Sync, act->sofIdx, act->capType,
          act->frameExpDuration, P1INFO_ACT_VAR(*act));
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::notifyCtrlMeta(IPipelineNodeCallback::eCtrlType eType,
                          P1QueAct* rAct,
                          STREAM_META const streamAppMeta,
                          IMetadata* pAppMetadata,
                          STREAM_META const streamHalMeta,
                          IMetadata* pHalMetadata,
                          MBOOL* rIsChanged) {
  *rIsChanged = MFALSE;
  int64_t nsWarning = 3000000LL;  // 3ms
  MBOOL bChangeLog = (0 <= mLogLevelI);
  switch (eType) {
    case IPipelineNodeCallback::eCtrl_Resize:
      nsWarning = 2000000LL;
      bChangeLog = (2 <= mLogLevelI);
      break;
    case IPipelineNodeCallback::eCtrl_Setting:
    case IPipelineNodeCallback::eCtrl_Readout:
      break;
    default:  // IPipelineNodeCallback::eCtrl_Sync
      return OK;
  }
  P1Act act = GET_ACT_PTR(act, *rAct, BAD_VALUE);
  MUINT cntApp = 0;
  MUINT cntHal = 0;
  P1_CHECK_STREAM_SET(META, streamAppMeta);
  P1_CHECK_STREAM_SET(META, streamHalMeta);
  P1_CHECK_MAP_STREAM(Meta, (*act), streamAppMeta);
  P1_CHECK_MAP_STREAM(Meta, (*act), streamHalMeta);
  if (CC_UNLIKELY(pAppMetadata == nullptr)) {
    MY_LOGD("AppMetadata is Null");
    return BAD_VALUE;
  } else {
    cntApp = pAppMetadata->count();
  }
  if (CC_UNLIKELY(pHalMetadata == nullptr)) {
    MY_LOGD("HalMetadata is Null");
    return BAD_VALUE;
  } else {
    cntHal = pHalMetadata->count();
  }
  MY_LOGI("CtrlCb_Meta[%d] AppMeta[%d]=(%d) HalMeta[%d]=(%d) " P1INFO_ACT_STR,
          eType, streamAppMeta, cntApp, streamHalMeta, cntHal,
          P1INFO_ACT_VAR(*act));
  //
  IMetadata& rAppMetadata = (*(pAppMetadata));
  IMetadata& rHalMetadata = (*(pHalMetadata));
  MBOOL isChanged = MFALSE;
  DurationProfile duration("CtrlCb_Meta", nsWarning);
  duration.pulse_up();
  P1_TRACE_F_BEGIN(SLG_I, "CtrlCb_Meta[%d]", eType);
  if (eType == IPipelineNodeCallback::eCtrl_Setting) {
    onCtrlSetting(act->appFrame, mvStreamMeta[streamAppMeta]->getStreamId(),
                  &rAppMetadata, mvStreamMeta[streamHalMeta]->getStreamId(),
                  &rHalMetadata, &isChanged);
  } else if (eType == IPipelineNodeCallback::eCtrl_Readout) {
    onCtrlReadout(act->appFrame, mvStreamMeta[streamAppMeta]->getStreamId(),
                  &rAppMetadata, mvStreamMeta[streamHalMeta]->getStreamId(),
                  &rHalMetadata, &isChanged);
  } else {  // eType == IPipelineNodeCallback::eCtrl_Resize
    onCtrlResize(act->appFrame, mvStreamMeta[streamAppMeta]->getStreamId(),
                 &rAppMetadata, mvStreamMeta[streamHalMeta]->getStreamId(),
                 &rHalMetadata, &isChanged);
  }
  P1_TRACE_C_END(SLG_I);  // "CtrlCb_Meta"
  duration.pulse_down();
  if (duration.isWarning() || (isChanged && bChangeLog)) {  // for log only
    char str[32] = {0};
    switch (eType) {
      case IPipelineNodeCallback::eCtrl_Setting:
        snprintf(str, sizeof(str), "CtrlCb_Meta[%d]-Setting", eType);
        break;
      case IPipelineNodeCallback::eCtrl_Readout:
        snprintf(str, sizeof(str), "CtrlCb_Meta[%d]-Readout", eType);
        break;
      case IPipelineNodeCallback::eCtrl_Resize:
        snprintf(str, sizeof(str), "CtrlCb_Meta[%d]-Resize", eType);
        break;
      default:  // IPipelineNodeCallback::eCtrl_Sync
        return OK;
    }
    if (duration.isWarning()) {
      MY_LOGI("%s sof(%d) cap(%d) exp(%" PRId64 "ns) " P1INFO_ACT_STR, str,
              act->sofIdx, act->capType, act->frameExpDuration,
              P1INFO_ACT_VAR(*act));
    }

    if (isChanged && bChangeLog) {
      MY_LOGI(
          "%s change AppMeta[%d]=(%d-%d) HalMeta[%d]=(%d-%d) " P1INFO_ACT_STR,
          str, streamAppMeta, cntApp, rAppMetadata.count(), streamHalMeta,
          cntHal, rHalMetadata.count(), P1INFO_ACT_VAR(*act));
    }
  }
  *rIsChanged = isChanged;
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::attemptCtrlSync(P1QueAct* rAct) {
  P1Act act = GET_ACT_PTR(act, *rAct, BAD_VALUE);
  if ((act->appFrame != nullptr) &&
      needCtrlCb(act->appFrame, IPipelineNodeCallback::eCtrl_Sync)) {
    notifyCtrlSync(rAct);
  }
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::attemptCtrlSetting(P1QueAct* rAct) {
  P1Act act = GET_ACT_PTR(act, *rAct, BAD_VALUE);
  MBOOL bIsChanged = MFALSE;
  if ((act->appFrame != nullptr) &&
      needCtrlCb(act->appFrame, IPipelineNodeCallback::eCtrl_Setting)) {
    notifyCtrlMeta(IPipelineNodeCallback::eCtrl_Setting, rAct,
                   STREAM_META_IN_APP, &(act->metaSet.appMeta),
                   STREAM_META_IN_HAL, &(act->metaSet.halMeta), &bIsChanged);
  }
  if (bIsChanged) {
    act->metaSet.PreSetKey = P1_PRESET_KEY_NULL;
  }
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::attemptCtrlResize(P1QueAct* rAct, MBOOL* rIsChanged) {
  P1Act act = GET_ACT_PTR(act, *rAct, BAD_VALUE);
  MBOOL isChanged = MFALSE;
  if ((act->appFrame != nullptr) &&
      needCtrlCb(act->appFrame, IPipelineNodeCallback::eCtrl_Resize)) {
    IMetadata revAppMeta;
    IMetadata revHalMeta;
    notifyCtrlMeta(IPipelineNodeCallback::eCtrl_Resize, rAct,
                   STREAM_META_IN_APP, &revAppMeta, STREAM_META_IN_HAL,
                   &revHalMeta, &isChanged);
    if (isChanged) {
      MBOOL ctrlFlush = MFALSE;
      prepareCropInfo(rAct, &revAppMeta, &revHalMeta,
                      PREPARE_CROP_PHASE_CONTROL_RESIZE, &ctrlFlush);
      if (ctrlFlush) {
        act->setFlush(FLUSH_MIS_RESIZE);
      }
    }
  }
  *rIsChanged = isChanged;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
P1NodeImp::attemptCtrlReadout(P1QueAct* rAct,
                              IMetadata* pAppMetadata,
                              IMetadata* pHalMetadata,
                              MBOOL* rIsChanged) {
  P1Act act = GET_ACT_PTR(act, *rAct, BAD_VALUE);
  MBOOL isChanged = MFALSE;
  if ((act->appFrame != nullptr) &&
      needCtrlCb(act->appFrame, IPipelineNodeCallback::eCtrl_Readout)) {
    notifyCtrlMeta(IPipelineNodeCallback::eCtrl_Readout, rAct,
                   STREAM_META_OUT_APP, pAppMetadata, STREAM_META_OUT_HAL,
                   pHalMetadata, &isChanged);
    if (isChanged) {
      MBOOL outFlush = MFALSE;
      if (tryGetMetadata<MINT32>(pHalMetadata, MTK_P1NODE_CTRL_READOUT_FLUSH,
                                 &outFlush) &&
          (outFlush)) {
        act->setFlush(FLUSH_MIS_READOUT);
      }
    }
  }
  *rIsChanged = isChanged;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1NodeImp::findPortBufIndex(QBufInfo const& deqBuf, P1QueJob* job) {
  size_t job_size = (*job).size();
  if ((job_size == 0) || (deqBuf.mvOut.size() % job_size > 0)) {
    MY_LOGE("Output size is not match");
    return MFALSE;
  }
  // assume the port order is the same in each de-queue set,
  // it only check the first de-queue set and apply to each act
  P1_OUTPUT_PORT port = P1_OUTPUT_PORT_TOTAL;
  MUINT32 group = 0;
  MUINT32 index = 0;
  for (size_t i = 0; i < (deqBuf.mvOut.size()); i += job_size) {
    index = deqBuf.mvOut[i].mPortID.index;
    port = P1_OUTPUT_PORT_TOTAL;
    if (index == PORT_RRZO.index) {
      port = P1_OUTPUT_PORT_RRZO;
    } else if (index == PORT_IMGO.index) {
      port = P1_OUTPUT_PORT_IMGO;
    } else if (index == PORT_EISO.index) {
      port = P1_OUTPUT_PORT_EISO;
    } else if (index == PORT_LCSO.index) {
      port = P1_OUTPUT_PORT_LCSO;
    } else if (index == PORT_RSSO.index) {
      port = P1_OUTPUT_PORT_RSSO;
    } else {
      MY_LOGE("Output port is not match");
      return MFALSE;
    }
    //
    if (port < P1_OUTPUT_PORT_TOTAL) {
      for (size_t j = 0; j < job_size; j++) {
        P1Act act = GET_ACT_PTR(act, (*job).edit(j), MFALSE);
        act->portBufIndex[port] = (group * job_size) + j;
      }
    }
    group++;
  }
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::checkBufferDumping(P1QueAct* rAct) {
  P1Act act = GET_ACT_PTR(act, *rAct, RET_VOID);
#if (SUPPORT_BUFFER_TUNING_DUMP)
  if (mCamDumpEn == 0) {
    return;
  }

  MINT32 nDumpIMGO = property_get_int32("vendor.debug.camera.dump.p1.imgo", 0);
  if (nDumpIMGO == 0) {
    return;
  }

  P1_TRACE_AUTO(SLG_E, "P1:BufferDumping");
  MY_LOGI("[DUMP_IMGO] " P1INFO_ACT_STR, P1INFO_ACT_VAR(*act));

  if (act->reqType != REQ_TYPE_NORMAL) {
    MY_LOGI("[DUMP_IMGO] not-apply (%d)", act->reqType);
    return;
  }

  IMetadata outHalMetadata;
  if (OK != act->frameMetadataGet(STREAM_META_OUT_HAL, &outHalMetadata)) {
    MY_LOGW("[DUMP_IMGO] cannot get out-hal-metadata");
    return;
  }
  //
  std::shared_ptr<IImageBuffer> imgBuf;
  if (mvStreamImg[STREAM_IMG_OUT_FULL] == nullptr) {
    MY_LOGW("[DUMP_IMGO] StreamImg FULL not exist");
    return;
  }
  MY_LOGI("[DUMP_IMGO] map(%d) type(%d) state(%d) [%p]",
          act->streamBufImg[STREAM_IMG_OUT_FULL].bExist,
          act->streamBufImg[STREAM_IMG_OUT_FULL].eSrcType,
          act->streamBufImg[STREAM_IMG_OUT_FULL].eLockState,
          act->streamBufImg[STREAM_IMG_OUT_FULL].spImgBuf.get());
  imgBuf = act->streamBufImg[STREAM_IMG_OUT_FULL].spImgBuf;
  if (imgBuf == NULL) {
    MY_LOGW("[DUMP_IMGO] cannot get ImageBuffer");
    return;
  }
  //
  NSCam::TuningUtils::FILE_DUMP_NAMING_HINT hint;
  MBOOL res = MTRUE;

  res = extract(&hint, &outHalMetadata);
  if (!res) {
    MY_LOGW("[DUMP_IMGO] extract with metadata fail (%d)", res);
    return;
  }
  //
  res = extract(&hint, imgBuf.get());
  if (!res) {
    MY_LOGW("[DUMP_IMGO] extract with ImgBuf fail (%d)", res);
    return;
  }
  //
  res = extract_by_SensorOpenId(&hint, getOpenId());
  if (!res) {
    MY_LOGW("[DUMP_IMGO] extract with OpenId fail (%d)", res);
    return;
  }
  //
  {
    char file_name[512];
    ::memset(file_name, 0, sizeof(file_name));
    genFileName_RAW(file_name, sizeof(file_name), &hint,
                    NSCam::TuningUtils::RAW_PORT_IMGO);
    P1_TRACE_AUTO(SLG_E, file_name);
    MBOOL ret = imgBuf->saveToFile(file_name);
    MY_LOGI("[DUMP_IMGO] SaveFile[%s]:(%d)", file_name, ret);
  }
#endif
  return;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::inflightMonitoring(INFLIGHT_MONITORING_TIMING timing) {
  MINT64 currentTime = (MINT64)(NSCam::Utils::getTimeInNs());
  MBOOL trigger = MFALSE;
  {
    std::lock_guard<std::mutex> _l(mMonitorLock);
    if (currentTime > (mMonitorTime + (P1_PERIODIC_INSPECT_INV_NS))) {
      mMonitorTime = currentTime;
      trigger = MTRUE;
    }
  }
  if (trigger) {
    char str[128] = {0};

    MINT32 cnt = (MINT32)std::atomic_load_explicit(&mInFlightRequestCnt,
                                                   std::memory_order_acquire);
    switch (timing) {
      case IMT_REQ:
        snprintf(str, sizeof(str),
                 "[%d:AfterRequestReceived]"
                 "[Burst=%d Count=%d]",
                 timing, mBurstNum, cnt);
        break;
      case IMT_ENQ:
        snprintf(str, sizeof(str),
                 "[%d:AfterEnQ]"
                 "[Burst=%d Count=%d]",
                 timing, mBurstNum, cnt);
        break;
      case IMT_DEQ:
        snprintf(str, sizeof(str),
                 "[%d:AfterDeQ]"
                 "[Burst=%d Count=%d]",
                 timing, mBurstNum, cnt);
        break;
      default:  // IMT_COMMON
        snprintf(str, sizeof(str),
                 "[%d:CommonCase]"
                 "[Burst=%d Count=%d]",
                 timing, mBurstNum, cnt);
        break;
    }
    mLogInfo.inspect(LogInfo::IT_PERIODIC_CHECK, str);
  }
  return;
}

MUINT32
P1NodeImp::get_and_increase_magicnum() {
  std::lock_guard<std::mutex> _l(mLastNumLock);
  uint32_t magicnum = 0;
  if (CC_UNLIKELY(mpCamIO.get() == nullptr)) {
    MY_LOGE("cannot generate magicnum since mpCamIO is nullptr");
    return -1U;
  }

  MBOOL _result = mpCamIO->sendCommand(ENPipeCmd_GEN_MAGIC_NUM,
                                       reinterpret_cast<MINTPTR>(&magicnum), 0,
                                       0);  // arg2, arg3 <-- dont care

  if (CC_UNLIKELY(_result != MTRUE)) {
    MY_LOGE("mpCamIO returns fail with cmd(ENPipeCmd_GEN_MAGIC_NUM)");
    return -1U;
  }

  MY_LOGD("gen magicnum=%u", magicnum);

  mLastNum = magicnum;

  MUINT32 ret = mLastNum;
  // skip num = 0 as 3A would callback 0 when request stack is empty
  // skip -1U as a reserved number to indicate that which would never happen in
  // 3A queue
  if (ret == 0 || ret == -1U) {
    ret = mLastNum = 1;
  }
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
P1NodeImp::onReturnFrame(P1QueAct* rAct,
                         FLUSH_TYPE flushType,
                         MBOOL isTrigger) {
  P1Act act = GET_ACT_PTR(act, *rAct, RET_VOID);
  //
  P1_TRACE_F_BEGIN(SLG_I, "P1:return|Mnum:%d SofIdx:%d Fnum:%d Rnum:%d",
                   act->magicNum, act->sofIdx, act->frmNum, act->reqNum);
  //
  if (flushType != FLUSH_NONEED) {
    act->setFlush(flushType);
  }
  if (act->getFlush() && getActive()) {
    MY_LOGI("need flush act " P1INFO_ACT_STR, P1INFO_ACT_VAR(*act));
  }
  act->exeState = EXE_STATE_DONE;
  //
  if (1 <= mLogLevelI) {
    if (CC_UNLIKELY(!act->isReadoutReady)) {
      std::string strInfo("");
      strInfo += base::StringPrintf(
          "[P1::DEL]" P1INFO_ACT_STR
          " Readout(%d)"
          " Bypass(%d) ",
          P1INFO_ACT_VAR(*act), act->isReadoutReady,
          ((act->getType() == ACT_TYPE_BYPASS) ? MTRUE : MFALSE));
      act->res += strInfo;
    }
  }
  //
  releaseAction(rAct);
  //
  if (CC_LIKELY(ACT_TYPE_INTERNAL != rAct->getType())) {
    if (CC_LIKELY(act->appFrame != nullptr)) {
      if (mpDeliverMgr != nullptr && mpDeliverMgr->runningGet()) {
        mpDeliverMgr->sendActQueue(rAct, isTrigger);
      } else {
        P1FrameAct frameAct(rAct);
        if (CC_LIKELY(frameAct.ready())) {
          releaseFrame(&frameAct);
        } else {
          MY_LOGE("FrameAct not ready to release - " P1INFO_ACT_STR,
                  P1INFO_ACT_VAR(*act));
        }
      }
    } else {
      MY_LOGE("PipelineFrame is nullptr - " P1INFO_ACT_STR,
              P1INFO_ACT_VAR(*act));
    }
  }
  /* DO NOT use this P1QueAct after releaseAction() / sendActQueue() */
  P1_TRACE_C_END(SLG_I);  // "P1:return"
  return;
}

#if 1
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  IndependentVerification Implementation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
P1NodeImp::IndependentVerification::exe(void) {
  MBOOL bLoop = MTRUE;
  std::shared_ptr<P1NodeImp> spP1NodeImp = mwpP1NodeImp.lock();
  if (CC_UNLIKELY(spP1NodeImp == nullptr)) {
    MY_LOGI("[P1_IV] exit");
    bLoop = MFALSE;
    return bLoop;
  }
  {
    // for Periodic Inspection
    MY_LOGI("[P1_IV] InflightMonitoring +++");
    spP1NodeImp->inflightMonitoring(IMT_COMMON);
    MY_LOGI("[P1_IV] InflightMonitoring ---");
  }
  //
  return bLoop;
}
#endif

};  // namespace NSP1Node
};  // namespace v3
};  // namespace NSCam

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<NSCam::v3::P1Node> NSCam::v3::P1Node::createInstance() {
  return std::make_shared<NSCam::v3::NSP1Node::P1NodeImp>();
}

#if MTKCAM_HAVE_SANDBOX_SUPPORT
int NSCam::v3::NSP1Node::P1NodeImp::setDynamicSensorInfoToIPCHalSensor(
    size_t sensorIdx) {
  IHalSensorList* pHalSensorList = GET_HalSensorList();
  IIPCHalSensorListProv* pIPCSensorList = IIPCHalSensorListProv::getInstance();

  if (CC_UNLIKELY(pHalSensorList == nullptr)) {
    MY_LOGE("IHalSensorList is nullptr");
    return -1;
  }
  if (CC_UNLIKELY(pIPCSensorList == nullptr)) {
    MY_LOGE("IIPCHalSensorListProv is nullptr");
    return -1;
  }

  IHalSensor* pHalSensor = pHalSensorList->createSensor(LOG_TAG, sensorIdx);
  IIPCHalSensor* pIPCSensor = static_cast<IIPCHalSensor*>(
      pIPCSensorList->createSensor(LOG_TAG, sensorIdx));

  if (CC_UNLIKELY(pHalSensor == nullptr)) {
    MY_LOGE("IHalSensor is nullptr");
    return -1;
  }
  if (CC_UNLIKELY(pIPCSensor == nullptr)) {
    MY_LOGE("IIPCHalSensor is nullptr");
    return -1;
  }

  SensorDynamicInfo info;
  MBOOL ok = pHalSensor->querySensorDynamicInfo(sensorIdx, &info);
  if (CC_UNLIKELY(ok == MFALSE)) {
    MY_LOGE("query SensorDynamicInfo returns failed");
    return -1;
  }

  // set info to IPCHalSensor
  pIPCSensor->ipcSetDynamicInfo(info);
  return 0;
}

int NSCam::v3::NSP1Node::P1NodeImp::setDynamicInfoExToIPCHalSensor(
    size_t sensorIdx, const IIPCHalSensor::DynamicInfo& info) {
  IIPCHalSensorListProv* pIPCSensorList = IIPCHalSensorListProv::getInstance();
  if (CC_UNLIKELY(pIPCSensorList == nullptr)) {
    MY_LOGE("IIPCHalSensorListProv is nullptr");
    return -1;
  }
  IIPCHalSensor* pIPCSensor = static_cast<IIPCHalSensor*>(
      pIPCSensorList->createSensor(LOG_TAG, sensorIdx));

  if (CC_UNLIKELY(pIPCSensor == nullptr)) {
    MY_LOGE("IIPCHalSensor is nullptr");
    return -1;
  }

  pIPCSensor->ipcSetDynamicInfoEx(info);
  return 0;
}

#endif
