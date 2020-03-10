/*
 * Copyright (C) 2019 Mediatek Corporation.
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

#define LOG_TAG "Hal3AIpcAdapter"

#include <list>
#include <memory>
#include <stdio.h>
#include <string>
#include <vector>

#include <IPCHal3a.h>
#include <camera/hal/mediatek/mtkcam/ipc/client/Hal3AIpcAdapter.h>

#include <mtkcam/aaa/aaa_utils.h>
#include <mtkcam/utils/metastore/IMetadataProvider.h>
#include <MyUtils.h>
#include <HalSensorList.h>
#include <v4l2/mtk_p1_metabuf.h>
#include <mtkcam/utils/hw/HwTransform.h>
#include <mtkcam/utils/imgbuf/IImageBuffer.h>
#include <mtkcam/utils/imgbuf/IDummyImageBufferHeap.h>

using NSCam::IImageBuffer;
using NSCam::SensorCropWinInfo;
using NSCam::SensorDynamicInfo;
using NSCam::SensorVCInfo;

namespace NS3Av3 {

IHal3A* Hal3AIpcAdapter::getInstance(MINT32 const i4SensorOpenIndex,
                                     const char* strUser) {
  switch (i4SensorOpenIndex) {
    case 0: {
      static Hal3AIpcAdapter* _singleton_0 = new Hal3AIpcAdapter(0);
      _singleton_0->doInit(i4SensorOpenIndex, strUser);
      return _singleton_0;
    }
    case 1: {
      static Hal3AIpcAdapter* _singleton_1 = new Hal3AIpcAdapter(1);
      _singleton_1->doInit(i4SensorOpenIndex, strUser);
      return _singleton_1;
    }
    default:
      CAM_LOGE("Unsupport sensor Index: %d", i4SensorOpenIndex);
      return NULL;
  }
}

bool Hal3AIpcAdapter::doInit(MINT32 const i4SensorIdx, const char* strUser) {
  std::lock_guard<std::mutex> lock(mInitMutex);

  MY_LOGD("[%s] User.count(%d), User doInit(%s)", __FUNCTION__, m_Users.size(),
          strUser);

  if (mInitialized) {
    m_Users[string(strUser)]++;
    return true;
  }

  mMems = {
      {"/mtkHal3aInit", sizeof(hal3a_init_params), &mMemInit, false},
      {"/mtkHal3aConfig", sizeof(hal3a_config_params), &mMemConfig, false},
      {"/mtkHal3aStart", sizeof(hal3a_start_params), &mMemStart, false},
      {"/mtkHal3aStop", sizeof(hal3a_stop_params), &mMemStop, false},
      {"/mtkHal3aStopStt", sizeof(hal3a_stop_stt_params), &mMemStopStt, false},
      {"/mtkHal3aSet", sizeof(hal3a_set_params), &mMemSet, false},
      {"/mtkHal3aSetIsp", sizeof(hal3a_setisp_params), &mMemSetIsp, false},
      {"/mtkHal3aSend3aCtrl", sizeof(hal3a_send3actrl_params), &mMemSend3aCtrl,
       false},
      {"/mtkHal3aGetSensorParam", sizeof(hal3a_getsensorparam_params),
       &mMemGetSensorParam, false},
      {"/mtkHal3aNotifyCallBack", sizeof(hal3a_notifycallback_params),
       &mMemNotifyCallBack, false},
      {"/mtkHal3aTuningPipe", sizeof(hal3a_tuningpipe_params), &mMemTuningPipe,
       false},
      {"/mtkHal3aSttPipe", sizeof(hal3a_sttpipe_params), &mMemSttPipe, false},
      {"/mtkHal3aStt2Pipe", sizeof(hal3a_stt2pipe_params), &mMemStt2Pipe,
       false},
      {"/mtkHal3aHwEvent", sizeof(hal3a_hwevent_params), &mMemHwEvent, false},
      {"/mtkHal3aAePlineLimit", sizeof(hal3a_plinelimit_params),
       &mMemAePlineLimit, false},
      {"/mtkHal3aAfLensConfig", sizeof(hal3a_lensconfig_params),
       &mMemAfLensConfig, false},
      {"/mtkHal3aAfLensEnable", sizeof(hal3a_lensconfig_params),
       &mMemAfLensEnable, false},
      {"/mtkHal3aSartCapture", sizeof(hal3a_start_capture_params),
       &mMemStartCapture, false},
      {"/mtkHal3aStartRequestQ", sizeof(hal3a_start_requestq_params),
       &mMemStartRequestQ, false},
      {"/mtkHal3aPreset", sizeof(hal3a_preset_params), &mMempPreset, false},
      {"/mtkHal3aNofifyP1PwrOn", sizeof(hal3a_notify_p1_pwr_on_params),
       &mMemNofifyP1PwrOn, false},
      {"/mtkHal3aNofifyP1PwrOff", sizeof(hal3a_notify_p1_pwr_off_params),
       &mMemNofifyP1PwrOff, false},
      {"/mtkHal3aNofifyP1Done", sizeof(hal3a_notify_p1_pwr_done_params),
       &mMemNofifyP1Done, false},
      {"/mtkHal3aSetSensorMode", sizeof(hal3a_set_sensor_mode_params),
       &mMemSetSensorMode, false},
      {"/mtkHal3aAttachCB", sizeof(hal3a_attach_cb_params), &mMemAttachCB,
       false},
      {"/mtkHal3aDetachCB", sizeof(hal3a_detach_cb_params), &mMemDetachCB,
       false},
      {"/mtkHal3aGet", sizeof(hal3a_get_params), &mMemGet, false},
      {"/mtkHal3aGetCur", sizeof(hal3a_get_cur_params), &mMemGetCur, false},
      {"/mtkHal3aSetFDInfo", sizeof(hal3a_set_fdInfo_params), &mMemSetFDInfo,
       false}};

  mCommon.init(i4SensorIdx);
  m_i4SensorIdx = i4SensorIdx;
  m_pLsc2ImgBuf = nullptr;

  bool success = mCommon.allocateAllShmMems(&mMems);
  if (!success) {
    IPC_LOGE("Allocate all share memories failed");
    mInitialized = false;
    mCommon.releaseAllShmMems(&mMems);
    return false;
  }

  int ret;
  HalSensorList* pHalSensorList = HalSensorList::singleton();
  int size = pHalSensorList->queryNumberOfSensors();
  for (int i = 0; i < size; i++) {
    const NSCam::NSHalSensor::HalSensorList::EnumInfo* info =
        pHalSensorList->queryEnumInfoByIndex(i);
    if (!info) {
      IPC_LOGE("Query Enum Info by Index failed");
      mInitialized = false;
      mCommon.releaseAllShmMems(&mMems);
      return false;
    }

    IpcSensorStaticInfo_T ipcSensorStatic;
    ipcSensorStatic.idx = i;
    ipcSensorStatic.type = info->getSensorType();
    ipcSensorStatic.deviceId = info->getDeviceId();

    LOG1("ipcSensorStatic idx:%d, type:%d, deviceId:%d", ipcSensorStatic.idx,
         ipcSensorStatic.type, ipcSensorStatic.deviceId);

    if (!pHalSensorList->querySensorStaticInfo(i)) {
      IPC_LOGE("Query Sensor Static Info failed");
      mInitialized = false;
      mCommon.releaseAllShmMems(&mMems);
      return false;
    } else {
      ipcSensorStatic.sensorStaticInfo =
          *pHalSensorList->querySensorStaticInfo(i);
    }

    ret = send3ACtrl(E3ACtrl_IPC_SetStaticInfo,
                     reinterpret_cast<MINTPTR>(&ipcSensorStatic), 0);
    if (ret == false) {
      IPC_LOGE("E3ACtrl_IPC_SetStaticInfo failed");
      mInitialized = false;
      mCommon.releaseAllShmMems(&mMems);
      return false;
    }
  }

  ret = sendRequest(IPC_HAL3A_INIT, &mMemInit);
  if (ret == false) {
    IPC_LOGE("Hal3a init failed");
    mInitialized = false;
    mCommon.releaseAllShmMems(&mMems);
    return false;
  }

  MY_LOGD("[%s] - User.count(%d)", __FUNCTION__, m_Users.size());

  mInitialized = true;

  m_Users[string(strUser)]++;

  return true;
}

void Hal3AIpcAdapter::doUninit() {
  CAM_LOGD("%s", __FUNCTION__);

  int ret = sendRequest(IPC_HAL3A_DEINIT, &mMemInit);
  if (ret == false) {
    IPC_LOGE("Hal3a uninit failed");
    return;
  }

  for (const auto& n : mSttIPCHandles) {
    mCommon.deregisterBuffer(n.second);
  }
  for (const auto& n : mStt2IPCHandles) {
    mCommon.deregisterBuffer(n.second);
  }
  for (const auto& n : mP2TuningBufHandles) {
    mCommon.deregisterBuffer(n.second);
  }
  for (const auto& n : mP1TuningBufHandles) {
    mCommon.deregisterBuffer(n.second);
  }
  for (const auto& n : mLceIPCHandles) {
    mCommon.deregisterBuffer(n.second);
  }

  mSttIPCHandles.clear();
  mMetaBuf1Pool.clear();
  mStt2IPCHandles.clear();
  mMetaBuf2Pool.clear();
  mP2TuningBufHandles.clear();
  mP1TuningBufHandles.clear();
  mLceIPCHandles.clear();

  if (m_pLsc2ImgBuf) {
    m_pLsc2ImgBuf->unlockBuf("LSC_P2_CPU");
    m_pLsc2ImgBuf = nullptr;
  }

  mCommon.releaseAllShmMems(&mMems);
  mInitialized = false;
  return;
}

Hal3AIpcAdapter::Hal3AIpcAdapter(MINT32 const i4SensorIdx) {
  mInitialized = false;
}

Hal3AIpcAdapter::~Hal3AIpcAdapter() {
  CAM_LOGD("~Hal3AIpcAdapter");

  if (mInitialized)
    doUninit();
}

MVOID Hal3AIpcAdapter::destroyInstance(const char* strUser) {
  std::lock_guard<std::mutex> lock(mInitMutex);

  MY_LOGD("[%s] User.count(%d), User uninit(%s)", __FUNCTION__, m_Users.size(),
          strUser);

  if (!m_Users[string(strUser)]) {
    CAM_LOGE("User(%s) did not create 3A!", strUser);
  } else {
    m_Users[string(strUser)]--;
    if (!m_Users[string(strUser)]) {
      m_Users.erase(string(strUser));

      if (!m_Users.size())
        doUninit();
      else
        MY_LOGD("[%s] Still %d users", __FUNCTION__, m_Users.size());
    }
  }

  MY_LOGD("[%s] - User.count(%d)", __FUNCTION__, m_Users.size());
}

MINT32 Hal3AIpcAdapter::sendRequest(IPC_CMD cmd,
                                    ShmMemInfo* memInfo,
                                    int32_t group) {
  hal3a_common_params* params =
      static_cast<hal3a_common_params*>(memInfo->mAddr);
  params->m_i4SensorIdx = m_i4SensorIdx;

  return mCommon.requestSync(cmd, memInfo->mHandle, group);
}

MINT32 Hal3AIpcAdapter::sendRequest(IPC_CMD cmd, ShmMemInfo* memInfo) {
  hal3a_common_params* params =
      static_cast<hal3a_common_params*>(memInfo->mAddr);
  params->m_i4SensorIdx = m_i4SensorIdx;

  return mCommon.requestSync(cmd, memInfo->mHandle);
}

MINT32 Hal3AIpcAdapter::MetaSetFlatten(const std::vector<MetaSet_T*>& requestQ,
                                       struct hal3a_metaset_params* params) {
  if (requestQ.empty() || requestQ[0] == nullptr) {
    IPC_LOGE("RequestQ is Empty or MetaSet Data is NULL");
    return MFALSE;
  }
  LOG1("%s, MagicNum:%d", __func__, requestQ[0]->MagicNum);
  int appRet = 0, halRet = 0;

  params->MagicNum = requestQ[0]->MagicNum;
  params->Dummy = requestQ[0]->Dummy;
  params->PreSetKey = requestQ[0]->PreSetKey;
  appRet = requestQ[0]->appMeta.flatten(&params->appMetaBuffer,
                                        sizeof(params->appMetaBuffer));
  halRet = requestQ[0]->halMeta.flatten(&params->halMetaBuffer,
                                        sizeof(params->halMetaBuffer));
  if (appRet < 0 || halRet < 0) {
    if (appRet < 0)
      IPC_LOGE("AppMeta data flatten failed");
    if (halRet < 0)
      IPC_LOGE("HalMeta data flatten failed");
    return MFALSE;
  }
  return MTRUE;
}

MINT32 Hal3AIpcAdapter::config(const ConfigInfo_T& rConfigInfo) {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  hal3a_config_params* params =
      static_cast<hal3a_config_params*>(mMemConfig.mAddr);
  int appRet = 0, halRet = 0;

  /* pre-check for un-released shared fd */
  for (const auto& n : mSttIPCHandles) {
    LOGD("pre-check un-released fd: stt");
    mCommon.deregisterBuffer(n.second);
  }
  for (const auto& n : mStt2IPCHandles) {
    LOGD("pre-check un-released fd: stt2");
    mCommon.deregisterBuffer(n.second);
  }
  for (const auto& n : mP2TuningBufHandles) {
    LOGD("pre-check un-released fd: p2 tuning");
    mCommon.deregisterBuffer(n.second);
  }
  for (const auto& n : mP1TuningBufHandles) {
    LOGD("pre-check un-released fd: p1 tuning");
    mCommon.deregisterBuffer(n.second);
  }
  for (const auto& n : mLceIPCHandles) {
    LOGD("pre-check un-released fd: lce");
    mCommon.deregisterBuffer(n.second);
  }

  mSttIPCHandles.clear();
  mMetaBuf1Pool.clear();
  mStt2IPCHandles.clear();
  mMetaBuf2Pool.clear();
  mP2TuningBufHandles.clear();
  mP1TuningBufHandles.clear();
  mLceIPCHandles.clear();

  if (m_pLsc2ImgBuf) {
    LOGD("pre-check un-released LSC_P2_CPU");
    m_pLsc2ImgBuf->unlockBuf("LSC_P2_CPU");
    m_pLsc2ImgBuf = nullptr;
  }

  /* pre-check for un-released shared fd */

  params->rConfigInfo.i4SubsampleCount = rConfigInfo.i4SubsampleCount;
  params->rConfigInfo.i4BitMode = rConfigInfo.i4BitMode;
  params->rConfigInfo.i4HlrOption = rConfigInfo.i4HlrOption;
  appRet = rConfigInfo.CfgHalMeta.flatten(&params->CfgHalMeta,
                                          sizeof(params->CfgHalMeta));
  halRet = rConfigInfo.CfgAppMeta.flatten(&params->CfgAppMeta,
                                          sizeof(params->CfgAppMeta));
  if (appRet < 0 || halRet < 0) {
    if (appRet < 0)
      IPC_LOGE("AppMeta data flatten failed");
    if (halRet < 0)
      IPC_LOGE("HalMeta data flatten failed");
    return MFALSE;
  }

  /* getMatrix to active and from active here */
  params->rConfigInfo.matFromAct = rConfigInfo.matFromAct;
  params->rConfigInfo.matToAct = rConfigInfo.matToAct;

  CheckError(sendRequest(IPC_HAL3A_CONFIG, &mMemConfig) == false,
             FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  LOG1("%s ----", __func__);
  return MTRUE;
}

