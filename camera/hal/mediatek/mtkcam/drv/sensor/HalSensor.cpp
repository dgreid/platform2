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

#define LOG_TAG "MtkCam/HalSensor"

#include "MyUtils.h"
#ifdef USING_MTK_LDVT
#include <uvvf.h>
#endif
#include <fcntl.h>
#include <mtkcam/def/common.h>
#include <mtkcam/v4l2/IPCIHalSensor.h>
#include <sys/ioctl.h>
#include "mtkcam/drv/sensor/HalSensor.h"
#include "mtkcam/drv/sensor/img_sensor.h"
#include <linux/v4l2-subdev.h>
#include <cutils/compiler.h>  // for CC_LIKELY, CC_UNLIKELY

// STL
#include <array>   // std::array
#include <memory>  // shared_ptr
#include <string>
#include <vector>  // std::vector

using NSCam::IIPCHalSensor;
using NSCam::IIPCHalSensorListProv;
using NSCam::SensorCropWinInfo;
using NSCam::SensorVCInfo;

#if MTKCAM_HAVE_SANDBOX_SUPPORT
static IHalSensor* createIPCHalSensorByIdx(MUINT32 idx) {
  IIPCHalSensorListProv* pIPCSensorList = IIPCHalSensorListProv::getInstance();
  if (CC_UNLIKELY(pIPCSensorList == nullptr)) {
    CAM_LOGE(
        "get IIPCHalSensorListProv is nullptr, sendCommand to IPCSensor"
        "failed");
    return nullptr;
  }

  IHalSensor* pIPCSensor = pIPCSensorList->createSensor(LOG_TAG, idx);

  if (CC_UNLIKELY(pIPCSensor == nullptr)) {
    CAM_LOGE("create IIPCHalSensor failed, sendCommand failed");
    return nullptr;
  }
  return pIPCSensor;
}

template <class ARG1_T, class ARG2_T>
inline void _updateCommand(MUINT i,
                           MUINTPTR cmd,
                           ARG1_T* arg1,
                           ARG2_T* arg2,
                           IHalSensor* p,
                           IIPCHalSensor* q) {
  p->sendCommand(i, cmd, reinterpret_cast<MUINTPTR>(&(*arg1)), sizeof(*arg1),
                 reinterpret_cast<MUINTPTR>(&(*arg2)), sizeof(*arg2), 0, 0);

  q->updateCommand(i, cmd, reinterpret_cast<MUINTPTR>(&(*arg1)),
                   reinterpret_cast<MUINTPTR>(&(*arg2)), 0);
}

static void sendDataToIPCHalSensor(IHalSensor* pSource,
                                   IIPCHalSensor* pTarget,
                                   MUINT indexDual) {
  // supported scenarios
  const std::array<MINT32, 3> scenarios = {
      NSCam::SENSOR_SCENARIO_ID_NORMAL_PREVIEW,
      NSCam::SENSOR_SCENARIO_ID_NORMAL_CAPTURE,
      NSCam::SENSOR_SCENARIO_ID_NORMAL_VIDEO};

  // SENSOR_CMD_GET_SENSOR_CROP_WIN_INFO
  for (const auto& i : scenarios) {
    MINT32 arg1 = i;
    SensorCropWinInfo arg2;

    _updateCommand<MINT32, SensorCropWinInfo>(
        indexDual, NSCam::SENSOR_CMD_GET_SENSOR_CROP_WIN_INFO, &arg1, &arg2,
        pSource, pTarget);
  }

  // SENSOR_CMD_GET_PIXEL_CLOCK_FREQ
  do {
    MINT32 arg1 = 0, arg2 = 0;
    _updateCommand<MINT32, MINT32>(indexDual,
                                   NSCam::SENSOR_CMD_GET_PIXEL_CLOCK_FREQ,
                                   &arg1, &arg2, pSource, pTarget);
  } while (0);

  // SENSOR_CMD_GET_FRAME_SYNC_PIXEL_LINE_NUM
  do {
    MUINT32 arg1 = 0, arg2 = 0;
    _updateCommand<MUINT32, MUINT32>(
        indexDual, NSCam::SENSOR_CMD_GET_FRAME_SYNC_PIXEL_LINE_NUM, &arg1,
        &arg2, pSource, pTarget);
  } while (0);

  // SENSOR_CMD_GET_SENSOR_PDAF_INFO
  for (const auto& i : scenarios) {
    MINT32 arg1 = i;
    SET_PD_BLOCK_INFO_T arg2;
    _updateCommand<MINT32, SET_PD_BLOCK_INFO_T>(
        indexDual, NSCam::SENSOR_CMD_GET_SENSOR_PDAF_INFO, &arg1, &arg2,
        pSource, pTarget);
  }

  // SENSOR_CMD_GET_SENSOR_PDAF_CAPACITY
  for (const auto& i : scenarios) {
    MINT32 arg1 = i;
    MBOOL arg2 = MFALSE;
    _updateCommand<MINT32, MBOOL>(indexDual,
                                  NSCam::SENSOR_CMD_GET_SENSOR_PDAF_CAPACITY,
                                  &arg1, &arg2, pSource, pTarget);
  }

  // SENSOR_CMD_GET_SENSOR_VC_INFO
  for (const auto& i : scenarios) {
    SensorVCInfo arg1;
    MINT32 arg2 = i;
    _updateCommand<SensorVCInfo, MINT32>(indexDual,
                                         NSCam::SENSOR_CMD_GET_SENSOR_VC_INFO,
                                         &arg1, &arg2, pSource, pTarget);
  }

  // SENSOR_CMD_GET_DEFAULT_FRAME_RATE_BY_SCENARIO
  for (const auto& i : scenarios) {
    MINT32 arg1 = i;
    MUINT32 arg2 = 0;
    _updateCommand<MINT32, MUINT32>(
        indexDual, NSCam::SENSOR_CMD_GET_DEFAULT_FRAME_RATE_BY_SCENARIO, &arg1,
        &arg2, pSource, pTarget);
  }

  // SENSOR_CMD_GET_SENSOR_ROLLING_SHUTTER
  do {
    MUINT32 arg1 = 0, arg2 = 0;
    _updateCommand<MUINT32, MUINT32>(
        indexDual, NSCam::SENSOR_CMD_GET_SENSOR_ROLLING_SHUTTER, &arg1, &arg2,
        pSource, pTarget);
  } while (0);

  // SENSOR_CMD_GET_VERTICAL_BLANKING
  do {
    MINT32 arg1 = 0, arg2 = 0;
    _updateCommand<MINT32, MINT32>(indexDual,
                                   NSCam::SENSOR_CMD_GET_VERTICAL_BLANKING,
                                   &arg1, &arg2, pSource, pTarget);
  } while (0);
}
#endif

