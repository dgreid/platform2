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

#define LOG_TAG "Mediatek3AServerAdapter"

#include <dlfcn.h>
#include <errno.h>
#include <Errors.h>
#include <fcntl.h>
#include <memory>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <vector>

#include <mtkcam/utils/hw/HwTransform.h>
#include <mtkcam/utils/imgbuf/IImageBuffer.h>
#include <mtkcam/utils/imgbuf/IDummyImageBufferHeap.h>
#include <mtkcam/v4l2/IPCIHalSensor.h>
#include <mtkcam/v4l2/mtk_p1_metabuf.h>

#include "IPCHal3a.h"
#include "IPCCommon.h"
#include "Mediatek3AServer.h"

#include <camera/hal/mediatek/mtkcam/ipc/server/Hal3aIpcServerAdapter.h>

using NSCam::IImageBuffer;
using NSCam::IIPCHalSensor;
using NSCam::IIPCHalSensorList;

namespace NS3Av3 {

int Hal3aIpcServerAdapter::hal3a_server_parsing_sensor_idx(void* addr) {
  hal3a_common_params* params = static_cast<hal3a_common_params*>(addr);
  if (!params) {
    IPC_LOGE("Common Params for Sensor Info is NULL");
    return -1;
  }
  return params->m_i4SensorIdx;
}

int Hal3aIpcServerAdapter::hal3a_server_metaset_unflatten(
    struct hal3a_metaset_params* params,
    MetaSet_T* metaSet,
    std::vector<MetaSet_T*>* requestQ) {
  metaSet->MagicNum = params->MagicNum;
  metaSet->Dummy = params->Dummy;
  metaSet->PreSetKey = params->PreSetKey;

  ssize_t appSize = metaSet->appMeta.unflatten(&params->appMetaBuffer,
                                               sizeof(params->appMetaBuffer));
  ssize_t halSize = metaSet->halMeta.unflatten(&params->halMetaBuffer,
                                               sizeof(params->halMetaBuffer));

  if (appSize < 0 || halSize < 0) {
    if (appSize < 0)
      IPC_LOGE("AppMeta data unflatten failed");
    if (halSize < 0)
      IPC_LOGE("HalMeta data unflatten failed");
    return -1;
  }

  requestQ->push_back(metaSet);

  return OK;
}

int Hal3aIpcServerAdapter::init(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_init_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);

  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;

  LOG1("%s sensor idx0x%x ++++", __func__, sensor_index);

  MAKE_Hal3A_IPC(
      mpHal3A[sensor_index],
      [](IHal3A* p) { p->destroyInstance("Hal3aIpcServerAdapter"); },
      sensor_index, "Hal3aIpcServerAdapter");

  m_pLceImgBuf = NULL;

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);
  return OK;
}

int Hal3aIpcServerAdapter::uninit(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_init_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);

  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;

  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);
  mpHal3A[sensor_index] = nullptr;

  if (m_pLceImgBuf) {
    m_pLceImgBuf->unlockBuf("LCS_P2_CPU");
    m_pLceImgBuf = nullptr;
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);

  return OK;
}

int Hal3aIpcServerAdapter::config(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_config_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);

  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  hal3a_config_params* config_params = static_cast<hal3a_config_params*>(addr);
  if (!config_params) {
    IPC_LOGE("Config Shared Buffer is NULL");
    return -1;
  }

  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);

  ConfigInfo_T rConfigInfo;
  rConfigInfo.i4SubsampleCount = config_params->rConfigInfo.i4SubsampleCount;
  rConfigInfo.i4BitMode = config_params->rConfigInfo.i4BitMode;
  rConfigInfo.i4HlrOption = config_params->rConfigInfo.i4HlrOption;
  ssize_t cfgHalSize = rConfigInfo.CfgHalMeta.unflatten(
      &config_params->CfgHalMeta, sizeof(config_params->CfgHalMeta));
  ssize_t cfgAppSize = rConfigInfo.CfgAppMeta.unflatten(
      &config_params->CfgAppMeta, sizeof(config_params->CfgAppMeta));
  if (cfgHalSize < 0 || cfgAppSize < 0) {
    if (cfgHalSize < 0)
      IPC_LOGE("CfgHalMeta data unflatten failed");
    if (cfgAppSize < 0)
      IPC_LOGE("CfgAppMeta data unflatten failed");
    return -1;
  }

  /* getMatrix to active and from active here */
  rConfigInfo.matFromAct = config_params->rConfigInfo.matFromAct;
  rConfigInfo.matToAct = config_params->rConfigInfo.matToAct;

  MINT32 i4Ret;
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    i4Ret = mpHal3A[sensor_index]->config(rConfigInfo);
    if (!i4Ret) {
      IPC_LOGE("Config Failed in Hal3A");
      return -1;
    }
  }

  LOG1("i4SubsampleCount:%d, i4BitMode:%d", rConfigInfo.i4SubsampleCount,
       rConfigInfo.i4BitMode);
  LOG1("%s sensor idx:%d ----", __func__, sensor_index);

  return OK;
}

int Hal3aIpcServerAdapter::start(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_start_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);

  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d start++++", __func__, sensor_index);

  hal3a_start_params* start_params = static_cast<hal3a_start_params*>(addr);
  if (!start_params) {
    IPC_LOGE("Start Shared Buffer is NULL");
    return -1;
  }

  MINT32 i4Ret;
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    i4Ret = mpHal3A[sensor_index]->start(start_params->i4StartNum);
    if (i4Ret) {
      IPC_LOGE("Start Failed in Hal3A");
      return -1;
    }
  }

  LOG1("%s sensor idx:%d, i4StartNum:%d start----", __func__, sensor_index,
       start_params->i4StartNum);

  return OK;
}

int Hal3aIpcServerAdapter::stop(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_stop_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);

  MINT32 i4Ret;
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    i4Ret = mpHal3A[sensor_index]->stop();
    if (i4Ret) {
      IPC_LOGE("Stop Failed in Hal3A");
      return -1;
    }
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);

  return OK;
}

int Hal3aIpcServerAdapter::stopStt(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_stop_stt_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);

  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    mpHal3A[sensor_index]->stopStt();
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);

  return OK;
}

int Hal3aIpcServerAdapter::set(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_set_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);

  hal3a_set_params* params = static_cast<hal3a_set_params*>(addr);
  if (!params) {
    IPC_LOGE("Set Shared Buffer is NULL");
    return -1;
  }

  int ret = 0;
  std::vector<MetaSet_T*> transfer;
  MetaSet_T metaSet;
  ret = hal3a_server_metaset_unflatten(&params->requestQ, &metaSet, &transfer);
  if (ret < 0)
    return -1;

  MINT32 i4Ret;
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    i4Ret = mpHal3A[sensor_index]->set(transfer);
    if (i4Ret) {
      IPC_LOGE("Set Failed in Hal3A");
      return -1;
    }
  }

  LOG1("%s sensor idx:%d MagicNum:%d ----", __func__, sensor_index,
       transfer[0]->MagicNum);
  return OK;
}

/*********************************************************************/
/*                                                                   */
/*  setIsp() is to get the HW setting for pass2 hw tuning register.  */
/*  LCE is for Local Contrast Enhancement. */
/*  LSC is for Lens shading compensation. */
/*  LCE buffer contains raw data needed by LCE algorithm to calculate LCE
 * setting.  */
