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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_IPC_IPCCOMMON_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_IPC_IPCCOMMON_H_

#include <memory>
#include <string>

#include <mtkcam/utils/std/Log.h>
#include "mtkcam/def/common.h"
#include "mtkcam/utils/imgbuf/IDummyImageBufferHeap.h"

#define LOG1(fmt, arg...)                            \
  do {                                               \
    CAM_LOGD("[%s] " fmt "\t", __FUNCTION__, ##arg); \
  } while (0)
#define LOG2(fmt, arg...)                            \
  do {                                               \
    CAM_LOGI("[%s] " fmt "\t", __FUNCTION__, ##arg); \
  } while (0)
#define IPC_LOGE(fmt, arg...)                        \
  do {                                               \
    CAM_LOGE("[%s] " fmt "\t", __FUNCTION__, ##arg); \
  } while (0)

/**
 * Use to check input parameter and if failed, return err_code and print error
 * message
 */
#define VOID_VALUE
#define CheckError(condition, err_code, err_msg, args...) \
  do {                                                    \
    if (condition) {                                      \
      IPC_LOGE(err_msg, ##args);                          \
      return err_code;                                    \
    }                                                     \
  } while (0)

/**
 * Use to check input parameter and if failed, return err_code and print warning
 * message, this should be used for non-vital error checking.
 */
#define CheckWarning(condition, err_code, err_msg, args...) \
  do {                                                      \
    if (condition) {                                        \
      LOGW(err_msg, ##args);                                \
      return err_code;                                      \
    }                                                       \
  } while (0)

#define IPC_MATCHING_KEY 0x56  // the value is randomly chosen
#define IPC_REQUEST_HEADER_USED_NUM 2

enum IPC_CMD {
  /* cmds of HAL3A */
  IPC_HAL3A_INIT,  // 0
  IPC_HAL3A_DEINIT,
  IPC_HAL3A_CONFIG,
  IPC_HAL3A_START,
  IPC_HAL3A_STOP,
  IPC_HAL3A_STOP_STT,  // 5
  IPC_HAL3A_SET,
  IPC_HAL3A_SETISP,
  IPC_HAL3A_START_REQUEST_Q,
  IPC_HAL3A_START_CAPTURE,
  IPC_HAL3A_PRESET,  // 10
  IPC_HAL3A_SEND3ACTRL,
  IPC_HAL3A_GETSENSORPARAM,
  IPC_HAL3A_NOTIFYCB,
  IPC_HAL3A_TUNINGPIPE,
  IPC_HAL3A_STTPIPE,  // 15
  IPC_HAL3A_HWEVENT,
  IPC_HAL3A_NOTIFY_P1_PWR_ON,
  IPC_HAL3A_NOTIFY_P1_PWR_DONE,
  IPC_HAL3A_NOTIFY_P1_PWR_OFF,
  IPC_HAL3A_SET_SENSOR_MODE,  // 20
  IPC_HAL3A_ATTACH_CB,
  IPC_HAL3A_DETACH_CB,
  IPC_HAL3A_GET,
  IPC_HAL3A_GET_CUR,
  IPC_HAL3A_DEBUG,  // 25
  IPC_HAL3A_NOTIFY_CB,
  IPC_HAL3A_NOTIFYCB_ENABLE,
  IPC_HAL3A_TUNINGPIPE_TERM,
  IPC_HAL3A_GETSENSORPARAM_ENABLE,
  IPC_HAL3A_STT2PIPE,  // 30
  /* cmds of SWNR */
  IPC_SWNR_CREATE,
  IPC_SWNR_DESTROY,
  IPC_SWNR_DO_SWNR,
  IPC_SWNR_GET_DEBUGINFO,
  IPC_SWNR_DUMP_PARAM,  // 35
  /* cmds of LCS */
  IPC_LCS_CREATE,
  IPC_LCS_INIT,
  IPC_LCS_CONFIG,
  IPC_LCS_UNINIT,
  /* cmds of 3DNR */
  IPC_HAL3A_AEPLINELIMIT,  // 40
  IPC_ISPMGR_CREATE,
  IPC_ISPMGR_QUERYLCSO,
  IPC_ISPMGR_PPNR3D,
  IPC_NR3D_EIS_CREATE,
  IPC_NR3D_EIS_DESTROY,  // 45
  IPC_NR3D_EIS_INIT,
  IPC_NR3D_EIS_MAIN,
  IPC_NR3D_EIS_RESET,
  IPC_NR3D_EIS_FEATURECTRL,
  /* cmds of FD */
  IPC_FD_CREATE,  // 50
  IPC_FD_DESTORY,
  IPC_FD_INIT,
  IPC_FD_MAIN,
  IPC_FD_GET_CAL_DATA,
  IPC_FD_SET_CAL_DATA,  // 55
  IPC_FD_MAIN_PHASE2,
  IPC_FD_GETRESULT,
  IPC_FD_RESET,
  /*cmds of AF*/
  IPC_HAL3A_AFLENSCONFIG,
  IPC_HAL3A_AFLENS_ENABLE,  // 60
  IPC_HAL3A_SET_FDINFO,
};

enum IPC_GROUP {
  IPC_GROUP_0,
  IPC_GROUP_GETSENSORPARAM,  // 1
  IPC_GROUP_NOTIFYCB,
  IPC_GROUP_TUNINGPIPE,
  IPC_GROUP_STTPIPE,
  IPC_GROUP_HWEVENT,  // 5
  IPC_GROUP_SETISP,
  IPC_GROUP_PRESET,
  IPC_GROUP_CB_SENSOR_ENABLE,
  IPC_GROUP_TUNINGPIPE_TERM,
  IPC_GROUP_STT2PIPE,  // 10
  IPC_GROUP_SET,
  IPC_GROUP_GET,
  IPC_GROUP_AEPLINELIMIT,
  IPC_GROUP_SWNR,
  IPC_GROUP_LCS,  // 15
  IPC_GROUP_3DNR,
  IPC_GROUP_ISPMGR,
  IPC_GROUP_FD,
  IPC_GROUP_AF,
  IPC_GROUP_AF_ENABLE,  // 20
  IPC_GROUP_3ACTRL,
  IPC_GROUP_NUM
};

IPC_GROUP Mediatek3AIpcCmdToGroup(IPC_CMD cmd);
std::string Mediatek3AIpcCmdToString(IPC_CMD cmd);

#define IPC_MAX_SENSOR_NUM 2

/* utility to create an customized IImageBuffer */
class IPCImageBufAllocator {
 public:
  struct config {
    MINT format;
    MUINT32 width;
    MUINT32 height;
    MUINT32 planecount;
    MUINT32 strides[3];
    MUINT32 scanlines[3];
    MUINTPTR va[3];
    MUINTPTR pa[3];
    MINT32 fd[3];
    MINT32 imgbits;
    MUINT32 stridepixel[3];
    MUINT32 bufsize[3];
  };

 public:
  std::shared_ptr<NSCam::IImageBuffer> createImageBuffer();

 public:
  IPCImageBufAllocator(const struct config& cfg, std::string caller)
      : m_imgCfg(cfg), m_caller(caller) {}
  virtual ~IPCImageBufAllocator() = default;

 private:
  struct config m_imgCfg;
  std::string m_caller;
};

struct IImagebufInfo {
  MUINT32 width;
  MUINT32 height;
  MINT format;
  MUINT32 plane_cnt;
  MUINT32 strides_bytes[3];
  MUINT32 strides_pixel[3];
  MUINT32 scanlines[3];
  MUINT32 buf_size[3];
  MUINTPTR va;  // should be filled by IPC server
  MINT32 buf_handle;
};

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_IPC_IPCCOMMON_H_