static std::string sensorCommandToString(MUINTPTR cmd) {
  switch (cmd) {
    case NSCam::SENSOR_CMD_SET_SENSOR_EXP_TIME:
      return "SENSOR_CMD_SET_SENSOR_EXP_TIME";
    case NSCam::SENSOR_CMD_SET_SENSOR_EXP_LINE:
      return "SENSOR_CMD_SET_SENSOR_EXP_LINE";
    case NSCam::SENSOR_CMD_SET_SENSOR_GAIN:
      return "SENSOR_CMD_SET_SENSOR_GAIN";
    case NSCam::SENSOR_CMD_SET_SENSOR_DUAL_GAIN:
      return "SENSOR_CMD_SET_SENSOR_DUAL_GAIN";
    case NSCam::SENSOR_CMD_SET_FLICKER_FRAME_RATE:
      return "SENSOR_CMD_SET_FLICKER_FRAME_RATE";
    case NSCam::SENSOR_CMD_SET_VIDEO_FRAME_RATE:
      return "SENSOR_CMD_SET_VIDEO_FRAME_RATE";
    case NSCam::SENSOR_CMD_SET_AE_EXPOSURE_GAIN_SYNC:
      return "SENSOR_CMD_SET_AE_EXPOSURE_GAIN_SYNC";
    case NSCam::SENSOR_CMD_SET_CCT_FEATURE_CONTROL:
      return "SENSOR_CMD_SET_CCT_FEATURE_CONTROL";
    case NSCam::SENSOR_CMD_SET_SENSOR_CALIBRATION_DATA:
      return "SENSOR_CMD_SET_SENSOR_CALIBRATION_DATA";
    case NSCam::SENSOR_CMD_SET_MAX_FRAME_RATE_BY_SCENARIO:
      return "SENSOR_CMD_SET_MAX_FRAME_RATE_BY_SCENARIO";
    case NSCam::SENSOR_CMD_SET_TEST_PATTERN_OUTPUT:
      return "SENSOR_CMD_SET_TEST_PATTERN_OUTPUT";
    case NSCam::SENSOR_CMD_SET_SENSOR_ESHUTTER_GAIN:
      return "SENSOR_CMD_SET_SENSOR_ESHUTTER_GAIN";
    case NSCam::SENSOR_CMD_SET_OB_LOCK:
      return "SENSOR_CMD_SET_OB_LOCK";
    case NSCam::SENSOR_CMD_SET_SENSOR_HDR_SHUTTER_GAIN:
      return "SENSOR_CMD_SET_SENSOR_HDR_SHUTTER_GAIN";
    case NSCam::SENSOR_CMD_SET_SENSOR_HDR_SHUTTER:
      return "SENSOR_CMD_SET_SENSOR_HDR_SHUTTER";
    case NSCam::SENSOR_CMD_SET_SENSOR_HDR_AWB_GAIN:
      return "SENSOR_CMD_SET_SENSOR_HDR_AWB_GAIN";
    case NSCam::SENSOR_CMD_SET_SENSOR_AWB_GAIN:
      return "SENSOR_CMD_SET_SENSOR_AWB_GAIN";
    case NSCam::SENSOR_CMD_SET_SENSOR_ISO:
      return "SENSOR_CMD_SET_SENSOR_ISO";
    case NSCam::SENSOR_CMD_SET_SENSOR_OTP_AWB_CMD:
      return "SENSOR_CMD_SET_SENSOR_OTP_AWB_CMD";
    case NSCam::SENSOR_CMD_SET_SENSOR_OTP_LSC_CMD:
      return "SENSOR_CMD_SET_SENSOR_OTP_LSC_CMD";
    case NSCam::SENSOR_CMD_SET_MIN_MAX_FPS:
      return "SENSOR_CMD_SET_MIN_MAX_FPS";
    case NSCam::SENSOR_CMD_SET_SENSOR_EXP_FRAME_TIME:
      return "SENSOR_CMD_SET_SENSOR_EXP_FRAME_TIME";
    case NSCam::SENSOR_CMD_SET_SENSOR_EXP_TIME_BUF_MODE:
      return "SENSOR_CMD_SET_SENSOR_EXP_TIME_BUF_MODE";
    case NSCam::SENSOR_CMD_SET_SENSOR_EXP_LINE_BUF_MODE:
      return "SENSOR_CMD_SET_SENSOR_EXP_LINE_BUF_MODE";
    case NSCam::SENSOR_CMD_SET_SENSOR_GAIN_BUF_MODE:
      return "SENSOR_CMD_SET_SENSOR_GAIN_BUF_MODE";
    case NSCam::SENSOR_CMD_SET_I2C_BUF_MODE_EN:
      return "SENSOR_CMD_SET_I2C_BUF_MODE_EN";
    case NSCam::SENSOR_CMD_SET_STREAMING_SUSPEND:
      return "SENSOR_CMD_SET_STREAMING_SUSPEND";
    case NSCam::SENSOR_CMD_SET_STREAMING_RESUME:
      return "SENSOR_CMD_SET_STREAMING_RESUME";
    case NSCam::SENSOR_CMD_SET_N3D_I2C_POS:
      return "SENSOR_CMD_SET_N3D_I2C_POS";
    case NSCam::SENSOR_CMD_SET_N3D_I2C_TRIGGER:
      return "SENSOR_CMD_SET_N3D_I2C_TRIGGER";
    case NSCam::SENSOR_CMD_SET_N3D_I2C_STREAM_REGDATA:
      return "SENSOR_CMD_SET_N3D_I2C_STREAM_REGDATA";
    case NSCam::SENSOR_CMD_SET_N3D_START_STREAMING:
      return "SENSOR_CMD_SET_N3D_START_STREAMING";
    case NSCam::SENSOR_CMD_SET_N3D_STOP_STREAMING:
      return "SENSOR_CMD_SET_N3D_STOP_STREAMING";
    case NSCam::SENSOR_CMD_GET_PIXEL_CLOCK_FREQ:
      return "SENSOR_CMD_GET_PIXEL_CLOCK_FREQ";
    case NSCam::SENSOR_CMD_GET_FRAME_SYNC_PIXEL_LINE_NUM:
      return "SENSOR_CMD_GET_FRAME_SYNC_PIXEL_LINE_NUM";
    case NSCam::SENSOR_CMD_GET_SENSOR_FEATURE_INFO:
      return "SENSOR_CMD_GET_SENSOR_FEATURE_INFO";
    case NSCam::SENSOR_CMD_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
      return "SENSOR_CMD_GET_DEFAULT_FRAME_RATE_BY_SCENARIO";
    case NSCam::SENSOR_CMD_GET_TEST_PATTERN_CHECKSUM_VALUE:
      return "SENSOR_CMD_GET_TEST_PATTERN_CHECKSUM_VALUE";
    case NSCam::SENSOR_CMD_GET_TEMPERATURE_VALUE:
      return "SENSOR_CMD_GET_TEMPERATURE_VALUE";
    case NSCam::SENSOR_CMD_GET_SENSOR_CROP_WIN_INFO:
      return "SENSOR_CMD_GET_SENSOR_CROP_WIN_INFO";
    case NSCam::SENSOR_CMD_GET_SENSOR_PIXELMODE:
      return "SENSOR_CMD_GET_SENSOR_PIXELMODE";
    case NSCam::SENSOR_CMD_GET_SENSOR_PDAF_INFO:
      return "SENSOR_CMD_GET_SENSOR_PDAF_INFO";
    case NSCam::SENSOR_CMD_GET_SENSOR_POWER_ON_STATE:
      return "SENSOR_CMD_GET_SENSOR_POWER_ON_STATE";
    case NSCam::SENSOR_CMD_GET_SENSOR_N3D_DIFFERENCE_COUNT:
      return "SENSOR_CMD_GET_SENSOR_N3D_DIFFERENCE_COUNT";
    case NSCam::SENSOR_CMD_GET_SENSOR_N3D_STREAM_TO_VSYNC_TIME:
      return "SENSOR_CMD_GET_SENSOR_N3D_STREAM_TO_VSYNC_TIME";
    case NSCam::SENSOR_CMD_SET_YUV_FEATURE_CMD:
      return "SENSOR_CMD_SET_YUV_FEATURE_CMD";
    case NSCam::SENSOR_CMD_SET_YUV_SINGLE_FOCUS_MODE:
      return "SENSOR_CMD_SET_YUV_SINGLE_FOCUS_MODE";
    case NSCam::SENSOR_CMD_SET_YUV_CANCEL_AF:
      return "SENSOR_CMD_SET_YUV_CANCEL_AF";
    case NSCam::SENSOR_CMD_SET_YUV_CONSTANT_AF:
      return "SENSOR_CMD_SET_YUV_CONSTANT_AF";
    case NSCam::SENSOR_CMD_SET_YUV_INFINITY_AF:
      return "SENSOR_CMD_SET_YUV_INFINITY_AF";
    case NSCam::SENSOR_CMD_SET_YUV_AF_WINDOW:
      return "SENSOR_CMD_SET_YUV_AF_WINDOW";
    case NSCam::SENSOR_CMD_SET_YUV_AE_WINDOW:
      return "SENSOR_CMD_SET_YUV_AE_WINDOW";
    case NSCam::SENSOR_CMD_SET_YUV_AUTOTEST:
      return "SENSOR_CMD_SET_YUV_AUTOTEST";
    case NSCam::SENSOR_CMD_SET_YUV_3A_CMD:
      return "SENSOR_CMD_SET_YUV_3A_CMD";
    case NSCam::SENSOR_CMD_SET_YUV_GAIN_AND_EXP_LINE:
      return "SENSOR_CMD_SET_YUV_GAIN_AND_EXP_LINE";
    case NSCam::SENSOR_CMD_GET_SENSOR_VC_INFO:
      return "SENSOR_CMD_GET_SENSOR_VC_INFO";
    case NSCam::SENSOR_CMD_GET_YUV_AF_STATUS:
      return "SENSOR_CMD_GET_YUV_AF_STATUS";
    case NSCam::SENSOR_CMD_GET_YUV_AE_STATUS:
      return "SENSOR_CMD_GET_YUV_AE_STATUS";
    case NSCam::SENSOR_CMD_GET_YUV_AWB_STATUS:
      return "SENSOR_CMD_GET_YUV_AWB_STATUS";
    case NSCam::SENSOR_CMD_GET_YUV_EV_INFO_AWB_REF_GAIN:
      return "SENSOR_CMD_GET_YUV_EV_INFO_AWB_REF_GAIN";
    case NSCam::SENSOR_CMD_GET_YUV_CURRENT_SHUTTER_GAIN_AWB_GAIN:
      return "SENSOR_CMD_GET_YUV_CURRENT_SHUTTER_GAIN_AWB_GAIN";
    case NSCam::SENSOR_CMD_GET_YUV_AF_MAX_NUM_FOCUS_AREAS:
      return "SENSOR_CMD_GET_YUV_AF_MAX_NUM_FOCUS_AREAS";
    case NSCam::SENSOR_CMD_GET_YUV_AE_MAX_NUM_METERING_AREAS:
      return "SENSOR_CMD_GET_YUV_AE_MAX_NUM_METERING_AREAS";
    case NSCam::SENSOR_CMD_GET_YUV_EXIF_INFO:
      return "SENSOR_CMD_GET_YUV_EXIF_INFO";
    case NSCam::SENSOR_CMD_GET_YUV_DELAY_INFO:
      return "SENSOR_CMD_GET_YUV_DELAY_INFO";
    case NSCam::SENSOR_CMD_GET_YUV_AE_AWB_LOCK:
      return "SENSOR_CMD_GET_YUV_AE_AWB_LOCK";
    case NSCam::SENSOR_CMD_GET_YUV_STROBE_INFO:
      return "SENSOR_CMD_GET_YUV_STROBE_INFO";
    case NSCam::SENSOR_CMD_GET_YUV_TRIGGER_FLASHLIGHT_INFO:
      return "SENSOR_CMD_GET_YUV_TRIGGER_FLASHLIGHT_INFO";
    case NSCam::SENSOR_CMD_GET_PDAF_DATA:
      return "SENSOR_CMD_GET_PDAF_DATA";
    case NSCam::SENSOR_CMD_GET_SENSOR_PDAF_CAPACITY:
      return "SENSOR_CMD_GET_SENSOR_PDAF_CAPACITY";
    case NSCam::SENSOR_CMD_SET_PDFOCUS_AREA:
      return "SENSOR_CMD_SET_PDFOCUS_AREA";
    case NSCam::SENSOR_CMD_GET_SENSOR_ROLLING_SHUTTER:
      return "SENSOR_CMD_GET_SENSOR_ROLLING_SHUTTER";
    case NSCam::SENSOR_CMD_DEBUG_P1_DQ_SENINF_STATUS:
      return "SENSOR_CMD_DEBUG_P1_DQ_SENINF_STATUS";
    case NSCam::SENSOR_CMD_GET_SENSOR_HDR_CAPACITY:
      return "SENSOR_CMD_GET_SENSOR_HDR_CAPACITY";
    case NSCam::SENSOR_CMD_GET_SENSOR_PDAF_REG_SETTING:
      return "SENSOR_CMD_GET_SENSOR_PDAF_REG_SETTING";
    case NSCam::SENSOR_CMD_SET_SENSOR_PDAF_REG_SETTING:
      return "SENSOR_CMD_SET_SENSOR_PDAF_REG_SETTING";
    case NSCam::SENSOR_CMD_GET_4CELL_SENSOR:
      return "SENSOR_CMD_GET_4CELL_SENSOR";
    case NSCam::SENSOR_CMD_SET_SENINF_CAM_TG_MUX:
      return "SENSOR_CMD_SET_SENINF_CAM_TG_MUX";
    case NSCam::SENSOR_CMD_SET_TEST_MODEL:
      return "SENSOR_CMD_SET_TEST_MODEL";
    case NSCam::SENSOR_CMD_DEBUG_GET_SENINF_METER:
      return "SENSOR_CMD_DEBUG_GET_SENINF_METER";
    case NSCam::SENSOR_CMD_GET_MIPI_PIXEL_RATE:
      return "SENSOR_CMD_GET_MIPI_PIXEL_RATE";
    case NSCam::SENSOR_CMD_SET_SENSOR_HDR_ATR:
      return "SENSOR_CMD_SET_SENSOR_HDR_ATR";
    case NSCam::SENSOR_CMD_SET_SENSOR_HDR_TRI_GAIN:
      return "SENSOR_CMD_SET_SENSOR_HDR_TRI_GAIN";
    case NSCam::SENSOR_CMD_SET_SENSOR_HDR_TRI_SHUTTER:
      return "SENSOR_CMD_SET_SENSOR_HDR_TRI_SHUTTER";
    case NSCam::SENSOR_CMD_SET_SENSOR_LSC_TBL:
      return "SENSOR_CMD_SET_SENSOR_LSC_TBL";
    case NSCam::SENSOR_CMD_GET_VERTICAL_BLANKING:
      return "SENSOR_CMD_GET_VERTICAL_BLANKING";
    case NSCam::SENSOR_CMD_SET_VERTICAL_BLANKING:
      return "SENSOR_CMD_SET_VERTICAL_BLANKING";
    case NSCam::SENSOR_CMD_GET_SENSOR_SYNC_MODE_CAPACITY:
      return "SENSOR_CMD_GET_SENSOR_SYNC_MODE_CAPACITY";
    case NSCam::SENSOR_CMD_GET_SENSOR_SYNC_MODE:
      return "SENSOR_CMD_GET_SENSOR_SYNC_MODE";
    case NSCam::SENSOR_CMD_SET_SENSOR_SYNC_MODE:
      return "SENSOR_CMD_SET_SENSOR_SYNC_MODE";
    case NSCam::SENSOR_CMD_SET_DUAL_CAM_MODE:
      return "SENSOR_CMD_SET_DUAL_CAM_MODE";
    case NSCam::SENSOR_CMD_SET_IPC_PING:
      return "SENSOR_CMD_SET_IPC_PING";
    default:
      return "Unknown command";
  }
}