/*  LSC buffer is for capture use when pass2 handles raw to yuv/jpeg. */
/*                                                                    */
/**********************************************************************/
int Hal3aIpcServerAdapter::setIsp(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_setisp_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);
  MetaSet_T inMeta;
  TuningParam tuneBuf;
  MetaSet_T outMeta;
  hal3a_setisp_params* params = static_cast<hal3a_setisp_params*>(addr);
  if (!params) {
    IPC_LOGE("SetIsp Shared Buffer is NULL");
    return -1;
  }

  /* unflatten inMeta here*/
  inMeta.MagicNum = params->control.MagicNum;
  inMeta.Dummy = params->control.Dummy;
  inMeta.PreSetKey = params->control.PreSetKey;
  ssize_t inAppSize = inMeta.appMeta.unflatten(&params->inAppMetaBuffer,
                                               sizeof(params->inAppMetaBuffer));
  LOG1("%s server: inAppSize = %zd", __func__, inAppSize);
  ssize_t inHalSize = inMeta.halMeta.unflatten(&params->inHalMetaBuffer,
                                               sizeof(params->inHalMetaBuffer));
  LOG1("%s server: inHalSize = %zd", __func__, inHalSize);
  if (inAppSize < 0 || inHalSize < 0) {
    if (inAppSize < 0)
      IPC_LOGE("inAppMeta data unflatten failed");
    if (inHalSize < 0)
      IPC_LOGE("inHalMeta data unflatten failed");
    return -1;
  }

  /* point pRegBuf to shared buffer */
  tuneBuf.pRegBuf = reinterpret_cast<void*>(params->p2tuningbuf_va);

  /* handle LCE imageBuf */
  if (params->u4LceEnable == 1) {
    if (m_pLceImgBuf) {
      m_pLceImgBuf->unlockBuf("LCS_P2_CPU");
      m_pLceImgBuf = nullptr;
    }

    IPCImageBufAllocator::config cfg = {};
    cfg.format = params->lceBufInfo.imgFormat;
    cfg.width = params->lceBufInfo.width;
    cfg.height = params->lceBufInfo.height;
    cfg.planecount = params->lceBufInfo.planeCount;
    cfg.strides[0] = params->lceBufInfo.bufStrides[0];
    cfg.scanlines[0] = params->lceBufInfo.bufScanlines[0];
    cfg.va[0] = params->lceBufInfo.bufVA[0];
    cfg.pa[0] = params->lceBufInfo.bufPA[0];
    cfg.fd[0] = params->lceBufInfo.fd[0];
    IPCImageBufAllocator allocator(cfg, "LCS_P2");
    std::shared_ptr<IImageBuffer> pImgBuf = allocator.createImageBuffer();
    pImgBuf->lockBuf("LCS_P2_CPU");
    tuneBuf.pLcsBuf = pImgBuf.get();
    m_pLceImgBuf = pImgBuf;

    LOG1("%s LCE: va=%#" PRIxPTR "", __func__, params->lceBufInfo.bufVA[0]);
    LOG1("%s LCE: u4LceEnable = %d", __func__, params->u4LceEnable);
    LOG1("%s LCE: imgFormat = %d", __func__, params->lceBufInfo.imgFormat);
    LOG1("%s LCE: width = %d", __func__, params->lceBufInfo.width);
    LOG1("%s LCE: height = %d", __func__, params->lceBufInfo.height);
    LOG1("%s LCE: bufStrides = %d", __func__, params->lceBufInfo.bufStrides[0]);
    LOG1("%s LCE: fd = %d", __func__, params->lceBufInfo.fd[0]);
  } else {
    tuneBuf.pLcsBuf = NULL;
  }

  MINT32 i4Ret;
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    i4Ret = mpHal3A[sensor_index]->setIsp(params->flowType, inMeta, &tuneBuf,
                                          &outMeta);
    if (i4Ret) {
      IPC_LOGE("SetIsp Failed in Hal3A");
      return -1;
    }
  }

  /*************************************************************/
  /*                 Update LCSO Buffer Info                   */
  /* --------------------------------------------------------- */
  /* The input LCE buffer length is the maximum value.         */
  /* The LCE algorithm would return the actual active region.  */
  /* Pass2 need update for correct HW setting.                 */
  /*                                                           */
  /*************************************************************/
  if (params->u4LceEnable == 1) {
    params->lceBufInfo.width = m_pLceImgBuf->getImgSize().w;
    params->lceBufInfo.height = m_pLceImgBuf->getImgSize().h;
  }

  /* handle LSC2 buffer */
  params->u4Lsc2Enable = 0;
  if (tuneBuf.pLsc2Buf != NULL) {
    IImageBuffer* pLSC2 = reinterpret_cast<IImageBuffer*>(tuneBuf.pLsc2Buf);
    params->u4Lsc2Enable = 1;
    params->lsc2BufInfo.imgFormat = pLSC2->getImgFormat();
    params->lsc2BufInfo.imgBits = pLSC2->getImgBitsPerPixel();
    params->lsc2BufInfo.width = pLSC2->getImgSize().w;
    params->lsc2BufInfo.height = pLSC2->getImgSize().h;
    params->lsc2BufInfo.planeCount = pLSC2->getPlaneCount();
    for (MINT i = 0; i < params->lsc2BufInfo.planeCount; i++) {
      params->lsc2BufInfo.bufStrides[i] = pLSC2->getBufStridesInBytes(i);
      params->lsc2BufInfo.bufScanlines[i] = pLSC2->getBufScanlines(i);
      params->lsc2BufInfo.bufPA[i] = 0;
      params->lsc2BufInfo.bufStridesPixel[i] = pLSC2->getBufStridesInPixel(i);
      params->lsc2BufInfo.bufSize[i] = pLSC2->getBufSizeInBytes(i);
    }
    params->lsc2BufInfo.fd[0] = pLSC2->getFD(0);
    params->lsc2BufInfo.bufVA[0] = pLSC2->getBufVA(0);
    char* pLsc2ContOut = reinterpret_cast<char*>(params->lsc2BufInfo.bufVA[0]);
    MUINT32 copySize = (pLSC2->getImgSize().w) * (pLSC2->getImgSize().h);
    LOG1("%s shading: copySize = %d", __func__, copySize);
    memcpy(params->pLsc2BufCont, pLsc2ContOut, copySize);
  }

  /* flatten outMeta here*/
  params->metaSetResult.MagicNum = outMeta.MagicNum;
  params->metaSetResult.Dummy = outMeta.Dummy;
  params->metaSetResult.PreSetKey = outMeta.PreSetKey;
  ssize_t outAppSize = outMeta.appMeta.flatten(
      &params->outAppMetaBuffer, sizeof(params->outAppMetaBuffer));
  LOG1("%s server: outAppSize = %zd", __func__, outAppSize);
  ssize_t outHalSize = outMeta.halMeta.flatten(
      &params->outHalMetaBuffer, sizeof(params->outHalMetaBuffer));
  LOG1("%s server: outHalSize = %zd", __func__, outHalSize);
  if (outAppSize < 0 || outHalSize < 0) {
    if (outAppSize < 0)
      IPC_LOGE("outAppMeta data unflatten failed");
    if (outHalSize < 0)
      IPC_LOGE("outHalMeta data unflatten failed");
    return -1;
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);
  return OK;
}

