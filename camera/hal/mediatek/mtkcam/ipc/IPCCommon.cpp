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

#include <iostream>
#include <memory>
#include <string>

#include <camera/hal/mediatek/mtkcam/ipc/IPCCommon.h>

IPC_GROUP Mediatek3AIpcCmdToGroup(IPC_CMD cmd) {
  switch (cmd) {
    case IPC_HAL3A_GETSENSORPARAM:
      return IPC_GROUP_GETSENSORPARAM;
    case IPC_HAL3A_NOTIFYCB:
      return IPC_GROUP_NOTIFYCB;
    case IPC_HAL3A_TUNINGPIPE:
      return IPC_GROUP_TUNINGPIPE;
    case IPC_HAL3A_STTPIPE:
      return IPC_GROUP_STTPIPE;
    case IPC_HAL3A_HWEVENT:
      return IPC_GROUP_HWEVENT;
    case IPC_HAL3A_SETISP:
      return IPC_GROUP_SETISP;
    case IPC_HAL3A_PRESET:
      return IPC_GROUP_PRESET;
    case IPC_HAL3A_NOTIFYCB_ENABLE:
    case IPC_HAL3A_GETSENSORPARAM_ENABLE:
      return IPC_GROUP_CB_SENSOR_ENABLE;
    case IPC_HAL3A_TUNINGPIPE_TERM:
      return IPC_GROUP_TUNINGPIPE_TERM;
    case IPC_HAL3A_STT2PIPE:
      return IPC_GROUP_STT2PIPE;
    case IPC_HAL3A_SET:
      return IPC_GROUP_SET;
    case IPC_HAL3A_GET:
      return IPC_GROUP_GET;
    case IPC_HAL3A_AEPLINELIMIT:
      return IPC_GROUP_AEPLINELIMIT;
    case IPC_SWNR_CREATE:
    case IPC_SWNR_DESTROY:
    case IPC_SWNR_DO_SWNR:
    case IPC_SWNR_GET_DEBUGINFO:
    case IPC_SWNR_DUMP_PARAM:
      return IPC_GROUP_SWNR;
    case IPC_LCS_CREATE:
    case IPC_LCS_INIT:
    case IPC_LCS_CONFIG:
    case IPC_LCS_UNINIT:
      return IPC_GROUP_LCS;
    case IPC_ISPMGR_CREATE:
    case IPC_ISPMGR_QUERYLCSO:
    case IPC_ISPMGR_PPNR3D:
      return IPC_GROUP_ISPMGR;
    case IPC_NR3D_EIS_CREATE:
    case IPC_NR3D_EIS_DESTROY:
    case IPC_NR3D_EIS_INIT:
    case IPC_NR3D_EIS_MAIN:
    case IPC_NR3D_EIS_RESET:
    case IPC_NR3D_EIS_FEATURECTRL:
      return IPC_GROUP_3DNR;
    case IPC_FD_CREATE:
    case IPC_FD_DESTORY:
    case IPC_FD_INIT:
    case IPC_FD_MAIN:
    case IPC_FD_GET_CAL_DATA:
    case IPC_FD_SET_CAL_DATA:
    case IPC_FD_MAIN_PHASE2:
    case IPC_FD_GETRESULT:
    case IPC_FD_RESET:
    case IPC_HAL3A_SET_FDINFO:
      return IPC_GROUP_FD;
    case IPC_HAL3A_AFLENSCONFIG:
      return IPC_GROUP_AF;
    case IPC_HAL3A_AFLENS_ENABLE:
      return IPC_GROUP_AF_ENABLE;
    case IPC_HAL3A_SEND3ACTRL:
      return IPC_GROUP_3ACTRL;
    default:
      return IPC_GROUP_0;
  }
}

std::shared_ptr<NSCam::IImageBuffer> IPCImageBufAllocator::createImageBuffer() {
  MINT32 bufBoudndaryInBytes[3] = {0};

  NSCam::IImageBufferAllocator::ImgParam extParam(
      m_imgCfg.format, MSize(m_imgCfg.width, m_imgCfg.height), m_imgCfg.strides,
      bufBoudndaryInBytes, m_imgCfg.planecount);

  /* dummy heap descriptor */
  NSCam::PortBufInfo_dummy portBufInfo = NSCam::PortBufInfo_dummy(
      m_imgCfg.fd[0], m_imgCfg.va, m_imgCfg.pa, m_imgCfg.planecount);

  std::shared_ptr<NSCam::IImageBufferHeap> pHeap =
      NSCam::IDummyImageBufferHeap::create(m_caller.c_str(), extParam,
                                           portBufInfo);

  if (pHeap == NULL)
    return NULL;

  /* create image buffer */
  std::shared_ptr<NSCam::IImageBuffer> pBuf = pHeap->createImageBuffer();

  return pBuf;
}