/******************************************************************************
 *
 ******************************************************************************/
HalSensor::~HalSensor() {}

/******************************************************************************
 *
 ******************************************************************************/
HalSensor::HalSensor()
    : mSensorIdx(IMGSENSOR_SENSOR_IDX_NONE),
      mScenarioId(0),
      mHdrMode(0),
      mPdafMode(0),
      mDgainRatio(0),
      mFramerate(0) {
  memset(&mSensorDynamicInfo, 0, sizeof(SensorDynamicInfo));
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
HalSensor::destroyInstance(char const* szCallerName) {
  HalSensorList::singleton()->closeSensor(this, szCallerName);
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
HalSensor::onDestroy() {
  CAM_LOGD("#Sensor:%zu", mSensorData.size());

  std::unique_lock<std::mutex> lk(mMutex);

  if (mSensorIdx == IMGSENSOR_SENSOR_IDX_NONE) {
    mSensorData.clear();
  } else {
    CAM_LOGI("Forget to powerOff before destroying. mSensorIdx:%d", mSensorIdx);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HalSensor::onCreate(vector<MUINT> const& vSensorIndex) {
  CAM_LOGD("+ #Sensor:%zu", vSensorIndex.size());

  std::unique_lock<std::mutex> lk(mMutex);

  mSensorData.clear();
  for (MUINT i = 0; i < vSensorIndex.size(); i++) {
    MUINT const uSensorIndex = vSensorIndex[i];

    mSensorData.push_back(uSensorIndex);
  }

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HalSensor::isMatch(vector<MUINT> const& vSensorIndex) const {
  if (vSensorIndex.size() != mSensorData.size()) {
    return MFALSE;
  }

  auto iter = mSensorData.begin();
  for (MUINT i = 0; i < vSensorIndex.size(); i++, iter++) {
    if (vSensorIndex[i] != *iter) {
      return MFALSE;
    }
  }

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HalSensor::setupLink(int sensorIdx, int flag) {
  int srcEntId = HalSensorList::singleton()->querySensorEntId(sensorIdx);
  int sinkEntId = HalSensorList::singleton()->querySeninfEntId();
  int p1NodeEntId = HalSensorList::singleton()->queryP1NodeEntId();
  const char* dev_name = HalSensorList::singleton()->queryDevName();
  int rc = 0;
  int dev_fd = 0;
  struct media_link_desc linkDesc;
  struct media_pad_desc srcPadDesc, sinkPadDesc;

  CAM_LOGD("setupLink %s (%d %d %d)", dev_name, srcEntId, sinkEntId,
           p1NodeEntId);
  dev_fd = open(dev_name, O_RDWR | O_NONBLOCK);
  if (dev_fd < 0) {
    CAM_LOGD("Open media device error, fd %d", dev_fd);
    return MFALSE;
  }

  // setup link sensor & seninf
  memset(&linkDesc, 0, sizeof(struct media_link_desc));
  srcPadDesc.entity = srcEntId;
  srcPadDesc.index = 0;
  srcPadDesc.flags = MEDIA_PAD_FL_SOURCE;
  sinkPadDesc.entity = sinkEntId;
  sinkPadDesc.index = sensorIdx;
  sinkPadDesc.flags = MEDIA_PAD_FL_SINK;

  linkDesc.source = srcPadDesc;
  linkDesc.sink = sinkPadDesc;
  linkDesc.flags = flag;

  rc = ioctl(dev_fd, MEDIA_IOC_SETUP_LINK, &linkDesc);
  if (rc < 0) {
    CAM_LOGE("Link setup failed @1: %s", strerror(errno));
    close(dev_fd);
    return MFALSE;
  }

  close(dev_fd);

  return MTRUE;
}

MBOOL
HalSensor::powerOn(char const* szCallerName,
                   MUINT const uCountOfIndex,
                   MUINT const* pArrayOfIndex) {
  if (pArrayOfIndex == NULL) {
    CAM_LOGE("powerOn fail, pArrayOfIndex == NULL");
    return MFALSE;
  }

  IMGSENSOR_SENSOR_IDX sensorIdx =
      (IMGSENSOR_SENSOR_IDX)HalSensorList::singleton()
          ->queryEnumInfoByIndex(*pArrayOfIndex)
          ->getDeviceId();
  const char* sensorSubdevName =
      HalSensorList::singleton()->querySensorSubdevName(sensorIdx);
  const char* seninfSubdevName =
      HalSensorList::singleton()->querySeninfSubdevName();
  int sensorNum = HalSensorList::singleton()->queryNumberOfSensors();

  int sensor_fd = 0;
  int seninf_fd = 0;

  CAM_LOGI("powerOn %d %d", *pArrayOfIndex, sensorIdx);

  sensor_fd = open(sensorSubdevName, O_RDWR);
  if (sensor_fd < 0) {
    CAM_LOGE("[%s] open v4l2 sensor subdev fail", __FUNCTION__);
    HalSensorList::singleton()->setSensorFd(sensor_fd, sensorIdx);
    return MFALSE;
  }

  seninf_fd = open(seninfSubdevName, O_RDWR);
  if (sensor_fd < 0) {
    CAM_LOGE("[%s] open v4l2 seninf subdev fail", __FUNCTION__);
    HalSensorList::singleton()->setSeninfFd(seninf_fd);
    return MFALSE;
  }

  HalSensorList::singleton()->setSensorFd(sensor_fd, sensorIdx);
  HalSensorList::singleton()->setSeninfFd(seninf_fd);

  for (int i = 0; i < sensorNum; i++) {
    setupLink(i, 0);  // reset link for all sensors
  }
  setupLink(sensorIdx, MEDIA_LNK_FL_ENABLED);
  mSensorIdx = sensorIdx;

#if MTKCAM_HAVE_SANDBOX_SUPPORT
  // send poweron command and other dynamically data to IPCIHalSensor
  do {
    IHalSensor* pIPCSensor = createIPCHalSensorByIdx(mSensorIdx);
    if (CC_UNLIKELY(pIPCSensor == nullptr)) {
      CAM_LOGE("create IIPCHalSensor failed, sendCommand failed");
      break;
    }

    pIPCSensor->powerOn(nullptr, 1 << mSensorIdx, 0);
    sendDataToIPCHalSensor(this, static_cast<IIPCHalSensor*>(pIPCSensor),
                           1 << mSensorIdx);
    pIPCSensor->destroyInstance();
  } while (0);
#endif

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HalSensor::powerOff(char const* szCallerName,
                    MUINT const uCountOfIndex,
                    MUINT const* pArrayOfIndex) {
  IMGSENSOR_SENSOR_IDX sensorIdx =
      (IMGSENSOR_SENSOR_IDX)HalSensorList::singleton()
          ->queryEnumInfoByIndex(*pArrayOfIndex)
          ->getDeviceId();
  int sensor_fd = HalSensorList::singleton()->querySensorFd(sensorIdx);
  int seninf_fd = HalSensorList::singleton()->querySeninfFd();

  CAM_LOGI("powerOff %d %d", *pArrayOfIndex, sensorIdx);

  if (sensor_fd >= 0) {
    close(sensor_fd);
  }
  if (seninf_fd >= 0) {
    close(seninf_fd);
  }
#if MTKCAM_HAVE_SANDBOX_SUPPORT
  // send poweron command to
  do {
    IHalSensor* pIPCSensor = createIPCHalSensorByIdx(mSensorIdx);
    if (CC_UNLIKELY(pIPCSensor == nullptr)) {
      CAM_LOGE("create IIPCHalSensor failed, sendCommand failed");
      break;
    }
    pIPCSensor->powerOff(nullptr, 0, 0);
    pIPCSensor->destroyInstance();
  } while (0);
#endif

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL HalSensor::querySensorDynamicInfo(MUINT32 indexDual,
                                        SensorDynamicInfo* pSensorDynamicInfo) {
  if (pSensorDynamicInfo == NULL) {
    CAM_LOGE("querySensorDynamicInfo fail, pSensorDynamicInfo is NULL");
    return MFALSE;
  }
  memcpy(pSensorDynamicInfo, &mSensorDynamicInfo, sizeof(SensorDynamicInfo));

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL HalSensor::configure(MUINT const uCountOfParam,
                           ConfigParam const* pConfigParam) {
  MINT32 ret = MFALSE;

  if (pConfigParam == NULL) {
    CAM_LOGE("configure fail, pConfigParam is NULL");
    return MFALSE;
  }

  IMGSENSOR_SENSOR_IDX sensorIdx =
      (IMGSENSOR_SENSOR_IDX)HalSensorList::singleton()
          ->queryEnumInfoByIndex(pConfigParam->index)
          ->getDeviceId();
  int sensor_fd = HalSensorList::singleton()->querySensorFd(sensorIdx);
  int seninf_fd = HalSensorList::singleton()->querySeninfFd();

  struct v4l2_subdev_format aFormat;
  SensorDynamicInfo* pSensorDynamicInfo = &mSensorDynamicInfo;
  unsigned int width = 0;
  unsigned int height = 0;
  unsigned int framelength = 0;
  unsigned int line_length = 0;
  unsigned int pix_clk = 0;

  (void)uCountOfParam;
  std::unique_lock<std::mutex> lk(mMutex);

  CAM_LOGI("configure sensorIdx (%d)", sensorIdx);

  struct imgsensor_info_struct* pImgsensorInfo =
      HalSensorList::singleton()->getSensorInfo(sensorIdx);
  if (pImgsensorInfo == NULL) {
    CAM_LOGE("configure fail, cannot get sensor info");
    return MFALSE;
  }

  if (mSensorIdx == IMGSENSOR_SENSOR_IDX_NONE || mSensorIdx != sensorIdx) {
    CAM_LOGE("configure fail. mSensorIdx = %d, sensorIdx = %d", mSensorIdx,
             sensorIdx);
    return MFALSE;
  }

  pSensorDynamicInfo->pixelMode = SENINF_PIXEL_MODE_CAM;
  pSensorDynamicInfo->HDRPixelMode = pSensorDynamicInfo->PDAFPixelMode =
      SENINF_PIXEL_MODE_CAMSV;

  pSensorDynamicInfo->TgInfo = pSensorDynamicInfo->HDRInfo =
      pSensorDynamicInfo->PDAFInfo = CAM_TG_NONE;

  mScenarioId = pConfigParam->scenarioId;
  CAM_LOGD("pConfigParam->scenarioId %d", pConfigParam->scenarioId);

  switch (mScenarioId) {
    case SENSOR_SCENARIO_ID_NORMAL_CAPTURE:
      width = pImgsensorInfo->cap.grabwindow_width;
      height = pImgsensorInfo->cap.grabwindow_height;
      pix_clk = pImgsensorInfo->cap.pclk;
      line_length = pImgsensorInfo->cap.linelength;
      framelength = pImgsensorInfo->cap.framelength;
      break;
    case SENSOR_SCENARIO_ID_NORMAL_PREVIEW:
      width = pImgsensorInfo->pre.grabwindow_width;
      height = pImgsensorInfo->pre.grabwindow_height;
      pix_clk = pImgsensorInfo->pre.pclk;
      line_length = pImgsensorInfo->pre.linelength;
      framelength = pImgsensorInfo->pre.framelength;
      break;
    case SENSOR_SCENARIO_ID_NORMAL_VIDEO:
      width = pImgsensorInfo->normal_video.grabwindow_width;
      height = pImgsensorInfo->normal_video.grabwindow_height;
      pix_clk = pImgsensorInfo->normal_video.pclk;
      line_length = pImgsensorInfo->normal_video.linelength;
      framelength = pImgsensorInfo->normal_video.framelength;
      break;
    case SENSOR_SCENARIO_ID_SLIM_VIDEO1:
      width = pImgsensorInfo->hs_video.grabwindow_width;
      height = pImgsensorInfo->hs_video.grabwindow_height;
      pix_clk = pImgsensorInfo->hs_video.pclk;
      line_length = pImgsensorInfo->hs_video.linelength;
      framelength = pImgsensorInfo->hs_video.framelength;
      break;
    case SENSOR_SCENARIO_ID_SLIM_VIDEO2:
      width = pImgsensorInfo->slim_video.grabwindow_width;
      height = pImgsensorInfo->slim_video.grabwindow_height;
      pix_clk = pImgsensorInfo->slim_video.pclk;
      line_length = pImgsensorInfo->slim_video.linelength;
      framelength = pImgsensorInfo->slim_video.framelength;
      break;
    default:
      width = pImgsensorInfo->cap.grabwindow_width;
      height = pImgsensorInfo->cap.grabwindow_height;
      pix_clk = pImgsensorInfo->cap.pclk;
      line_length = pImgsensorInfo->cap.linelength;
      framelength = pImgsensorInfo->cap.framelength;
      break;
  }

  m_vblank = framelength - height;
  m_pixClk = pix_clk;
  m_linelength = line_length;
  m_framelength = framelength;
  m_margin = pImgsensorInfo->margin;
  m_minShutter = pImgsensorInfo->min_shutter;
  m_maxFramelength = pImgsensorInfo->max_frame_length;
  m_LineTimeInus = (line_length * 1000000 + ((pix_clk / 1000) - 1)) /
                   (pix_clk / 1000);  // 1000 base , 33657 mean 33.657 us
  m_SensorGainFactor = pImgsensorInfo->SensorGainfactor;
  m_SensorGainBase = GAIN_BASE_3A >> m_SensorGainFactor;
  mDgainRatio = m_SensorGainBase;
  m_SensorGainMapSize = sizeof(pImgsensorInfo->sensor_agc_param_map) /
                        sizeof(pImgsensorInfo->sensor_agc_param_map[0]);
  m_SensorAgcParam = pImgsensorInfo->sensor_agc_param_map;
  if (!m_SensorAgcParam) {
    CAM_LOGW("sensorIdx (%d), m_SensorAgcParam is NULL\n", sensorIdx);
  }

  aFormat.pad = 0;
  aFormat.which = V4L2_SUBDEV_FORMAT_ACTIVE;
  aFormat.format.width = width;
  aFormat.format.height = height;
  ret = ioctl(sensor_fd, VIDIOC_SUBDEV_S_FMT, &aFormat);
  if (ret < 0) {
    CAM_LOGE("set sensor format fail");
    return MFALSE;
  }
  // set seninf format the same as sensor format to avoid link invalid
  ret = ioctl(sensor_fd, VIDIOC_SUBDEV_G_FMT, &aFormat);
  if (ret < 0) {
    CAM_LOGE("get sensor format fail");
    return MFALSE;
  }

  aFormat.pad = sensorIdx;
  ret = ioctl(seninf_fd, VIDIOC_SUBDEV_S_FMT, &aFormat);
  if (ret < 0) {
    CAM_LOGE("set seninf format fail");
    return MFALSE;
  }

  /* send data to IPC sensor again */
  do {
    IHalSensor* pIPCSensor = createIPCHalSensorByIdx(mSensorIdx);
    if (CC_UNLIKELY(pIPCSensor == nullptr)) {
      CAM_LOGE("create IIPCHalSensor failed, sendCommand failed");
      break;
    }

    sendDataToIPCHalSensor(this, static_cast<IIPCHalSensor*>(pIPCSensor),
                           1 << mSensorIdx);
    pIPCSensor->destroyInstance();
  } while (0);

  return (ret == 0);
}

/******************************************************************************
 *
 ******************************************************************************/
MINT HalSensor::sendCommand(MUINT indexDual,
                            MUINTPTR cmd,
                            MUINTPTR arg1,
                            MUINT arg1_size,
                            MUINTPTR arg2,
                            MUINT arg2_size,
                            MUINTPTR arg3,
                            MUINT arg3_size) {
  MINT32 ret = 0;
  IMGSENSOR_SENSOR_IDX sensorIdx = IMGSENSOR_SENSOR_IDX_MAP(indexDual);
  int sensor_fd = HalSensorList::singleton()->querySensorFd(sensorIdx);
  v4l2_control control;
  unsigned int u32temp = 0, u32temp1 = 0, u32temp2 = 0;

  switch (cmd) {
    case SENSOR_CMD_GET_SENSOR_PIXELMODE:
      if ((reinterpret_cast<MUINT32*>(arg3) != NULL) &&
          (arg3_size == sizeof(MUINT32))) {
        *reinterpret_cast<MUINT32*>(arg3) = mSensorDynamicInfo.pixelMode;
      } else {
        CAM_LOGE("%s(0x%x) wrong input params", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_GET_SENSOR_POWER_ON_STATE:  // LSC funciton need open after
                                                // sensor Power On
      if ((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MUINT32))) {
        *(reinterpret_cast<MUINT32*>(arg1)) =
            (mSensorIdx != IMGSENSOR_SENSOR_IDX_NONE) ? 1 << mSensorIdx : 0;
      } else {
        CAM_LOGE("%s(0x%x) wrong input params", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_GET_SENSOR_CROP_WIN_INFO:
      if (((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
           (arg1_size == sizeof(MUINT32))) &&
          ((reinterpret_cast<MUINT32*>(arg2) != NULL) &&
           (arg2_size == sizeof(SENSOR_WINSIZE_INFO_STRUCT)))) {
        SENSOR_WINSIZE_INFO_STRUCT* ptr;
        ptr = HalSensorList::singleton()->getWinSizeInfo(
            sensorIdx, *reinterpret_cast<MUINT32*>(arg1));
        if (ptr) {
          memcpy(reinterpret_cast<void*>(arg2), reinterpret_cast<void*>(ptr),
                 sizeof(SENSOR_WINSIZE_INFO_STRUCT));
        }
      } else {
        CAM_LOGE("%s(0x%x) wrong input params", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_SET_MAX_FRAME_RATE_BY_SCENARIO:
      if ((reinterpret_cast<MUINT32*>(arg2) != NULL) &&
          (arg2_size == sizeof(MUINT32))) {
        u32temp = *reinterpret_cast<MUINT32*>(arg2);  // get framerate is 10x,
                                                      // namely 100 for 10 fps
        u32temp = ((1000 * 1000000) / u32temp / m_LineTimeInus) * 10;
        mFramerate = u32temp;
        control.id = V4L2_CID_VBLANK;
        control.value = (u32temp > m_framelength)
                            ? (u32temp - m_framelength + m_vblank)
                            : m_vblank;
        ret = ioctl(sensor_fd, VIDIOC_S_CTRL, &control);
        if (ret < 0) {
          CAM_LOGE("[%s] set max framerate fail %d", __FUNCTION__,
                   control.value);
        }
        CAM_LOGD("set max framerate %d, mFramerate %d control.value %d",
                 *(MUINT32*)arg2, mFramerate, control.value);
      } else {
        CAM_LOGE("%s(0x%x) wrong input params", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_SET_SENSOR_GAIN:
      if ((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MUINT32))) {
        if (m_SensorAgcParam->auto_pregain) {
          u32temp = *reinterpret_cast<MUINT32*>(arg1);
          u32temp1 = u32temp >> m_SensorGainFactor;
          for (u32temp2 = m_SensorGainMapSize - 1; u32temp2 > 0; u32temp2--) {
            if (u32temp1 >= m_SensorAgcParam[u32temp2].auto_pregain) {
              break;
            }
          }

          control.id = V4L2_CID_ANALOGUE_GAIN;
          control.value = m_SensorAgcParam[u32temp2].col_code;
          ret = ioctl(sensor_fd, VIDIOC_S_CTRL, &control);
          if (ret < 0) {
            CAM_LOGE("[%s] set SENSOR A-GAIN fail %d\n", __FUNCTION__,
                     control.value);
          }

          if (m_SensorAgcParam[u32temp2].auto_pregain) {
            u32temp1 = u32temp1 * mDgainRatio /
                       (m_SensorAgcParam[u32temp2].auto_pregain);
          } else {
            CAM_LOGE("AGC index (%d), auto_pregain is NULL\n", u32temp2);
            return MFALSE;
          }

          CAM_LOGD("Mapped AGC PARAM pregain(%d)\n",
                   m_SensorAgcParam[u32temp2].auto_pregain);
          control.id = V4L2_CID_DIGITAL_GAIN;
          control.value = u32temp1;
          ret = ioctl(sensor_fd, VIDIOC_S_CTRL, &control);
          if (ret < 0) {
            CAM_LOGE("[%s] set SENSOR D-GAIN fail %d\n", __FUNCTION__,
                     control.value);
          }
        } else {
          control.id = V4L2_CID_ANALOGUE_GAIN;
          u32temp = *reinterpret_cast<MUINT32*>(arg1);
          control.value = u32temp >> m_SensorGainFactor;
          ret = ioctl(sensor_fd, VIDIOC_S_CTRL, &control);
          if (ret < 0) {
            CAM_LOGE("[%s] set SENSOR A-GAIN fail %d\n", __FUNCTION__,
                     control.value);
          }
        }
      } else {
        CAM_LOGE("%s(0x%x) wrong input params\n", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_SET_SENSOR_EXP_TIME:
      if ((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MUINT32))) {
        u32temp = *reinterpret_cast<MUINT32*>(arg1);
        u32temp = ((1000 * (u32temp)) / m_LineTimeInus);
        u32temp1 = (u32temp > mFramerate) ? u32temp : mFramerate;
        control.id = V4L2_CID_VBLANK;
        control.value = (u32temp1 > m_framelength)
                            ? (u32temp1 - m_framelength + m_vblank)
                            : m_vblank;
        ret = ioctl(sensor_fd, VIDIOC_S_CTRL, &control);
        if (ret < 0) {
          CAM_LOGE("[%s] set SENSOR VBLANK fail %d", __FUNCTION__,
                   control.value);
        }
        u32temp = (u32temp < m_minShutter) ? m_minShutter : u32temp;
        u32temp = (u32temp > (m_maxFramelength - m_margin))
                      ? (m_maxFramelength - m_margin)
                      : u32temp;
        u32temp1 = u32temp & ~3;
        if (u32temp1 > 0) {
          mDgainRatio = m_SensorGainBase * u32temp / u32temp1;
        } else {
          CAM_LOGW("[%s] too small exp-lines, using SensorGainBase\n",
                   __FUNCTION__);
        }
        control.id = V4L2_CID_EXPOSURE;
        control.value = u32temp1;
        ret = ioctl(sensor_fd, VIDIOC_S_CTRL, &control);
        if (ret < 0) {
          CAM_LOGE("[%s] set SENSOR EXPOSURE fail %d", __FUNCTION__,
                   control.value);
        }
      } else {
        CAM_LOGE("%s(0x%x) wrong input params", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_GET_PIXEL_CLOCK_FREQ:
      if ((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MUINT32))) {
        *reinterpret_cast<MUINT32*>(arg1) = m_pixClk;
      } else {
        CAM_LOGE("%s(0x%x) wrong input params", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_GET_FRAME_SYNC_PIXEL_LINE_NUM:
      if ((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MUINT32))) {
        *reinterpret_cast<MUINT32*>(arg1) =
            (m_framelength << 16) + m_linelength;
      } else {
        CAM_LOGE("%s(0x%x) wrong input params", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_SET_TEST_PATTERN_OUTPUT:
      if ((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MUINT32))) {
        u32temp = *reinterpret_cast<MUINT32*>(arg1);
        if (u32temp) {
          // api color bar arg is 2, but sensor driver color bar index is 1
          control.value = u32temp - 1;
        } else {
          control.value = u32temp;
        }
        if (control.value < 0) {
          CAM_LOGE("[%s] invalid pattern mode %d", __FUNCTION__, control.value);
          break;
        }
        control.id = V4L2_CID_TEST_PATTERN;
        ret = ioctl(sensor_fd, VIDIOC_S_CTRL, &control);
        if (ret < 0) {
          CAM_LOGE("[%s] set SENSOR TEST PATTERN fail 0x%x", __FUNCTION__,
                   control.value);
        }
      } else {
        CAM_LOGE("%s(0x%x) wrong input params", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_GET_SENSOR_ROLLING_SHUTTER:
      if ((reinterpret_cast<MINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MINT32))) {
        SENSOR_WINSIZE_INFO_STRUCT* cropInfo;
        cropInfo =
            HalSensorList::singleton()->getWinSizeInfo(sensorIdx, mScenarioId);
        if (!cropInfo) {
          *reinterpret_cast<MINT32*>(arg1) = 0;
          CAM_LOGE("Null cropInfo");
        } else if (m_pixClk != 0) {
          MINT64 tg_size = cropInfo->h2_tg_size;
          *reinterpret_cast<MINT32*>(arg1) =
              ((m_linelength * tg_size * 1000000000) / m_pixClk);
          CAM_LOGD("arg1:%d", *reinterpret_cast<MINT32*>(arg1));
          ret = MTRUE;
        } else {
          *reinterpret_cast<MINT32*>(arg1) = 0;
          CAM_LOGE("Wrong pixel clock");
        }
      } else {
        CAM_LOGE("%s(0x%x) wrong input params", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_GET_SENSOR_VC_INFO:
    case SENSOR_CMD_GET_SENSOR_PDAF_INFO:
    case SENSOR_CMD_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
    case SENSOR_CMD_GET_SENSOR_PDAF_CAPACITY:
    case SENSOR_CMD_GET_VERTICAL_BLANKING:
    case SENSOR_CMD_SET_FLICKER_FRAME_RATE:
    case SENSOR_CMD_SET_OB_LOCK:
      CAM_LOGD("TODO sendCommand(0x%x)", cmd);
      ret = MFALSE;
      break;

    case SENSOR_CMD_SET_IPC_PING:
      // ping message for sensor ipc to indicate 3a alive
      ret = MTRUE;
      break;

    default:
      CAM_LOGE("Unsupported sendCommand %s(0x%x)",
               sensorCommandToString(cmd).c_str(), cmd);
      ret = MFALSE;
      break;
  }

  return ret;
}

/*******************************************************************************
 *
 ******************************************************************************/
MINT32 HalSensor::setDebugInfo(IBaseCamExif* pIBaseCamExif) {
  (void)pIBaseCamExif;
  return 0;
}
MINT32 HalSensor::reset() {
  return 0;
}