int Hal3aIpcServerAdapter::startRequestQ(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_start_requestq_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);

  hal3a_start_requestq_params* params =
      static_cast<hal3a_start_requestq_params*>(addr);
  if (!params) {
    IPC_LOGE("Start Request Q Shared Buffer is NULL");
    return -1;
  }

  MetaSet_T metaSet;
  int ret = 0;
  std::vector<MetaSet_T*> transfer;
  ret = hal3a_server_metaset_unflatten(&params->requestQ, &metaSet, &transfer);
  if (ret < 0)
    return -1;

  MINT32 i4Ret;
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    i4Ret = mpHal3A[sensor_index]->startRequestQ(transfer);
    if (i4Ret) {
      IPC_LOGE("startRequestQ Failed in Hal3A");
      return -1;
    }
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);

  return OK;
}

int Hal3aIpcServerAdapter::startCapture(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_start_capture_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);

  hal3a_start_capture_params* params =
      static_cast<hal3a_start_capture_params*>(addr);
  if (!params) {
    IPC_LOGE("Start Capture Shared Buffer is NULL");
    return -1;
  }

  MetaSet_T metaSet;
  int ret = 0;
  std::vector<MetaSet_T*> transfer;
  ret = hal3a_server_metaset_unflatten(&params->requestQ, &metaSet, &transfer);
  if (ret < 0)
    return -1;

  MINT32 i4Ret;
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    i4Ret = mpHal3A[sensor_index]->startCapture(transfer);
    if (i4Ret < 0 || i4Ret > 2) {
      IPC_LOGE("startCapture Failed in Hal3A");
      return -1;
    }
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);

  return OK;
}

int Hal3aIpcServerAdapter::preset(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_preset_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);

  hal3a_preset_params* params = static_cast<hal3a_preset_params*>(addr);
  if (!params) {
    IPC_LOGE("Preset Buffer is NULL");
    return -1;
  }

  MetaSet_T metaSet;
  int ret = 0;
  std::vector<MetaSet_T*> transfer;
  ret = hal3a_server_metaset_unflatten(&params->requestQ, &metaSet, &transfer);
  if (ret < 0)
    return -1;

  MINT32 i4Ret;
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    i4Ret = mpHal3A[sensor_index]->preset(transfer);
    if (i4Ret) {
      IPC_LOGE("Preset Failed in Hal3A");
      return -1;
    }
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);

  return OK;
}

MINT32 Hal3aIpcServerAdapter::getSensorParam(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_getsensorparam_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  hal3a_getsensorparam_params* params =
      static_cast<hal3a_getsensorparam_params*>(addr);
  if (!params) {
    IPC_LOGE("Get Sensor Parameter is NULL");
    return -1;
  }

  static int sensor_param_status = 0;
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d e3ACtrl:getSensorParam 0x%x ++++", __func__,
       sensor_index, params->e3ACtrl);

  switch (params->e3ACtrl) {
    case E3ACtrl_IPC_AE_GetSensorParamEnable:
      sensor_param_status = params->arg1.enabled;
      if (!mpHal3A[sensor_index]->send3ACtrl(params->e3ACtrl,
                                             params->arg1.enabled, 0)) {
        IPC_LOGE("%s Result from GetSensorParamEnable is Failed", __func__);
        return -1;
      }
      LOG1("E3ACtrl_IPC_AE_GetSensorParamEnable enabled:%d",
           params->arg1.enabled);
      break;
    case E3ACtrl_IPC_AE_GetSensorParam:
      if (!mpHal3A[sensor_index]->send3ACtrl(
              params->e3ACtrl, reinterpret_cast<MINTPTR>(&params->arg1),
              params->arg2.timeoutMs)) {
        if (sensor_param_status == 0) {
          return 0;
        }
        IPC_LOGE("%s Result from GetSensorParam is Failed", __func__);
        return -1;
      }
      LOG1("E3ACtrl_IPC_AE_GetSensorParam timeout:%d", params->arg2.timeoutMs);
      LOG1("E3ACtrl_IPC_AE_GetSensorParam cmd:%d",
           params->arg1.ipcSensorParam.cmd);
      LOG1("E3ACtrl_IPC_AE_GetSensorParam sensorIdx:0x%x",
           params->arg1.ipcSensorParam.sensorIdx);
      LOG1("E3ACtrl_IPC_AE_GetSensorParam sensorDev:0x%x",
           params->arg1.ipcSensorParam.sensorDev);
      LOG1("E3ACtrl_IPC_AE_GetSensorParam a_frameRate:%d",
           params->arg1.ipcSensorParam.p2.a_frameRate);
      break;
    default:
      IPC_LOGE("%s Not Surpport This Send3ACtrl Commend", __func__);
      return -1;
      break;
  }

  LOG1("%s sensor idx:%d e3ACtrl:getSensorParam 0x%x ----", __func__,
       sensor_index, params->e3ACtrl);

  return OK;
}

MINT32 Hal3aIpcServerAdapter::notifyCallBack(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_notifycallback_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  MINT32* vectorData = nullptr;
  ssize_t metaSize = 0;
  IPC_P1NotifyCb_T serverP1NotifyCb;
  hal3a_notifycallback_params* params =
      static_cast<hal3a_notifycallback_params*>(addr);
  if (!params) {
    IPC_LOGE("Notify Callback Parameter is NULL");
    return -1;
  }

  static int callback_status = 0;
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d e3ACtrl:notifyCallBack 0x%x ++++", __func__,
       sensor_index, params->e3ACtrl);

  switch (params->e3ACtrl) {
    case E3ACtrl_IPC_P1_NotifyCbEnable:
      // hal3a no return value
      callback_status = params->arg1.enabled;
      mpHal3A[sensor_index]->send3ACtrl(params->e3ACtrl, params->arg1.enabled,
                                        0);
      LOG1("E3ACtrl_IPC_P1_NotifyCbEnable enabled:%d", params->arg1.enabled);
      break;
    case E3ACtrl_IPC_P1_NotifyCb:
      params->callback_ret = mpHal3A[sensor_index]->send3ACtrl(
          params->e3ACtrl, params->arg1.enabled,
          reinterpret_cast<MINTPTR>(&serverP1NotifyCb));

      if (params->arg1.enabled ==
              NS3Av3::IPC_P1NotifyCb_T::WAIT_3A_PROC_FINISHED &&
          params->callback_ret) {
        // flatten
        params->pu4CapType = serverP1NotifyCb.u4CapType;
        params->pmagicnum = serverP1NotifyCb.u.proc_finish.magicnum;

        if (params->pu4CapType == IHal3ACb::eID_NOTIFY_3APROC_FINISH) {
          /* RequestSet_T */
          vectorData = serverP1NotifyCb.u.proc_finish.pRequestResult->vNumberSet
                           .data();  // copy vector data
          if (!vectorData) {
            IPC_LOGE("%s Vector Data is NULL", __func__);
            return -1;
          }
          params->pRvNumberSet = *vectorData;
          params->pRfgKeep = serverP1NotifyCb.u.proc_finish.pRequestResult
                                 ->fgKeep;  // copy fgKeep
          params->pRfgDisableP1 = serverP1NotifyCb.u.proc_finish.pRequestResult
                                      ->fgDisableP1;  // copy fgDisableP1
          /* CapParam_T */
          params->pCu4CapType = serverP1NotifyCb.u.proc_finish.pCapParam
                                    ->u4CapType;  // copy u4CapType
          params->pCi8ExposureTime =
              serverP1NotifyCb.u.proc_finish.pCapParam
                  ->i8ExposureTime;  // copy i8ExposureTime
          metaSize = serverP1NotifyCb.u.proc_finish.pCapParam->metadata.flatten(
              params->pCmetadata, sizeof(params->pCmetadata));
          if (metaSize < 0) {
            IPC_LOGE("%s Meta data flatten failed", __func__);
            return -1;
          }
          LOG1("flatten IPC_Param_3AProc_Finish, metaSize size = %zd",
               metaSize);
        }
      }

      if (!params->callback_ret) {
        if (callback_status) {
          IPC_LOGE("%s Result from P1 NotifyCb is Failed", __func__);
          return -1;
        } else
          return 0;
      }
      LOG1("E3ACtrl_IPC_P1_NotifyCb ack:%d", params->arg1.enabled);
      break;
    default:
      IPC_LOGE("%s Not Surpport This Send3ACtrl Commend", __func__);
      return -1;
      break;
  }

  LOG1("%s sensor idx:%d e3ACtrl:notifyCallBack 0x%x ----", __func__,
       sensor_index, params->e3ACtrl);

  return OK;
}