MINT32 Hal3AIpcAdapter::start(MINT32 i4StartNum) {
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  hal3a_start_params* params =
      static_cast<hal3a_start_params*>(mMemStart.mAddr);
  params->i4StartNum = i4StartNum;
  LOG1("i4StartNum: %d start++++", i4StartNum);

  CheckError(sendRequest(IPC_HAL3A_START, &mMemStart) == false,
             FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  LOG1("i4StartNum:0x%x start----", i4StartNum);
  return 0;
}

MINT32 Hal3AIpcAdapter::stop() {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  CheckError(sendRequest(IPC_HAL3A_STOP, &mMemStop) == false,
             FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  if (m_pLsc2ImgBuf) {
    m_pLsc2ImgBuf->unlockBuf("LSC_P2_CPU");
    m_pLsc2ImgBuf = nullptr;
  }

  LOG1("%s ----", __func__);
  return 0;
}

MVOID Hal3AIpcAdapter::stopStt() {
  CheckError(sendRequest(IPC_HAL3A_STOP_STT, &mMemStopStt) == false, VOID_VALUE,
             "@%s, requestSync fails", __FUNCTION__);
  return;
}

MINT32 Hal3AIpcAdapter::set(const std::vector<MetaSet_T*>& requestQ) {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  hal3a_set_params* params = static_cast<hal3a_set_params*>(mMemSet.mAddr);
  int ret = MTRUE;
  ret = MetaSetFlatten(requestQ, &params->requestQ);
  if (ret == MFALSE)
    return MFALSE;
  CheckError(sendRequest(IPC_HAL3A_SET, &mMemSet, IPC_GROUP_SET) == false,
             FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  LOG1("%s ----", __func__);
  return MTRUE;
}

MINT32 Hal3AIpcAdapter::setIsp(MINT32 flowType,
                               const MetaSet_T& control,
                               TuningParam* pTuningBuf,
                               MetaSet_T* pResult) {
  LOG1("%s ++++", __func__);
  std::lock_guard<std::mutex> l(mIspMutex);
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  hal3a_setisp_params* params =
      static_cast<hal3a_setisp_params*>(mMemSetIsp.mAddr);

  params->flowType = flowType;
  /* flatten inMeta to share memory*/
  params->control.MagicNum = control.MagicNum;
  params->control.Dummy = control.Dummy;
  params->control.PreSetKey = control.PreSetKey;
  ssize_t inAppSize = control.appMeta.flatten(&params->inAppMetaBuffer,
                                              sizeof(params->inAppMetaBuffer));
  LOG1("%s client: inAppSize = %zd", __func__, inAppSize);
  ssize_t inHalSize = control.halMeta.flatten(&params->inHalMetaBuffer,
                                              sizeof(params->inHalMetaBuffer));
  LOG1("%s client: inHalSize = %zd", __func__, inHalSize);
  if (inAppSize < 0 || inHalSize < 0) {
    if (inAppSize < 0)
      IPC_LOGE("inAppMeta data flatten failed");
    if (inHalSize < 0)
      IPC_LOGE("inHalMeta data flatten failed");
    return DEAD_OBJECT;
  }

  /* TODO: handle LCEI */
  IImageBuffer* pLCEI = nullptr;
  if (pTuningBuf) {
    pLCEI = reinterpret_cast<IImageBuffer*>(pTuningBuf->pLcsBuf);
  } else {
    IPC_LOGE("Tuning Buffer is NULL");
    return DEAD_OBJECT;
  }
  MINT32 buff_handle;
  params->u4LceEnable = 0;
  if (pLCEI != NULL) {
    params->u4LceEnable = 1;
    params->lceBufInfo.imgFormat = pLCEI->getImgFormat();
    params->lceBufInfo.width = pLCEI->getImgSize().w;
    params->lceBufInfo.height = pLCEI->getImgSize().h;
    params->lceBufInfo.planeCount = pLCEI->getPlaneCount();
    for (MINT i = 0; i < params->lceBufInfo.planeCount; i++) {
      params->lceBufInfo.bufStrides[i] = pLCEI->getBufStridesInBytes(i);
      params->lceBufInfo.bufScanlines[i] = pLCEI->getBufScanlines(i);
      params->lceBufInfo.bufPA[i] = 0;
    }
    /* handle with fd and va */
    auto ipc_handle = mLceIPCHandles.find(pLCEI->getFD(0));
    if (ipc_handle == mLceIPCHandles.end()) {
      buff_handle = mCommon.registerBuffer(pLCEI->getFD(0));
      mLceIPCHandles[pLCEI->getFD(0)] = buff_handle;
    }
    params->lceBufInfo.fd[0] = mLceIPCHandles[pLCEI->getFD(0)];

    LOG1("%s LCE: u4LceEnable = %d", __func__, params->u4LceEnable);
    LOG1("%s LCE: imgFormat = %d", __func__, params->lceBufInfo.imgFormat);
    LOG1("%s LCE: width = %d", __func__, params->lceBufInfo.width);
    LOG1("%s LCE: bufStrides = %d", __func__, params->lceBufInfo.bufStrides[0]);
    LOG1("%s LCE: fd = %d", __func__, params->lceBufInfo.fd[0]);
  }

  auto it_p2tuningbuf_handle = mP2TuningBufHandles.find(pTuningBuf->RegBuf_fd);
  if (it_p2tuningbuf_handle == mP2TuningBufHandles.end()) {
    int32_t buff_handle = mCommon.registerBuffer(pTuningBuf->RegBuf_fd);
    if (buff_handle < 0) {
      IPC_LOGE("register p2 tuning buffer fail");
      return DEAD_OBJECT;
    }
    auto pair = mP2TuningBufHandles.emplace(pTuningBuf->RegBuf_fd, buff_handle);
    it_p2tuningbuf_handle = pair.first;
  }
  params->p2tuningbuf_handle = it_p2tuningbuf_handle->second;

  CheckError(
      sendRequest(IPC_HAL3A_SETISP, &mMemSetIsp, IPC_GROUP_SETISP) == false,
      FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  if (pLCEI != NULL) {
    MSize tempLCEIsize(params->lceBufInfo.width, params->lceBufInfo.height);
    LOG1("update LCE w/h %d %d", tempLCEIsize.w, tempLCEIsize.h);
    pLCEI->updateInfo(tempLCEIsize);
  }

  /* handle Lsc2 buffer */
  LOG1("%s shading: u4Lsc2Enable = %d", __func__, params->u4Lsc2Enable);
  if (params->u4Lsc2Enable == 1) {
    if (m_pLsc2ImgBuf) {
      pTuningBuf->pLsc2Buf = m_pLsc2ImgBuf.get();
    } else {
      IPCImageBufAllocator::config cfg = {0};
      cfg.format = params->lsc2BufInfo.imgFormat;
      cfg.width = params->lsc2BufInfo.width;
      cfg.height = params->lsc2BufInfo.height;
      cfg.planecount = params->lsc2BufInfo.planeCount;
      cfg.strides[0] = params->lsc2BufInfo.bufStrides[0];
      cfg.scanlines[0] = params->lsc2BufInfo.bufScanlines[0];
      cfg.va[0] = reinterpret_cast<MUINTPTR>(params->pLsc2BufCont);
      cfg.pa[0] = params->lsc2BufInfo.bufPA[0];
      cfg.fd[0] = params->lsc2BufInfo.fd[0];
      cfg.imgbits = params->lsc2BufInfo.imgBits;
      cfg.stridepixel[0] = params->lsc2BufInfo.bufStridesPixel[0];
      cfg.bufsize[0] = params->lsc2BufInfo.bufSize[0];

      IPCImageBufAllocator allocator(cfg, "LSC_P2");
      std::shared_ptr<IImageBuffer> pImgBuf = allocator.createImageBuffer();
      pImgBuf->lockBuf("LSC_P2_CPU");
      pTuningBuf->pLsc2Buf = pImgBuf.get();
      m_pLsc2ImgBuf = pImgBuf;
    }
  } else {
    pTuningBuf->pLsc2Buf = NULL;
  }

  /* unflatten outMeta to P1node*/
  if (!pResult) {
    IPC_LOGE("pResult for Output Metadata is NULL");
    return DEAD_OBJECT;
  }
  pResult->MagicNum = params->metaSetResult.MagicNum;
  pResult->Dummy = params->metaSetResult.Dummy;
  pResult->PreSetKey = params->metaSetResult.PreSetKey;
  ssize_t outAppSize = pResult->appMeta.unflatten(
      &params->outAppMetaBuffer, sizeof(params->outAppMetaBuffer));
  LOG1("%s client: outAppSize = %zd", __func__, outAppSize);
  ssize_t outHalSize = pResult->halMeta.unflatten(
      &params->outHalMetaBuffer, sizeof(params->outHalMetaBuffer));
  LOG1("%s client: outHalSize = %zd", __func__, outHalSize);
  if (outAppSize < 0 || outHalSize < 0) {
    if (outAppSize < 0)
      IPC_LOGE("outAppMeta data unflatten failed");
    if (outHalSize < 0)
      IPC_LOGE("outHalMeta data unflatten failed");
    return DEAD_OBJECT;
  }

  LOG1("%s ----", __func__);
  return OK;
}
MINT32 Hal3AIpcAdapter::startRequestQ(const std::vector<MetaSet_T*>& requestQ) {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  hal3a_start_requestq_params* params =
      static_cast<hal3a_start_requestq_params*>(mMemStartRequestQ.mAddr);
  int ret = MTRUE;
  ret = MetaSetFlatten(requestQ, &params->requestQ);
  if (ret == MFALSE)
    return MFALSE;
  CheckError(
      sendRequest(IPC_HAL3A_START_REQUEST_Q, &mMemStartRequestQ) == false,
      FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  LOG1("%s ----", __func__);
  return MTRUE;
}

MINT32 Hal3AIpcAdapter::startCapture(const std::vector<MetaSet_T*>& requestQ,
                                     MINT32 i4StartNum) {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  hal3a_start_capture_params* params =
      static_cast<hal3a_start_capture_params*>(mMemStartCapture.mAddr);
  int ret = MTRUE;
  ret = MetaSetFlatten(requestQ, &params->requestQ);
  if (ret == MFALSE)
    return MFALSE;
  CheckError(sendRequest(IPC_HAL3A_START_CAPTURE, &mMemStartCapture) == false,
             FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  LOG1("%s ----", __func__);
  return MTRUE;
}

MINT32 Hal3AIpcAdapter::preset(const std::vector<MetaSet_T*>& requestQ) {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  hal3a_preset_params* params =
      static_cast<hal3a_preset_params*>(mMempPreset.mAddr);
  int ret = MTRUE;
  ret = MetaSetFlatten(requestQ, &params->requestQ);
  if (ret == MFALSE)
    return MFALSE;
  CheckError(
      sendRequest(IPC_HAL3A_PRESET, &mMempPreset, IPC_GROUP_PRESET) == false,
      FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  LOG1("%s ----", __func__);
  return MTRUE;
}

MINT32 Hal3AIpcAdapter::send3ACtrlHalSensor(hal3a_send3actrl_params* params,
                                            E3ACtrl_T e3ACtrl,
                                            MINTPTR i4Arg1,
                                            MINTPTR i4Arg2) {
  NSCam::SensorCropWinInfo* pIpcSensorCropWinInfo;
  SET_PD_BLOCK_INFO_T* pIpcSensorPdafInfo;
  SensorVCInfo* pIpcSensorVCInfo;

  switch (e3ACtrl) {
    case E3ACtrl_IPC_CropWin:
      if (i4Arg1) {
        params->arg1.scenario = *reinterpret_cast<MUINT32*>(i4Arg1);
      } else {
        IPC_LOGE("E3ACtrl_IPC_CropWin: scenario is empty");
        return MFALSE;
      }

      pIpcSensorCropWinInfo = reinterpret_cast<SensorCropWinInfo*>(i4Arg2);
      if (pIpcSensorCropWinInfo) {
        memcpy(&params->arg2.sensorCropWinInfo, pIpcSensorCropWinInfo,
               sizeof(SensorCropWinInfo));
      } else {
        IPC_LOGE("IPC Sensor Crop Window Info is NULL");
        return MFALSE;
      }
      break;

    case E3ACtrl_IPC_PixelClock:
      if (i4Arg1) {
        params->arg1.pixelClokcFreq = *reinterpret_cast<MINT32*>(i4Arg1);
      } else {
        IPC_LOGE("E3ACtrl_IPC_PixelClock: pixelClokcFreq is empty");
        return MFALSE;
      }
      break;

    case E3ACtrl_IPC_PixelLine:
      if (i4Arg1) {
        params->arg1.frameSyncPixelLineNum =
            *reinterpret_cast<MUINT32*>(i4Arg1);
      } else {
        IPC_LOGE("E3ACtrl_IPC_PixelLine: frameSyncPixelLineNum is empty");
        return MFALSE;
      }
      break;

    case E3ACtrl_IPC_PdafInfo:
      if (i4Arg1) {
        params->arg1.scenario = *reinterpret_cast<MUINT32*>(i4Arg1);
      } else {
        IPC_LOGE("E3ACtrl_IPC_PdafInfo: scenario is empty");
        return MFALSE;
      }

      pIpcSensorPdafInfo = reinterpret_cast<SET_PD_BLOCK_INFO_T*>(i4Arg2);
      if (pIpcSensorPdafInfo) {
        memcpy(&params->arg2.sensorPdafInfo, pIpcSensorPdafInfo,
               sizeof(SET_PD_BLOCK_INFO_T));
      } else {
        IPC_LOGE("IPC Sensor Pda Info is NULL");
        return MFALSE;
      }
      break;

    case E3ACtrl_IPC_PdafCapacity:
      if (i4Arg1) {
        params->arg1.scenario = *reinterpret_cast<MUINT32*>(i4Arg1);
      } else {
        IPC_LOGE("E3ACtrl_IPC_PdafCapacity: scenario is empty");
        return MFALSE;
      }
      if (i4Arg2) {
        params->arg2.sensorPdafCapacity = *reinterpret_cast<MBOOL*>(i4Arg2);
      } else {
        IPC_LOGE("E3ACtrl_IPC_PdafCapacity: sensorPdafCapacity is empty");
        return MFALSE;
      }
      break;

    case E3ACtrl_IPC_SensorVCInfo:
      if (i4Arg2) {
        params->arg2.scenario = *reinterpret_cast<MUINT32*>(i4Arg2);
      } else {
        IPC_LOGE("E3ACtrl_IPC_SensorVCInfo: scenario is empty");
        return MFALSE;
      }

      pIpcSensorVCInfo = reinterpret_cast<SensorVCInfo*>(i4Arg1);
      if (pIpcSensorVCInfo) {
        memcpy(&params->arg1.sensorVCInfo, pIpcSensorVCInfo,
               sizeof(SensorVCInfo));
      } else {
        IPC_LOGE("IPC Sensor VC Info is NULL");
        return MFALSE;
      }
      break;

    case E3ACtrl_IPC_DefFrameRate:
      if (i4Arg1) {
        params->arg1.scenario = *reinterpret_cast<MUINT32*>(i4Arg1);
      } else {
        IPC_LOGE("E3ACtrl_IPC_DefFrameRate: scenario is empty");
        return MFALSE;
      }
      if (i4Arg2) {
        params->arg2.defaultFrameRate = *reinterpret_cast<MUINT32*>(i4Arg2);
      } else {
        IPC_LOGE("E3ACtrl_IPC_DefFrameRate: defaultFrameRate is empty");
        return MFALSE;
      }
      break;

    case E3ACtrl_IPC_RollingShutter:
      if (i4Arg1) {
        params->arg1.tline = *reinterpret_cast<MUINT32*>(i4Arg1);
      } else {
        IPC_LOGE("E3ACtrl_IPC_RollingShutter: tline is empty");
        return MFALSE;
      }
      if (i4Arg2) {
        params->arg2.vsize = *reinterpret_cast<MUINT32*>(i4Arg2);
      } else {
        IPC_LOGE("E3ACtrl_IPC_RollingShutter: vsize is empty");
        return MFALSE;
      }
      params->e3ACtrl = e3ACtrl;
      break;

    case E3ACtrl_IPC_VerticalBlanking:
      if (i4Arg1) {
        params->arg1.verticalBlanking = *reinterpret_cast<MINT32*>(i4Arg1);
      } else {
        IPC_LOGE("E3ACtrl_IPC_VerticalBlanking: verticalBlanking is empty");
        return MFALSE;
      }
      break;

    default:
      break;
  }

  params->e3ACtrl = e3ACtrl;
  CheckError(sendRequest(IPC_HAL3A_SEND3ACTRL, &mMemSend3aCtrl) == false,
             FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  return MTRUE;
}

MINT32 Hal3AIpcAdapter::send3ACtrlPeriSensor(hal3a_send3actrl_params* params,
                                             E3ACtrl_T e3ACtrl,
                                             MINTPTR i4Arg1,
                                             MINTPTR i4Arg2) {
  if (i4Arg1) {
    NS3Av3::IpcPeriSensorData_T* data =
        reinterpret_cast<NS3Av3::IpcPeriSensorData_T*>(i4Arg1);
    params->arg1.periSensorData.acceleration[0] = data->acceleration[0];
    params->arg1.periSensorData.acceleration[1] = data->acceleration[1];
    params->arg1.periSensorData.acceleration[2] = data->acceleration[2];
  } else {
    IPC_LOGE("IpcPeriSensorData_T is empty");
    return MFALSE;
  }

  params->e3ACtrl = e3ACtrl;
  CheckError(sendRequest(IPC_HAL3A_SEND3ACTRL, &mMemSend3aCtrl) == false,
             FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  return MTRUE;
}

MINT32 Hal3AIpcAdapter::send3ACtrl(E3ACtrl_T e3ACtrl,
                                   MINTPTR i4Arg1,
                                   MINTPTR i4Arg2) {
  /* Set Static Info is Initialization */
  if (e3ACtrl != E3ACtrl_IPC_SetStaticInfo) {
    CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  }

  hal3a_getsensorparam_params* params_gsp =
      static_cast<hal3a_getsensorparam_params*>(mMemGetSensorParam.mAddr);
  hal3a_notifycallback_params* params_ncb =
      static_cast<hal3a_notifycallback_params*>(mMemNotifyCallBack.mAddr);
  hal3a_tuningpipe_params* params_tp =
      static_cast<hal3a_tuningpipe_params*>(mMemTuningPipe.mAddr);
  hal3a_sttpipe_params* params_sp =
      static_cast<hal3a_sttpipe_params*>(mMemSttPipe.mAddr);
  hal3a_stt2pipe_params* params_sp2 =
      static_cast<hal3a_stt2pipe_params*>(mMemStt2Pipe.mAddr);
  hal3a_hwevent_params* params_hwe =
      static_cast<hal3a_hwevent_params*>(mMemHwEvent.mAddr);
  hal3a_plinelimit_params* params_pl =
      static_cast<hal3a_plinelimit_params*>(mMemAePlineLimit.mAddr);
  hal3a_lensconfig_params* params_lc =
      static_cast<hal3a_lensconfig_params*>(mMemAfLensConfig.mAddr);
  hal3a_lensconfig_params* params_le =
      static_cast<hal3a_lensconfig_params*>(mMemAfLensEnable.mAddr);
  hal3a_send3actrl_params* params =
      static_cast<hal3a_send3actrl_params*>(mMemSend3aCtrl.mAddr);

  AEInitExpoSetting_T* pAEInitExpoSetting;  // for GetAEInitExpoSetting
  IpcSensorStaticInfo_T* pIpcSensorStatic;  // for SetStaticInfo
  SensorDynamicInfo* pSensorDynamicInfo;    // for SetDynamicInfo
  NSCam::IIPCHalSensor::DynamicInfo*
      pSensorDynamicInfoExt;                 // for SetDynamicInfoEx
  IpcMetaStaticInfo_T* pIpcMetaStaticInfo;   // for MetaStaticInfo
  v4l2::P1Event* evt;                        // for HwSignal
  static uint64_t pTuningDrvBufVa;           // for ExchangeTuningBuf
  IPC_Metabuf1_T* pMeta1;                    // for SttControl
  IPC_Metabuf2_T* pMeta2;                    // for Stt2Control
  static IPC_P1NotifyCb_T clientP1NotifyCb;  // for NotifyCb
  static RequestSet_T clientRequestSet;
  static CapParam_T clientCapParam;
  int rc = MTRUE;

  LOG1("%s e3ACtrl:0x%x ++++", __func__, e3ACtrl);

  switch (e3ACtrl) {
    case E3ACtrl_IPC_CropWin:
    case E3ACtrl_IPC_PixelClock:
    case E3ACtrl_IPC_PixelLine:
    case E3ACtrl_IPC_PdafInfo:
    case E3ACtrl_IPC_PdafCapacity:
    case E3ACtrl_IPC_SensorVCInfo:
    case E3ACtrl_IPC_DefFrameRate:
    case E3ACtrl_IPC_RollingShutter:
    case E3ACtrl_IPC_VerticalBlanking:
      send3ACtrlHalSensor(params, e3ACtrl, i4Arg1, i4Arg2);
      break;
    case E3ACtrl_IPC_SetPeriSensorData:
      send3ACtrlPeriSensor(params, e3ACtrl, i4Arg1, i4Arg2);
      break;
    case E3ACtrl_GetAEInitExpoSetting:
      pAEInitExpoSetting = reinterpret_cast<AEInitExpoSetting_T*>(i4Arg1);
      if (pAEInitExpoSetting) {
        memcpy(&params->arg1.initExpoSetting, pAEInitExpoSetting,
               sizeof(AEInitExpoSetting_T));
      } else {
        IPC_LOGE("AE Init Exposure Setting Info is NULL");
        return MFALSE;
      }

      params->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_SEND3ACTRL, &mMemSend3aCtrl) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      memcpy(pAEInitExpoSetting, &params->arg1.initExpoSetting,
             sizeof(AEInitExpoSetting_T));
      break;

    case E3ACtrl_IPC_SetStaticInfo:
      pIpcSensorStatic = reinterpret_cast<IpcSensorStaticInfo_T*>(i4Arg1);
      if (pIpcSensorStatic) {
        memcpy(&params->arg1.ipcSensorStatic, pIpcSensorStatic,
               sizeof(IpcSensorStaticInfo_T));
      } else {
        IPC_LOGE("IPC Sensor Statistical Info is NULL");
        return MFALSE;
      }
      params->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_SEND3ACTRL, &mMemSend3aCtrl) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      break;

    case E3ACtrl_IPC_Set_MetaStaticInfo:
      pIpcMetaStaticInfo = reinterpret_cast<IpcMetaStaticInfo_T*>(i4Arg1);
      if (pIpcMetaStaticInfo) {
        memcpy(&params->arg1.ipcMetaStaticInfo, pIpcMetaStaticInfo,
               sizeof(IpcMetaStaticInfo_T));
      } else {
        IPC_LOGE("IPC Sensor Meta Statistical Info is NULL");
        return MFALSE;
      }
      params->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_SEND3ACTRL, &mMemSend3aCtrl) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      break;

    case E3ACtrl_IPC_SetDynamicInfo:
      pSensorDynamicInfo = reinterpret_cast<SensorDynamicInfo*>(i4Arg1);
      if (pSensorDynamicInfo) {
        memcpy(&params->arg1.sensorDynamicInfo, pSensorDynamicInfo,
               sizeof(SensorDynamicInfo));
      } else {
        IPC_LOGE("IPC Sensor Dynamic Info is NULL");
        return MFALSE;
      }
      params->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_SEND3ACTRL, &mMemSend3aCtrl) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      break;

    case E3ACtrl_IPC_SetDynamicInfoEx:
      pSensorDynamicInfoExt =
          reinterpret_cast<NSCam::IIPCHalSensor::DynamicInfo*>(i4Arg1);
      if (pSensorDynamicInfoExt) {
        memcpy(&params->arg1.sensorDynamicInfoExt, pSensorDynamicInfoExt,
               sizeof(NSCam::IIPCHalSensor::DynamicInfo));
      } else {
        IPC_LOGE("IPC Sensor Dynamic Info Ext is NULL");
        return MFALSE;
      }
      params->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_SEND3ACTRL, &mMemSend3aCtrl) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      break;

    case E3ACtrl_IPC_AE_GetSensorParamEnable:
      params_gsp->arg1.enabled = i4Arg1;
      params_gsp->e3ACtrl = e3ACtrl;
      CheckError(
          sendRequest(IPC_HAL3A_GETSENSORPARAM_ENABLE, &mMemGetSensorParam,
                      IPC_GROUP_CB_SENSOR_ENABLE) == false,
          FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      break;

    case E3ACtrl_IPC_AE_GetSensorParam:
      params_gsp->arg2.timeoutMs = i4Arg2;
      params_gsp->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_GETSENSORPARAM, &mMemGetSensorParam,
                             IPC_GROUP_GETSENSORPARAM) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      if (!i4Arg1) {
        IPC_LOGE("Argument for IPC Sensor Parameter is NULL");
        return MFALSE;
      }
      *reinterpret_cast<IPC_SensorParam_T*>(i4Arg1) =
          params_gsp->arg1.ipcSensorParam;
      break;

    case E3ACtrl_IPC_P1_NotifyCbEnable:
      params_ncb->arg1.enabled = i4Arg1;
      params_ncb->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_NOTIFYCB_ENABLE, &mMemNotifyCallBack,
                             IPC_GROUP_CB_SENSOR_ENABLE) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      break;

    case E3ACtrl_IPC_P1_NotifyCb:
      int ret;
      params_ncb->arg1.enabled = i4Arg1;
      params_ncb->e3ACtrl = e3ACtrl;

      CheckError(sendRequest(IPC_HAL3A_NOTIFYCB, &mMemNotifyCallBack,
                             IPC_GROUP_NOTIFYCB) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

      rc = params_ncb->callback_ret;
      if (params_ncb->arg1.enabled != 1 && params_ncb->callback_ret) {
        clientP1NotifyCb.u4CapType = params_ncb->pu4CapType;
        clientP1NotifyCb.u.proc_finish.magicnum = params_ncb->pmagicnum;

        if (clientP1NotifyCb.u4CapType == IHal3ACb::eID_NOTIFY_3APROC_FINISH) {
          /* RequestSet_T */
          clientRequestSet.vNumberSet.clear();  // copy vector data
          clientRequestSet.vNumberSet.push_back(params_ncb->pRvNumberSet);
          clientRequestSet.fgKeep = params_ncb->pRfgKeep;  // copy fgKeep
          clientRequestSet.fgDisableP1 =
              params_ncb->pRfgDisableP1;  // copy fgDisableP1
          clientP1NotifyCb.u.proc_finish.pRequestResult =
              &clientRequestSet;  // pRequestResult point to static local
          /* CapParam_T */
          clientCapParam.u4CapType = params_ncb->pCu4CapType;  // copy u4CapType
          clientCapParam.i8ExposureTime =
              params_ncb->pCi8ExposureTime;  // copy i8ExposureTime
          ret = clientCapParam.metadata.unflatten(
              params_ncb->pCmetadata, sizeof(params_ncb->pCmetadata));
          if (ret < 0) {
            IPC_LOGE("Capture Parameter Metadata unflatten failed");
            return MFALSE;
          }
          clientP1NotifyCb.u.proc_finish.pCapParam =
              &clientCapParam;  // pCapParam point to static local
        }
        if (!i4Arg2) {
          IPC_LOGE("Argument for Notify Callback is NULL");
          return MFALSE;
        }
        *reinterpret_cast<IPC_P1NotifyCb_T*>(i4Arg2) = clientP1NotifyCb;
      }
      break;

    case E3ACtrl_IPC_P1_WaitTuningReq:
      params_tp->arg1.cmd = i4Arg1;
      params_tp->e3ACtrl = e3ACtrl;

      if (IPC_IspTuningMgr_T::cmdTERMINATED == i4Arg1) {
        CheckError(sendRequest(IPC_HAL3A_TUNINGPIPE_TERM, &mMemTuningPipe,
                               IPC_GROUP_TUNINGPIPE_TERM) == false,
                   FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      } else {
        CheckError(sendRequest(IPC_HAL3A_TUNINGPIPE, &mMemTuningPipe,
                               IPC_GROUP_TUNINGPIPE) == false,
                   FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      }
      // P1nodeImp just use magicnum and response so now no handle bufVa
      if (IPC_IspTuningMgr_T::cmdWAIT_REQUEST == i4Arg1) {
        IPC_IspTuningMgr_T* pResult =
            reinterpret_cast<IPC_IspTuningMgr_T*>(i4Arg2);
        if (!pResult) {
          IPC_LOGE("Argument for Waiting Tuning Request is NULL");
          return MFALSE;
        }
        memcpy(pResult, &params_tp->arg2.ipcIspTuningMgr,
               sizeof(IPC_IspTuningMgr_T));
      }
      break;

    case E3ACtrl_IPC_P1_ExchangeTuningBuf:
      params_tp->arg1.cmd = i4Arg1;
      if (IPC_IspTuningMgr_T::cmdACQUIRE_FROM_FMK == i4Arg1) {
        IPC_IspTuningMgr_T* pTuning =
            reinterpret_cast<IPC_IspTuningMgr_T*>(i4Arg2);
        if (!pTuning) {
          IPC_LOGE("Tuning Mgr Pointer is NULL");
          return MFALSE;
        }

        if ((pTuning->bufFd < 0) || (!pTuning->bufVa)) {
          IPC_LOGE("Tuning Buffer is NULL");
          return MFALSE;
        }
        params_tp->arg2.ipcIspTuningMgr.magicnum = pTuning->magicnum;
        params_tp->arg2.ipcIspTuningMgr.response = pTuning->response;
        auto it_p1tuningbuf_handle = mP1TuningBufHandles.find(pTuning->bufFd);
        if (it_p1tuningbuf_handle == mP1TuningBufHandles.end()) {
          int32_t buff_handle = mCommon.registerBuffer(pTuning->bufFd);
          if (buff_handle < 0) {
            IPC_LOGE("register p1 tuning buffer fail");
            return DEAD_OBJECT;
          }
          auto pair = mP1TuningBufHandles.emplace(pTuning->bufFd, buff_handle);
          it_p1tuningbuf_handle = pair.first;
        }
        params_tp->p1tuningbuf_handle = it_p1tuningbuf_handle->second;

        // In order to enque same bufVa to driver, record it
        pTuningDrvBufVa = pTuning->bufVa;
      }
      params_tp->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_TUNINGPIPE, &mMemTuningPipe,
                             IPC_GROUP_TUNINGPIPE) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

      if (IPC_IspTuningMgr_T::cmdRESULT_FROM_FMK == i4Arg1) {
        if (params_tp->flag) {
          IPC_IspTuningMgr_T* pResult =
              reinterpret_cast<IPC_IspTuningMgr_T*>(i4Arg2);
          if (!pResult) {
            IPC_LOGE("Argument for ISP Tuning Mgr is NULL");
            return MFALSE;
          }
          pResult->magicnum = params_tp->arg2.ipcIspTuningMgr.magicnum;
          pResult->response = params_tp->arg2.ipcIspTuningMgr.response;
          pResult->bufVa = pTuningDrvBufVa;
        } else {
          rc = MFALSE;
        }
      }
      break;

    case E3ACtrl_IPC_P1_SttControl:
      pMeta1 = reinterpret_cast<IPC_Metabuf1_T*>(i4Arg1);
      if (!pMeta1) {
        IPC_LOGE("Stt Metadata is NULL");
        return MFALSE;
      }
      // need to assign cmd first to order hal3a do related work
      params_sp->arg1.ipcMetaBuf.cmd = pMeta1->cmd;
      if (IPC_Metabuf1_T::cmdENQUE_FROM_DRV == pMeta1->cmd) {
        params_sp->arg1.ipcMetaBuf.magicnum = pMeta1->magicnum;

        auto ipc_handle = mSttIPCHandles.find(pMeta1->bufFd);
        if (ipc_handle == mSttIPCHandles.end()) {
          uint32_t buff_handle = mCommon.registerBuffer(pMeta1->bufFd);
          mSttIPCHandles[pMeta1->bufFd] = buff_handle;
          mMetaBuf1Pool[buff_handle] = *pMeta1;
        }
        params_sp->arg1.ipcMetaBuf.bufFd = mSttIPCHandles[pMeta1->bufFd];
      }
      params_sp->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_STTPIPE, &mMemSttPipe,
                             IPC_GROUP_STTPIPE) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

      // to response p1 whether enque 3A failed or not
      if (IPC_Metabuf1_T::cmdENQUE_FROM_DRV == pMeta1->cmd) {
        pMeta1->response = params_sp->arg1.ipcMetaBuf.response;
      }

      if (IPC_Metabuf1_T::cmdDEQUE_FROM_3A == pMeta1->cmd) {
        pMeta1->cmd = params_sp->arg1.ipcMetaBuf.cmd;
        pMeta1->magicnum = params_sp->arg1.ipcMetaBuf.magicnum;
        pMeta1->response = params_sp->arg1.ipcMetaBuf.response;

        if (pMeta1->response == IPC_Metabuf1_T::responseOK) {
          pMeta1->bufFd = mMetaBuf1Pool[params_sp->arg1.ipcMetaBuf.bufFd].bufFd;
          pMeta1->bufVa = mMetaBuf1Pool[params_sp->arg1.ipcMetaBuf.bufFd].bufVa;
        }
      }
      break;

    case E3ACtrl_IPC_P1_Stt2Control:
      pMeta2 = reinterpret_cast<IPC_Metabuf2_T*>(i4Arg1);
      if (!pMeta2) {
        IPC_LOGE("Stt2 Metadata is NULL");
        return MFALSE;
      }

      params_sp2->arg1.ipcMetaBuf2.cmd = pMeta2->cmd;
      if (IPC_Metabuf2_T::cmdENQUE_FROM_DRV == pMeta2->cmd) {
        params_sp2->arg1.ipcMetaBuf2.magicnum = pMeta2->magicnum;

        auto ipc_handle = mStt2IPCHandles.find(pMeta2->bufFd);
        if (ipc_handle == mStt2IPCHandles.end()) {
          uint32_t buff_handle = mCommon.registerBuffer(pMeta2->bufFd);
          mStt2IPCHandles[pMeta2->bufFd] = buff_handle;
          mMetaBuf2Pool[buff_handle] = *pMeta2;
        }
        params_sp2->arg1.ipcMetaBuf2.bufFd = mStt2IPCHandles[pMeta2->bufFd];
      }

      params_sp2->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_STT2PIPE, &mMemStt2Pipe,
                             IPC_GROUP_STT2PIPE) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

      // to response p1 whether enque 3A failed or not
      if (IPC_Metabuf2_T::cmdENQUE_FROM_DRV == pMeta2->cmd) {
        pMeta2->response = params_sp2->arg1.ipcMetaBuf2.response;
      }

      if (IPC_Metabuf2_T::cmdDEQUE_FROM_3A == pMeta2->cmd) {
        pMeta2->cmd = params_sp2->arg1.ipcMetaBuf2.cmd;
        pMeta2->magicnum = params_sp2->arg1.ipcMetaBuf2.magicnum;
        pMeta2->response = params_sp2->arg1.ipcMetaBuf2.response;

        if (pMeta2->response == IPC_Metabuf2_T::responseOK) {
          pMeta2->bufFd =
              mMetaBuf2Pool[params_sp2->arg1.ipcMetaBuf2.bufFd].bufFd;
          pMeta2->bufVa =
              mMetaBuf2Pool[params_sp2->arg1.ipcMetaBuf2.bufFd].bufVa;
        }
      }
      break;

    case E3ACtrl_IPC_P1_HwSignal:
      evt = reinterpret_cast<v4l2::P1Event*>(i4Arg1);
      if (!evt) {
        IPC_LOGE("Hardware Signal Pointer is NULL");
        return MFALSE;
      }
      memcpy(&params_hwe->arg1.evt, evt, sizeof(v4l2::P1Event));
      params_hwe->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_HWEVENT, &mMemHwEvent,
                             IPC_GROUP_HWEVENT) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      break;

    case E3ACtrl_SetAEPlineLimitation:
      AE_Pline_Limitation_T rLimitParams;
      if (i4Arg1) {
        rLimitParams = *reinterpret_cast<AE_Pline_Limitation_T*>(i4Arg1);
      } else {
        IPC_LOGE("AE Pline Limitation Info is NULL");
        return MFALSE;
      }
      params_pl->e3ACtrl = e3ACtrl;
      params_pl->ipcLimitParams.bEnable = rLimitParams.bEnable;
      params_pl->ipcLimitParams.bEquivalent = rLimitParams.bEquivalent;
      params_pl->ipcLimitParams.u4IncreaseISO_x100 =
          rLimitParams.u4IncreaseISO_x100;
      params_pl->ipcLimitParams.u4IncreaseShutter_x100 =
          rLimitParams.u4IncreaseShutter_x100;

      CheckError(sendRequest(IPC_HAL3A_AEPLINELIMIT, &mMemAePlineLimit,
                             IPC_GROUP_AEPLINELIMIT) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      break;

    case E3ACtrl_IPC_AF_ExchangeLensConfig:
      IPC_LensConfig_T* pLensConfig;
      pLensConfig = reinterpret_cast<IPC_LensConfig_T*>(i4Arg1);
      if (!pLensConfig) {
        IPC_LOGE("Lens Config Info is NULL");
        return MFALSE;
      }

      if (pLensConfig->cmd == IPC_LensConfig_T::ASK_TO_START ||
          pLensConfig->cmd == IPC_LensConfig_T::ASK_TO_STOP) {
        params_le->e3ACtrl = e3ACtrl;
        params_le->lensConfig.cmd = pLensConfig->cmd;
        CheckError(sendRequest(IPC_HAL3A_AFLENS_ENABLE, &mMemAfLensEnable,
                               IPC_GROUP_AF_ENABLE) == false,
                   FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
        pLensConfig->cmd = params_le->lensConfig.cmd;
        pLensConfig->succeeded = params_le->lensConfig.succeeded;
      } else {
        params_lc->e3ACtrl = e3ACtrl;
        params_lc->lensConfig.cmd = pLensConfig->cmd;

        if (params_lc->lensConfig.cmd ==
            IPC_LensConfig_T::ACK_IS_SUPPORT_LENS) {
          params_lc->lensConfig.val.is_support = pLensConfig->val.is_support;
          params_lc->lensConfig.succeeded = pLensConfig->succeeded;
        }

        CheckError(sendRequest(IPC_HAL3A_AFLENSCONFIG, &mMemAfLensConfig,
                               IPC_GROUP_AF) == false,
                   FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

        pLensConfig->cmd = params_lc->lensConfig.cmd;
        pLensConfig->succeeded = params_lc->lensConfig.succeeded;

        if (pLensConfig->cmd == IPC_LensConfig_T::CMD_FOCUS_ABSOULTE)
          pLensConfig->val.focus_pos = params_lc->lensConfig.val.focus_pos;
      }
      break;

    case E3ACtrl_SetEnablePBin:
      params->arg1.enabled = i4Arg1;
      params->arg2.enabled = i4Arg2;
      params->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_SEND3ACTRL, &mMemSend3aCtrl) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      break;

    case E3ACtrl_GetIsAEStable:
      params->e3ACtrl = e3ACtrl;
      CheckError(sendRequest(IPC_HAL3A_SEND3ACTRL, &mMemSend3aCtrl) == false,
                 FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
      *(reinterpret_cast<MUINT32*>(i4Arg1)) = params->arg1.AeStable;
      break;

    default:
      break;
  }
  LOG1("%s e3ACtrl:0x%x ----", __func__, e3ACtrl);
  return rc;
}

MBOOL Hal3AIpcAdapter::notifyP1PwrOn() {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, false, "@%s, init fails", __FUNCTION__);
  CheckError(
      sendRequest(IPC_HAL3A_NOTIFY_P1_PWR_ON, &mMemNofifyP1PwrOn) == false,
      false, "@%s, requestSync fails", __FUNCTION__);
  LOG1("%s ----", __func__);
  return true;
}

MVOID Hal3AIpcAdapter::notifyP1Done(MUINT32 u4MagicNum, MVOID* pvArg) {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, VOID_VALUE, "@%s, init fails",
             __FUNCTION__);
  hal3a_notify_p1_pwr_done_params* params =
      static_cast<hal3a_notify_p1_pwr_done_params*>(mMemNofifyP1Done.mAddr);
  params->u4MagicNum = u4MagicNum;
  CheckError(
      sendRequest(IPC_HAL3A_NOTIFY_P1_PWR_DONE, &mMemNofifyP1Done) == false,
      VOID_VALUE, "@%s, requestSync fails", __FUNCTION__);
  LOG1("%s ----", __func__);
  return;
}

MBOOL Hal3AIpcAdapter::notifyP1PwrOff() {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, false, "@%s, init fails", __FUNCTION__);
  CheckError(
      sendRequest(IPC_HAL3A_NOTIFY_P1_PWR_OFF, &mMemNofifyP1PwrOff) == false,
      false, "@%s, requestSync fails", __FUNCTION__);
  LOG1("%s ----", __func__);
  return true;
}

MVOID Hal3AIpcAdapter::setSensorMode(MINT32 i4SensorMode) {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, VOID_VALUE, "@%s, init fails",
             __FUNCTION__);
  hal3a_set_sensor_mode_params* params =
      static_cast<hal3a_set_sensor_mode_params*>(mMemSetSensorMode.mAddr);
  params->i4SensorMode = i4SensorMode;

  CheckError(
      sendRequest(IPC_HAL3A_SET_SENSOR_MODE, &mMemSetSensorMode) == false,
      VOID_VALUE, "@%s, requestSync fails", __FUNCTION__);
  LOG1("%s ----", __func__);
  return;
}

MINT32 Hal3AIpcAdapter::attachCb(IHal3ACb::ECb_T eId, IHal3ACb* pCb) {
  int rc;
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  hal3a_attach_cb_params* params =
      static_cast<hal3a_attach_cb_params*>(mMemAttachCB.mAddr);
  params->eId = eId;

  LOG1("%s eId:%d ++++ ", __func__, eId);
  rc = m_CbSet[eId].addCallback(pCb);
  if (rc < 0)
    return rc;

  CheckError(sendRequest(IPC_HAL3A_ATTACH_CB, &mMemAttachCB) == false,
             FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);
  LOG1("%s eId:%d ---- ", __func__, eId);

  return 0;
}

MINT32 Hal3AIpcAdapter::detachCb(IHal3ACb::ECb_T eId, IHal3ACb* pCb) {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  hal3a_detach_cb_params* params =
      static_cast<hal3a_detach_cb_params*>(mMemDetachCB.mAddr);
  params->eId = eId;
  params->pCb = pCb;

  CheckError(sendRequest(IPC_HAL3A_DETACH_CB, &mMemDetachCB) == false,
             FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  m_CbSet[eId].removeCallback(pCb);
  LOG1("%s ----", __func__);
  return 0;
}

void Hal3AIpcAdapter::runCallback(int msg) {
  CheckError(mInitialized == false, VOID_VALUE, "@%s, init fails",
             __FUNCTION__);
  hal3a_attach_cb_params* params =
      static_cast<hal3a_attach_cb_params*>(mMemAttachCB.mAddr);
  msg = params->eId;

  m_CbSet[msg].doNotifyCb(msg, params->cb_result[msg]._ext1,
                          params->cb_result[msg]._ext2,
                          params->cb_result[msg]._ext3);

  LOG1("%s msg:%d", __func__, msg);
}

MINT32 Hal3AIpcAdapter::get(MUINT32 frmId, MetaSet_T* result) {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  hal3a_get_params* params = static_cast<hal3a_get_params*>(mMemGet.mAddr);
  params->frmId = frmId;
  CheckError(sendRequest(IPC_HAL3A_GET, &mMemGet, IPC_GROUP_GET) == false,
             FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  /* unflatten result3A to P1node*/
  result->MagicNum = params->result.MagicNum;
  result->Dummy = params->result.Dummy;
  result->PreSetKey = params->result.PreSetKey;
  ssize_t appSize = result->appMeta.unflatten(&params->appMetaBuffer,
                                              sizeof(params->appMetaBuffer));
  ssize_t halSize = result->halMeta.unflatten(&params->halMetaBuffer,
                                              sizeof(params->halMetaBuffer));
  if (appSize < 0 || halSize < 0) {
    if (appSize < 0)
      IPC_LOGE("App Metadata unflatten failed");
    if (halSize < 0)
      IPC_LOGE("Hal Metadata unflatten failed");
    return -1;
  }

  LOG1("%s client: appSize = %zd", __func__, appSize);
  LOG1("%s client: halSize = %zd", __func__, halSize);
  LOG1("%s ----", __func__);
  return params->get_ret;
}

MINT32 Hal3AIpcAdapter::getCur(MUINT32 frmId, MetaSet_T* result) {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  hal3a_get_cur_params* params =
      static_cast<hal3a_get_cur_params*>(mMemGetCur.mAddr);
  params->frmId = frmId;

  CheckError(sendRequest(IPC_HAL3A_GET_CUR, &mMemGetCur) == false,
             FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  /* unflatten result3a here. */
  result->MagicNum = params->result.MagicNum;
  result->Dummy = params->result.Dummy;
  result->PreSetKey = params->result.PreSetKey;
  ssize_t appSize = result->appMeta.unflatten(&params->appMetaBuffer,
                                              sizeof(params->appMetaBuffer));
  ssize_t halSize = result->halMeta.unflatten(&params->halMetaBuffer,
                                              sizeof(params->halMetaBuffer));
  if (appSize < 0 || halSize < 0) {
    if (appSize < 0)
      IPC_LOGE("App Metadata unflatten failed");
    if (halSize < 0)
      IPC_LOGE("Hal Metadata unflatten failed");
    return -1;
  }

  LOG1("%s client: appSize = %zd", __func__, appSize);
  LOG1("%s client: halSize = %zd", __func__, halSize);
  LOG1("%s ----", __func__);
  return params->get_cur_ret;
}

MBOOL Hal3AIpcAdapter::setFDInfoOnActiveArray(MVOID* prFaces) {
  LOG1("%s ++++", __func__);
  CheckError(mInitialized == false, NO_INIT, "@%s, init fails", __FUNCTION__);
  hal3a_set_fdInfo_params* params =
      static_cast<hal3a_set_fdInfo_params*>(mMemSetFDInfo.mAddr);

  MtkCameraFaceMetadata* rFaceMeta =
      reinterpret_cast<MtkCameraFaceMetadata*>(prFaces);
  memcpy(&(params->detectFace), rFaceMeta, sizeof(MtkCameraFaceMetadata));
  for (int i = 0; i < rFaceMeta->number_of_faces; i++) {
    memcpy(&(params->faceDetectInfo[i]), &(rFaceMeta->faces[i]),
           sizeof(MtkCameraFace));
    memcpy(&(params->facePoseInfo[i]), &(rFaceMeta->posInfo[i]),
           sizeof(MtkFaceInfo));
  }
  CheckError(
      sendRequest(IPC_HAL3A_SET_FDINFO, &mMemSetFDInfo, IPC_GROUP_FD) == false,
      FAILED_TRANSACTION, "@%s, requestSync fails", __FUNCTION__);

  LOG1("%s ----", __func__);
  return MTRUE;
}

Hal3ACbSet::Hal3ACbSet() : m_Mutex() {
  std::lock_guard<std::mutex> autoLock(m_Mutex);
  m_CallBacks.clear();
}

Hal3ACbSet::~Hal3ACbSet() {
  std::lock_guard<std::mutex> autoLock(m_Mutex);
  m_CallBacks.clear();
}

void Hal3ACbSet::doNotifyCb(MINT32 _msgType,
                            MINTPTR _ext1,
                            MINTPTR _ext2,
                            MINTPTR _ext3) {
  std::lock_guard<std::mutex> autoLock(m_Mutex);

  LOG1("[Hal3ACbSet::%s] _msgType(%d), _ext1(%d), _ext2(%d), _ext3(%d)",
       __FUNCTION__, _msgType, _ext1, _ext2, _ext3);

  for (auto const& pCb : m_CallBacks) {
    if (pCb)
      pCb->doNotifyCb(_msgType, _ext1, _ext2, _ext3);
  }
}

MINT32 Hal3ACbSet::addCallback(IHal3ACb* cb) {
  std::lock_guard<std::mutex> autoLock(m_Mutex);
  LOG1("[%s] %p callback! ++++", __FUNCTION__, cb);

  if (!cb) {
    LOG1("[%s] NULL callback!", __FUNCTION__);
    return INVALID_OPERATION;
  }

  for (auto const& pCb : m_CallBacks) {
    if (cb == pCb) {
      LOG1("[%s] Callback already exists!", __FUNCTION__);
      return ALREADY_EXISTS;
    }
  }

  m_CallBacks.push_back(cb);
  LOG1("[%s] %p callback! ----", __FUNCTION__, cb);
  return m_CallBacks.size();
}

MINT32 Hal3ACbSet::removeCallback(IHal3ACb* cb) {
  std::lock_guard<std::mutex> autoLock(m_Mutex);

  if (!cb) {
    LOG1("[%s] NULL callback!", __FUNCTION__);
    return INVALID_OPERATION;
  }

  for (std::list<IHal3ACb*>::iterator it = m_CallBacks.begin();
       it != m_CallBacks.end(); it++) {
    if (cb == *it) {
      m_CallBacks.erase(it);
      return m_CallBacks.size();
    }
  }

  LOG1("[%s] No such callback, remove failed", __FUNCTION__);
  return NAME_NOT_FOUND;
}

VISIBILITY_PUBLIC
extern "C" NS3Av3::IHal3A* createInstance_Hal3A_Client(MINT32 const i4SensorIdx,
                                                       const char* strUser) {
  return Hal3AIpcAdapter::getInstance(i4SensorIdx, strUser);
}

}  // namespace NS3Av3