MINT32 Hal3aIpcServerAdapter::tuningPipe(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_tuningpipe_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  IPC_IspTuningMgr_T tuning;
  int flag;
  hal3a_tuningpipe_params* params = static_cast<hal3a_tuningpipe_params*>(addr);
  if (!params) {
    IPC_LOGE("Tuning Pipe Shared Buffer is NULL");
    return -1;
  }

  static int tuning_req_status = 0;
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d e3ACtrl: tuningPipe 0x%x ++++", __func__, sensor_index,
       params->e3ACtrl);

  switch (params->e3ACtrl) {
    case E3ACtrl_IPC_P1_WaitTuningReq:
      // P1nodeImp just use magicnum and response so now no handle bufVa
      if (IPC_IspTuningMgr_T::cmdTERMINATED == params->arg1.cmd)
        tuning_req_status = 0;
      else if (IPC_IspTuningMgr_T::cmdREVIVE == params->arg1.cmd)
        tuning_req_status = 1;
      if (!mpHal3A[sensor_index]->send3ACtrl(
              params->e3ACtrl, params->arg1.cmd,
              reinterpret_cast<MUINTPTR>(&params->arg2.ipcIspTuningMgr))) {
        if (tuning_req_status) {
          IPC_LOGE("%s Result from P1 WaitTuningReq is Failed", __func__);
          return -1;
        } else
          return 0;
      }
      LOG1("E3ACtrl_IPC_P1_WaitTuningReq arg1.cmd: %d", params->arg1.cmd);
      LOG1("E3ACtrl_IPC_P1_WaitTuningReq arg2.ipcIspTuningMgr.magicnum: %d",
           params->arg2.ipcIspTuningMgr.magicnum);
      LOG1("E3ACtrl_IPC_P1_WaitTuningReq arg2.ipcIspTuningMgr.response: %d",
           params->arg2.ipcIspTuningMgr.response);
      break;
    case E3ACtrl_IPC_P1_ExchangeTuningBuf:
      if (IPC_IspTuningMgr_T::cmdACQUIRE_FROM_FMK == params->arg1.cmd) {
        tuning.magicnum = params->arg2.ipcIspTuningMgr.magicnum;
        tuning.response = params->arg2.ipcIspTuningMgr.response;
        tuning.bufVa = params->p1tuningbuf_va;

        LOG1("E3ACtrl_IPC_P1_ExchangeTuningBuf arg1.cmd: %d", params->arg1.cmd);
        LOG1(
            "E3ACtrl_IPC_P1_ExchangeTuningBuf arg2.ipcIspTuningMgr.magicnum: "
            "%d",
            tuning.magicnum);
        LOG1(
            "E3ACtrl_IPC_P1_ExchangeTuningBuf arg2.ipcIspTuningMgr.response: "
            "%d",
            tuning.response);
      }

      flag = mpHal3A[sensor_index]->send3ACtrl(
          params->e3ACtrl, params->arg1.cmd,
          reinterpret_cast<MUINTPTR>(&tuning));
      params->flag = flag;

      if (IPC_IspTuningMgr_T::cmdRESULT_FROM_FMK == params->arg1.cmd) {
        params->arg2.ipcIspTuningMgr.magicnum = tuning.magicnum;
        params->arg2.ipcIspTuningMgr.response = tuning.response;

        LOG1("E3ACtrl_IPC_P1_ExchangeTuningBuf arg1.cmd: %d", params->arg1.cmd);
        LOG1(
            "E3ACtrl_IPC_P1_ExchangeTuningBuf arg2.ipcIspTuningMgr.magicnum: "
            "%d",
            tuning.magicnum);
        LOG1(
            "E3ACtrl_IPC_P1_ExchangeTuningBuf arg2.ipcIspTuningMgr.response: "
            "%d",
            tuning.response);
        LOG1("E3ACtrl_IPC_P1_ExchangeTuningBuf flag: %d", params->flag);
      }
      break;
    default:
      IPC_LOGE("%s Not Surpport This Send3ACtrl Commend", __func__);
      return -1;
      break;
  }

  LOG1("%s sensor idx:%d e3ACtrl: tuningPipe 0x%x ----", __func__, sensor_index,
       params->e3ACtrl);

  return OK;
}

MINT32 Hal3aIpcServerAdapter::sttPipe(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_sttpipe_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  IPC_Metabuf1_T meta1;
  hal3a_sttpipe_params* params = static_cast<hal3a_sttpipe_params*>(addr);
  if (!params) {
    IPC_LOGE("Stt Pipe Shared Buffer is NULL");
    return -1;
  }

  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d e3ACtrl:sttPipe 0x%x ++++", __func__, sensor_index,
       params->e3ACtrl);

  if (params->e3ACtrl == E3ACtrl_IPC_P1_SttControl) {
    // need to assign cmd first to order hal3a do related work
    meta1.cmd = params->arg1.ipcMetaBuf.cmd;
    if (IPC_Metabuf1_T::cmdENQUE_FROM_DRV == params->arg1.ipcMetaBuf.cmd) {
      meta1.magicnum = params->arg1.ipcMetaBuf.magicnum;
      meta1.bufVa = params->arg1.ipcMetaBuf.bufVa;

      LOG1("E3ACtrl_IPC_P1_SttControl ipcMetaBuf.cmd:0x%x", meta1.cmd);
      LOG1("E3ACtrl_IPC_P1_SttControl ipcMetaBuf.magicnum:%d", meta1.magicnum);
    }

    // error handling implement on response member variable
    mpHal3A[sensor_index]->send3ACtrl(params->e3ACtrl,
                                      reinterpret_cast<MUINTPTR>(&meta1), 0);

    // to response p1 whether enque 3A failed or not
    if (IPC_Metabuf1_T::cmdENQUE_FROM_DRV == params->arg1.ipcMetaBuf.cmd) {
      params->arg1.ipcMetaBuf.response = meta1.response;
      LOG1("E3ACtrl_IPC_P1_SttControl ipcMetaBuf.response:%d", meta1.response);
    }

    if (IPC_Metabuf1_T::cmdDEQUE_FROM_3A == params->arg1.ipcMetaBuf.cmd) {
      params->arg1.ipcMetaBuf.magicnum = meta1.magicnum;
      params->arg1.ipcMetaBuf.response = meta1.response;

      if (params->arg1.ipcMetaBuf.response == IPC_Metabuf1_T::responseOK)
        params->arg1.ipcMetaBuf.bufVa = meta1.bufVa;
      else
        params->arg1.ipcMetaBuf.bufVa = 0;

      LOG1("E3ACtrl_IPC_P1_SttControl ipcMetaBuf.cmd:%d",
           params->arg1.ipcMetaBuf.cmd);
      LOG1("E3ACtrl_IPC_P1_SttControl ipcMetaBuf.magicnum:%d",
           params->arg1.ipcMetaBuf.magicnum);
      LOG1("E3ACtrl_IPC_P1_SttControl ipcMetaBuf.response:%d",
           params->arg1.ipcMetaBuf.response);
    }
  } else {
    IPC_LOGE("%s Not Surpport This Send3ACtrl Commend", __func__);
    return -1;
  }

  LOG1("%s sensor idx:%d e3ACtrl:sttPipe 0x%x ----", __func__, sensor_index,
       params->e3ACtrl);

  return OK;
}

MINT32 Hal3aIpcServerAdapter::stt2Pipe(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_stt2pipe_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  IPC_Metabuf2_T meta2;
  hal3a_stt2pipe_params* params = static_cast<hal3a_stt2pipe_params*>(addr);
  if (!params) {
    IPC_LOGE("Stt2 Pipe Shared Buffer is NULL");
    return -1;
  }

  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d e3ACtrl:stt2Pipe 0x%x ++++", __func__, sensor_index,
       params->e3ACtrl);

  if (params->e3ACtrl == E3ACtrl_IPC_P1_Stt2Control) {
    // need to assign cmd first to order hal3a do related work
    meta2.cmd = params->arg1.ipcMetaBuf2.cmd;
    if (IPC_Metabuf2_T::cmdENQUE_FROM_DRV == params->arg1.ipcMetaBuf2.cmd) {
      meta2.magicnum = params->arg1.ipcMetaBuf2.magicnum;
      meta2.bufVa = params->arg1.ipcMetaBuf2.bufVa;

      LOG1("E3ACtrl_IPC_P1_Stt2Control ipcMetaBuf2.cmd:0x%x", meta2.cmd);
      LOG1("E3ACtrl_IPC_P1_Stt2Control ipcMetaBuf2.magicnum:%d",
           meta2.magicnum);
      LOG1("E3ACtrl_IPC_P1_Stt2Control ipcMetaBuf2.bufFd:%llu",
           params->arg1.ipcMetaBuf2.bufFd);
    }

    // error handling implement on response member variable
    mpHal3A[sensor_index]->send3ACtrl(params->e3ACtrl,
                                      reinterpret_cast<MUINTPTR>(&meta2), 0);

    // to response p1 whether enque 3A failed or not
    if (IPC_Metabuf2_T::cmdENQUE_FROM_DRV == params->arg1.ipcMetaBuf2.cmd) {
      params->arg1.ipcMetaBuf2.response = meta2.response;
      LOG1("E3ACtrl_IPC_P1_Stt2Control ipcMetaBuf2.response:%d",
           meta2.response);
    }

    if (IPC_Metabuf2_T::cmdDEQUE_FROM_3A == params->arg1.ipcMetaBuf2.cmd) {
      params->arg1.ipcMetaBuf2.magicnum = meta2.magicnum;
      params->arg1.ipcMetaBuf2.response = meta2.response;

      if (params->arg1.ipcMetaBuf2.response == IPC_Metabuf2_T::responseOK)
        params->arg1.ipcMetaBuf2.bufVa = meta2.bufVa;
      else
        params->arg1.ipcMetaBuf2.bufVa = 0;

      LOG1("E3ACtrl_IPC_P1_Stt2Control ipcMetaBuf2.cmd:%d",
           params->arg1.ipcMetaBuf2.cmd);
      LOG1("E3ACtrl_IPC_P1_Stt2Control ipcMetaBuf2.magicnum:%d",
           params->arg1.ipcMetaBuf2.magicnum);
      LOG1("E3ACtrl_IPC_P1_Stt2Control ipcMetaBuf2.response:%d",
           params->arg1.ipcMetaBuf2.response);
    }
  } else {
    IPC_LOGE("%s Not Surpport This Send3ACtrl Commend", __func__);
    return -1;
  }

  LOG1("%s sensor idx:%d e3ACtrl:stt2Pipe 0x%x ----", __func__, sensor_index,
       params->e3ACtrl);

  return OK;
}

MINT32 Hal3aIpcServerAdapter::hwEvent(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_hwevent_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  hal3a_hwevent_params* params = static_cast<hal3a_hwevent_params*>(addr);
  if (!params) {
    IPC_LOGE("HW Event Shared Buffer is NULL");
    return -1;
  }

  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d e3ACtrl:hwEvent 0x%x ++++", __func__, sensor_index,
       params->e3ACtrl);

  if (params->e3ACtrl == E3ACtrl_IPC_P1_HwSignal) {
    if (!mpHal3A[sensor_index]->send3ACtrl(
            params->e3ACtrl, reinterpret_cast<MUINTPTR>(&params->arg1.evt),
            0)) {
      IPC_LOGE("%s Result from P1 HW Signal is Failed", __func__);
      return -1;
    }
    LOG1("E3ACtrl_IPC_P1_HwSignal evt.event:0x%x", params->arg1.evt.event);
  } else {
    IPC_LOGE("%s Not Surpport This Send3ACtrl Commend", __func__);
    return -1;
  }

  LOG1("%s sensor idx:%d e3ACtrl:hwEvent 0x%x ----", __func__, sensor_index,
       params->e3ACtrl);

  return OK;
}

MINT32 Hal3aIpcServerAdapter::aePlineLimit(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_plinelimit_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  AE_Pline_Limitation_T rLimitParams;
  hal3a_plinelimit_params* params = static_cast<hal3a_plinelimit_params*>(addr);
  if (!params) {
    IPC_LOGE("AE Pline Limitation Shared Buffer is NULL");
    return -1;
  }

  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d e3ACtrl:plinelimit 0x%x ++++", __func__, sensor_index,
       params->e3ACtrl);

  if (params->e3ACtrl == E3ACtrl_SetAEPlineLimitation) {
    rLimitParams.bEnable = params->ipcLimitParams.bEnable;
    rLimitParams.bEquivalent = params->ipcLimitParams.bEquivalent;
    rLimitParams.u4IncreaseISO_x100 = params->ipcLimitParams.u4IncreaseISO_x100;
    rLimitParams.u4IncreaseShutter_x100 =
        params->ipcLimitParams.u4IncreaseShutter_x100;

    LOG1("E3ACtrl_SetAEPlineLimitation: Enable = %d", rLimitParams.bEnable);
    LOG1("E3ACtrl_SetAEPlineLimitation: Equivalent = %d",
         rLimitParams.bEquivalent);
    LOG1("E3ACtrl_SetAEPlineLimitation: u4IncreaseISO_x100 = %d",
         rLimitParams.u4IncreaseISO_x100);
    LOG1("E3ACtrl_SetAEPlineLimitation: u4IncreaseShutter_x100 = %d",
         rLimitParams.u4IncreaseShutter_x100);

    if (!mpHal3A[sensor_index]->send3ACtrl(
            params->e3ACtrl, reinterpret_cast<MINTPTR>(&rLimitParams), 0)) {
      IPC_LOGE("%s Result from Set AE Pline Limitation is Failed", __func__);
      return -1;
    }
  } else {
    IPC_LOGE("%s Not Surpport This Send3ACtrl Commend", __func__);
    return -1;
  }

  LOG1("%s sensor idx:%d e3ACtrl:plinelimit 0x%x ----", __func__, sensor_index,
       params->e3ACtrl);
  return OK;
}

MINT32 Hal3aIpcServerAdapter::afLensConfig(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_lensconfig_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  IPC_LensConfig_T lensConfig;
  hal3a_lensconfig_params* params = static_cast<hal3a_lensconfig_params*>(addr);
  if (!params) {
    IPC_LOGE("AF Lens Config Shared Buffer is NULL");
    return -1;
  }

  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d e3ACtrl:lensConfig 0x%x ++++", __func__, sensor_index,
       params->e3ACtrl);

  if (params->e3ACtrl == E3ACtrl_IPC_AF_ExchangeLensConfig) {
    lensConfig.cmd = params->lensConfig.cmd;

    if (lensConfig.cmd == IPC_LensConfig_T::ACK_IS_SUPPORT_LENS) {
      lensConfig.cmd = params->lensConfig.cmd;
      lensConfig.val.is_support = params->lensConfig.val.is_support;
      lensConfig.succeeded = params->lensConfig.succeeded;
    }

    // error handling implement on succeeded member variable
    mpHal3A[sensor_index]->send3ACtrl(
        params->e3ACtrl, reinterpret_cast<MINTPTR>(&lensConfig), 0);

    params->lensConfig.cmd = lensConfig.cmd;
    params->lensConfig.succeeded = lensConfig.succeeded;

    if (params->lensConfig.cmd == IPC_LensConfig_T::CMD_FOCUS_ABSOULTE) {
      params->lensConfig.val.focus_pos = lensConfig.val.focus_pos;
    }
  } else {
    IPC_LOGE("%s Not Surpport This Send3ACtrl Commend", __func__);
    return -1;
  }

  LOG1("%s sensor idx:%d e3ACtrl:lensConfig 0x%x ----", __func__, sensor_index,
       params->e3ACtrl);
  return OK;
}

MINT32 Hal3aIpcServerAdapter::send3ACtrl(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_send3actrl_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  IIPCHalSensorList* pHalSensorList;
  IIPCHalSensor* pIPCSensor;
  hal3a_send3actrl_params* params = static_cast<hal3a_send3actrl_params*>(addr);
  if (!params) {
    IPC_LOGE("Send3ACtrl Shared Buffer is NULL");
    return -1;
  }

  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d e3ACtrl:0x%x ++++", __func__, sensor_index,
       params->e3ACtrl);

  if (mpHal3A[sensor_index] == nullptr &&
      params->e3ACtrl != E3ACtrl_IPC_SetStaticInfo) {
    IPC_LOGE("%s mpHal3A[sensor_index] == nullptr, so return", __func__);
    return -1;
  }

  switch (params->e3ACtrl) {
    case E3ACtrl_GetAEInitExpoSetting:
      AEInitExpoSetting_T* a_rAEInitExpoSetting;
      if (!mpHal3A[sensor_index]->send3ACtrl(
              params->e3ACtrl, reinterpret_cast<MINTPTR>(&params->arg1),
              reinterpret_cast<MINTPTR>(&params->arg2))) {
        IPC_LOGE("%s Result from Get AE Init ExpoSetting is Failed", __func__);
        return -1;
      }
      a_rAEInitExpoSetting =
          reinterpret_cast<AEInitExpoSetting_T*>(&params->arg1);
      if (!a_rAEInitExpoSetting) {
        IPC_LOGE("%s AE Init Exposure Setting Buffer is NULL", __func__);
        return -1;
      }
      memcpy(&params->arg1.initExpoSetting, a_rAEInitExpoSetting,
             sizeof(AEInitExpoSetting_T));
      LOG1("E3ACtrl_GetAEInitExpoSetting u4SensorMode:0x%x",
           a_rAEInitExpoSetting->u4SensorMode);
      LOG1("E3ACtrl_GetAEInitExpoSetting u4AETargetMode:0x%x",
           a_rAEInitExpoSetting->u4AETargetMode);
      LOG1("E3ACtrl_GetAEInitExpoSetting u4Eposuretime:%d",
           a_rAEInitExpoSetting->u4Eposuretime);
      LOG1("E3ACtrl_GetAEInitExpoSetting u4AfeGain:%d",
           a_rAEInitExpoSetting->u4AfeGain);
      break;
    case E3ACtrl_IPC_SetPeriSensorData:
      if (mpHal3A[sensor_index]->send3ACtrl(
              params->e3ACtrl, reinterpret_cast<MINTPTR>(&params->arg1), 0)) {
        IPC_LOGE("%s fail to set sensor data!", __func__);
        return -1;
      }
      break;

    case E3ACtrl_IPC_SetStaticInfo:
      pHalSensorList = IIPCHalSensorList::getInstance();
      pHalSensorList->ipcSetSensorStaticInfo(
          params->arg1.ipcSensorStatic.idx, params->arg1.ipcSensorStatic.type,
          params->arg1.ipcSensorStatic.deviceId,
          params->arg1.ipcSensorStatic.sensorStaticInfo);

      LOG1("E3ACtrl_IPC_SetStaticInfo idx:0x%x, type:0x%x, deviceId:0x%x",
           params->arg1.ipcSensorStatic.idx, params->arg1.ipcSensorStatic.type,
           params->arg1.ipcSensorStatic.deviceId);

      break;

    case E3ACtrl_IPC_SetDynamicInfo:
      pHalSensorList = IIPCHalSensorList::getInstance();
      pIPCSensor = static_cast<IIPCHalSensor*>(
          pHalSensorList->createSensor("", sensor_index));
      pIPCSensor->ipcSetDynamicInfo(params->arg1.sensorDynamicInfo);
      LOG1("E3ACtrl_IPC_SetDynamicInfo ");
      break;

    case E3ACtrl_IPC_SetDynamicInfoEx:
      pHalSensorList = IIPCHalSensorList::getInstance();
      pIPCSensor = static_cast<IIPCHalSensor*>(
          pHalSensorList->createSensor("", sensor_index));
      pIPCSensor->ipcSetDynamicInfoEx(params->arg1.sensorDynamicInfoExt);
      LOG1("E3ACtrl_IPC_SetDynamicInfoEx ");
      break;

    case E3ACtrl_IPC_CropWin:
      pHalSensorList = IIPCHalSensorList::getInstance();
      pIPCSensor = static_cast<IIPCHalSensor*>(
          pHalSensorList->createSensor("", sensor_index));
      LOG1("%s server: ipcSensorCropWinInfo:full_h = %d", __func__,
           params->arg2.sensorCropWinInfo.full_h);
      LOG1("%s server: ipcSensorCropWinInfo:full_w = %d", __func__,
           params->arg2.sensorCropWinInfo.full_w);
      pIPCSensor->updateCommand(
          0, NSCam::SENSOR_CMD_GET_SENSOR_CROP_WIN_INFO,
          reinterpret_cast<MUINTPTR>(&params->arg1.scenario),
          reinterpret_cast<MUINTPTR>(&params->arg2.sensorCropWinInfo), 0);
      break;
    case E3ACtrl_IPC_PixelClock:
      pHalSensorList = IIPCHalSensorList::getInstance();
      pIPCSensor = static_cast<IIPCHalSensor*>(
          pHalSensorList->createSensor("", sensor_index));
      pIPCSensor->updateCommand(
          0, NSCam::SENSOR_CMD_GET_PIXEL_CLOCK_FREQ,
          reinterpret_cast<MUINTPTR>(&params->arg1.pixelClokcFreq), 0, 0);
      break;

    case E3ACtrl_IPC_PixelLine:
      pHalSensorList = IIPCHalSensorList::getInstance();
      pIPCSensor = static_cast<IIPCHalSensor*>(
          pHalSensorList->createSensor("", sensor_index));
      pIPCSensor->updateCommand(
          0, NSCam::SENSOR_CMD_GET_FRAME_SYNC_PIXEL_LINE_NUM,
          reinterpret_cast<MUINTPTR>(&params->arg1.frameSyncPixelLineNum), 0,
          0);
      break;

    case E3ACtrl_IPC_PdafInfo:
      pHalSensorList = IIPCHalSensorList::getInstance();
      pIPCSensor = static_cast<IIPCHalSensor*>(
          pHalSensorList->createSensor("", sensor_index));
      pIPCSensor->updateCommand(
          0, NSCam::SENSOR_CMD_GET_SENSOR_PDAF_INFO,
          reinterpret_cast<MUINTPTR>(&params->arg1.scenario),
          reinterpret_cast<MUINTPTR>(&params->arg2.sensorPdafInfo), 0);
      break;

    case E3ACtrl_IPC_PdafCapacity:
      pHalSensorList = IIPCHalSensorList::getInstance();
      pIPCSensor = static_cast<IIPCHalSensor*>(
          pHalSensorList->createSensor("", sensor_index));
      pIPCSensor->updateCommand(
          0, NSCam::SENSOR_CMD_GET_SENSOR_PDAF_CAPACITY,
          reinterpret_cast<MUINTPTR>(&params->arg1.scenario),
          reinterpret_cast<MUINTPTR>(&params->arg2.sensorPdafCapacity), 0);
      break;

    case E3ACtrl_IPC_SensorVCInfo:
      pHalSensorList = IIPCHalSensorList::getInstance();
      pIPCSensor = static_cast<IIPCHalSensor*>(
          pHalSensorList->createSensor("", sensor_index));
      pIPCSensor->updateCommand(
          0, NSCam::SENSOR_CMD_GET_SENSOR_VC_INFO,
          reinterpret_cast<MUINTPTR>(&params->arg1.sensorVCInfo),
          reinterpret_cast<MUINTPTR>(&params->arg2.scenario), 0);
      break;

    case E3ACtrl_IPC_DefFrameRate:
      pHalSensorList = IIPCHalSensorList::getInstance();
      pIPCSensor = static_cast<IIPCHalSensor*>(
          pHalSensorList->createSensor("", sensor_index));
      pIPCSensor->updateCommand(
          0, NSCam::SENSOR_CMD_GET_DEFAULT_FRAME_RATE_BY_SCENARIO,
          reinterpret_cast<MUINTPTR>(&params->arg1.scenario),
          reinterpret_cast<MUINTPTR>(&params->arg2.defaultFrameRate), 0);
      break;

    case E3ACtrl_IPC_RollingShutter:
      pHalSensorList = IIPCHalSensorList::getInstance();
      pIPCSensor = static_cast<IIPCHalSensor*>(
          pHalSensorList->createSensor("", sensor_index));
      pIPCSensor->updateCommand(0, NSCam::SENSOR_CMD_GET_SENSOR_ROLLING_SHUTTER,
                                reinterpret_cast<MUINTPTR>(&params->arg1.tline),
                                reinterpret_cast<MUINTPTR>(&params->arg2.vsize),
                                0);
      break;

    case E3ACtrl_IPC_VerticalBlanking:
      pHalSensorList = IIPCHalSensorList::getInstance();
      pIPCSensor = static_cast<IIPCHalSensor*>(
          pHalSensorList->createSensor("", sensor_index));
      pIPCSensor->updateCommand(
          0, NSCam::SENSOR_CMD_GET_VERTICAL_BLANKING,
          reinterpret_cast<MUINTPTR>(&params->arg1.verticalBlanking), 0, 0);
      break;

    case E3ACtrl_SetEnablePBin:
      if (!mpHal3A[sensor_index]->send3ACtrl(
              params->e3ACtrl, params->arg1.enabled, params->arg2.enabled)) {
        IPC_LOGE("%s Result from Set Enable PBin is Failed", __func__);
        return -1;
      }
      LOG1("E3ACtrl_IPC_P1_SttControl arg1.enabled:0x%x, arg2.enabled:0x%x",
           params->arg1.enabled, params->arg2.enabled);
      break;

    case E3ACtrl_IPC_Set_MetaStaticInfo:
      if (mpHal3A[sensor_index]->send3ACtrl(
              params->e3ACtrl, reinterpret_cast<MINTPTR>(&params->arg1), 0)) {
        IPC_LOGE("%s Result from Set Meta Static Info is Failed", __func__);
        return -1;
      }
      LOG1("E3ACtrl_IPC_Set_MetaStaticInfo ");
      break;

    case E3ACtrl_GetIsAEStable:
      MINT32 AEStable;
      if (!mpHal3A[sensor_index]->send3ACtrl(
              params->e3ACtrl, reinterpret_cast<MUINTPTR>(&AEStable), 0)) {
        IPC_LOGE("Get Ae Stable Failed in Hal3A");
        return -1;
      }
      params->arg1.AeStable = AEStable;
      break;

    default:
      IPC_LOGE("%s Not Surpport This Send3ACtrl Commend", __func__);
      return -1;
      break;
  }

  LOG1("%s sensor idx:%d e3ACtrl:0x%x ----", __func__, sensor_index,
       params->e3ACtrl);

  return OK;
}

int Hal3aIpcServerAdapter::notifyP1PwrOn(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_notify_p1_pwr_on_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);

  MINT32 i4Ret;
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    i4Ret = mpHal3A[sensor_index]->notifyP1PwrOn();
    if (!i4Ret) {
      IPC_LOGE("notifyP1PwrOn Failed in Hal3A");
      return -1;
    }
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);
  return OK;
}

int Hal3aIpcServerAdapter::notifyP1Done(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_notify_p1_pwr_done_params)),
             UNKNOWN_ERROR, "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;

  hal3a_notify_p1_pwr_done_params* params =
      static_cast<hal3a_notify_p1_pwr_done_params*>(addr);
  if (!params) {
    IPC_LOGE("Notify P1 Done Buffer is NULL");
    return -1;
  }

  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    mpHal3A[sensor_index]->notifyP1Done(params->u4MagicNum, NULL);
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);

  return OK;
}

int Hal3aIpcServerAdapter::notifyP1PwrOff(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_notify_p1_pwr_off_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;

  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);
  MINT32 i4Ret;
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    i4Ret = mpHal3A[sensor_index]->notifyP1PwrOff();
    if (!i4Ret) {
      IPC_LOGE("notifyP1PwrOff Failed in Hal3A");
      return -1;
    }
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);

  return OK;
}

int Hal3aIpcServerAdapter::setSensorMode(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_set_sensor_mode_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;

  hal3a_set_sensor_mode_params* params =
      static_cast<hal3a_set_sensor_mode_params*>(addr);
  if (!params) {
    IPC_LOGE("Set Sensor Mode Buffer is NULL");
    return -1;
  }

  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    mpHal3A[sensor_index]->setSensorMode(params->i4SensorMode);
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);

  return OK;
}

int Hal3aIpcServerAdapter::attachCb(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_attach_cb_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;

  hal3a_attach_cb_params* params = static_cast<hal3a_attach_cb_params*>(addr);
  if (!params) {
    IPC_LOGE("Attach Callback Buffer is NULL");
    return -1;
  }

  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);
  MINT32 i4Ret;
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    i4Ret = mpHal3A[sensor_index]->attachCb(params->eId, this);
    addrMapping[params->eId] = addr;
    if (i4Ret < 0) {
      IPC_LOGE("Attach Callback Failed in Hal3A");
      return -1;
    }
  }

  LOG1("%s sensor idx:%d eId:%d addr:%p", __func__, sensor_index, params->eId,
       addr);

  return OK;
}

int Hal3aIpcServerAdapter::detachCb(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_detach_cb_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);

  hal3a_detach_cb_params* params = static_cast<hal3a_detach_cb_params*>(addr);
  if (!params) {
    IPC_LOGE("Attach Callback Buffer is NULL");
    return -1;
  }

  MINT32 i4Ret;
  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    i4Ret = mpHal3A[sensor_index]->detachCb(params->eId, this);
    if (i4Ret < 0) {
      IPC_LOGE("Detach Callback Failed in Hal3A");
      return -1;
    }
  }

  return OK;
}

int Hal3aIpcServerAdapter::get(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_get_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);

  MetaSet_T result3A;
  int ret = 1;
  hal3a_get_params* params = static_cast<hal3a_get_params*>(addr);
  if (!params) {
    IPC_LOGE("Get Buffer is NULL");
    return -1;
  }

  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    ret = mpHal3A[sensor_index]->get(params->frmId, &result3A);
  }

  /* flatten result3A to share memory*/
  params->result.MagicNum = result3A.MagicNum;
  params->result.Dummy = result3A.Dummy;
  params->result.PreSetKey = result3A.PreSetKey;
  params->get_ret = ret;
  ssize_t appSize = result3A.appMeta.flatten(&params->appMetaBuffer,
                                             sizeof(params->appMetaBuffer));
  ssize_t halSize = result3A.halMeta.flatten(&params->halMetaBuffer,
                                             sizeof(params->halMetaBuffer));
  if (appSize < 0 || halSize < 0) {
    if (appSize < 0)
      IPC_LOGE("GET: App Metadata flatten failed");
    if (halSize < 0)
      IPC_LOGE("GET: Hal Metadata flatten failed");
    return -1;
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);
  return OK;
}

int Hal3aIpcServerAdapter::getCur(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_get_cur_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);

  MetaSet_T result3A;
  int ret = 0;
  hal3a_get_cur_params* params = static_cast<hal3a_get_cur_params*>(addr);
  if (!params) {
    IPC_LOGE("GetCur Buffer is NULL");
    return -1;
  }

  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    ret = mpHal3A[sensor_index]->getCur(params->frmId, &result3A);
  }

  /* flatten result3a here. */
  params->result.MagicNum = result3A.MagicNum;
  params->result.Dummy = result3A.Dummy;
  params->result.PreSetKey = result3A.PreSetKey;
  params->get_cur_ret = ret;
  ssize_t appSize = result3A.appMeta.flatten(&params->appMetaBuffer,
                                             sizeof(params->appMetaBuffer));
  ssize_t halSize = result3A.halMeta.flatten(&params->halMetaBuffer,
                                             sizeof(params->halMetaBuffer));
  if (appSize < 0 || halSize < 0) {
    if (appSize < 0)
      IPC_LOGE("GETCUR: App Metadata flatten failed");
    if (halSize < 0)
      IPC_LOGE("GETCUR: Hal Metadata flatten failed");
    return -1;
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);
  return OK;
}

bool Hal3aIpcServerAdapter::setFDInfoOnActiveArray(void* addr, int dataSize) {
  CheckError((dataSize < sizeof(hal3a_set_fdInfo_params)), UNKNOWN_ERROR,
             "@%s, buffer is small", __FUNCTION__);
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return -1;
  LOG1("%s sensor idx:%d ++++", __func__, sensor_index);

  hal3a_set_fdInfo_params* params = static_cast<hal3a_set_fdInfo_params*>(addr);
  if (!params) {
    IPC_LOGE("Set FD Info On Active Array Buffer is NULL");
    return -1;
  }

  params->detectFace.faces = &params->faceDetectInfo[0];
  params->detectFace.posInfo = &params->facePoseInfo[0];
  bool ret = MTRUE;

  if (!mpHal3A[sensor_index]) {
    IPC_LOGE("mpHal3A[%d] is NULL", sensor_index);
    return -1;
  } else {
    ret = mpHal3A[sensor_index]->setFDInfoOnActiveArray(&params->detectFace);
    if (ret == MFALSE) {
      IPC_LOGE("Set FD Info On Active Array in Hal3A");
      return -1;
    }
  }

  LOG1("%s sensor idx:%d ----", __func__, sensor_index);
  return OK;
}

void Hal3aIpcServerAdapter::doNotifyCb(MINT32 _msgType,
                                       MINTPTR _ext1,
                                       MINTPTR _ext2,
                                       MINTPTR _ext3) {
  if (_msgType < 0 || _msgType >= IHal3ACb::eID_MSGTYPE_NUM) {
    IPC_LOGE("%s : Message Type %d is Invalid", __func__, _msgType);
    return;
  }
  void* addr = addrMapping[_msgType];
  int sensor_index = hal3a_server_parsing_sensor_idx(addr);
  if (sensor_index < 0)
    return;
  hal3a_attach_cb_params* params = static_cast<hal3a_attach_cb_params*>(addr);
  if (!params) {
    IPC_LOGE("Do Notify Callback Buffer is NULL");
    return;
  }

  LOG1("%s ++++", __func__);
  params->cb_result[_msgType]._ext1 = _ext1;
  params->cb_result[_msgType]._ext2 = _ext2;
  params->cb_result[_msgType]._ext3 = _ext3;

  LOG1("%s eId:%d", __func__, params->eId);
  Mediatek3AServer::getInstance()->notify(IPC_HAL3A_NOTIFY_CB, sensor_index);
  LOG1("%s ----", __func__);
}

}  // namespace NS3Av3
