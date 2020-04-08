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

#define LOG_TAG "MtkCam/HalSensorList"

#include <algorithm>
#include <cctype>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <mtkcam/utils/metadata/IMetadata.h>
#include <mtkcam/utils/metadata/client/mtk_metadata_tag.h>
#include <mtkcam/v4l2/IPCIHalSensor.h>
#include <cutils/compiler.h>  // for CC_LIKELY, CC_UNLIKELY

#include "mtkcam/custom/mt8183/hal/inc/camera_custom_imgsensor_cfg.h"
#include "mtkcam/drv/sensor/HalSensorList.h"
#include "mtkcam/drv/sensor/img_sensor.h"
#include "mtkcam/drv/sensor/MyUtils.h"
#include <mtkcam/utils/TuningUtils/TuningPlatformInfo.h>

#include <base/files/file_enumerator.h>
#include <sys/ioctl.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>

#define MTK_SUB_IMGSENSOR
#define MAX_ENTITY_CNT 255

#define MAIN_SENSOR_I2C_NUM 2
#define SUB_SENSOR_I2C_NUM 4

#ifdef USING_MTK_LDVT
#include <uvvf.h>
#endif
using NSCam::IMetadata;
using NSCam::SensorStaticInfo;
using NSCamCustomSensor::CUSTOM_CFG;
/******************************************************************************
 *
 ******************************************************************************/
IHalSensorList* IHalSensorList::getInstance() {
  return HalSensorList::singleton();
}

/******************************************************************************
 *
 ******************************************************************************/
HalSensorList* HalSensorList::singleton() {
  static HalSensorList* inst = new HalSensorList();
  return inst;
}

/******************************************************************************
 *
 ******************************************************************************/
HalSensorList::HalSensorList() : IHalSensorList() {}

/******************************************************************************
 *
 ******************************************************************************/
static NSCam::NSSensorType::Type mapToSensorType(
    const IMAGE_SENSOR_TYPE sensor_type) {
  NSCam::NSSensorType::Type eSensorType;

  switch (sensor_type) {
    case IMAGE_SENSOR_TYPE_RAW:
    case IMAGE_SENSOR_TYPE_RAW8:
    case IMAGE_SENSOR_TYPE_RAW12:
    case IMAGE_SENSOR_TYPE_RAW14:
      eSensorType = NSCam::NSSensorType::eRAW;
      break;

    case IMAGE_SENSOR_TYPE_YUV:
    case IMAGE_SENSOR_TYPE_YCBCR:
    case IMAGE_SENSOR_TYPE_RGB565:
    case IMAGE_SENSOR_TYPE_RGB888:
    case IMAGE_SENSOR_TYPE_JPEG:
      eSensorType = NSCam::NSSensorType::eYUV;
      break;

    default:
      eSensorType = NSCam::NSSensorType::eRAW;
      break;
  }

  return eSensorType;
}

/******************************************************************************
 *
 ******************************************************************************/
const char* HalSensorList::queryDevName() {
  return devName.c_str();
}

int HalSensorList::queryP1NodeEntId() {
  return p1NodeEntId;
}

int HalSensorList::querySeninfEntId() {
  return seninfEntId;
}

int HalSensorList::querySensorEntId(MUINT const index) {
  if (index < IMGSENSOR_SENSOR_IDX_MAX_NUM) {
    return sensorEntId[index];
  } else {
    return MFALSE;
  }
}

const char* HalSensorList::querySeninfSubdevName() {
  return seninfSubdevName.c_str();
}

const char* HalSensorList::querySensorSubdevName(MUINT const index) {
  if (index < IMGSENSOR_SENSOR_IDX_MAX_NUM) {
    return sensorSubdevName[index].c_str();
  } else {
    return NULL;
  }
}

int HalSensorList::querySeninfFd() {
  return seninfFd;
}

MVOID HalSensorList::setSeninfFd(int fd) {
  seninfFd = fd;
}

int HalSensorList::querySensorFd(MUINT const index) {
  if (index < IMGSENSOR_SENSOR_IDX_MAX_NUM) {
    return sensorFd[index];
  } else {
    return -MFALSE;
  }
}

MVOID HalSensorList::setSensorFd(int fd, MUINT const index) {
  if (index < IMGSENSOR_SENSOR_IDX_MAX_NUM) {
    sensorFd[index] = fd;
  }
}

// ov5695 2-0036 (entity_name)
// ov5695_mipi_raw (sensor_name)
int findsensor(std::string entity_name, int* id) {
  int find_cnt;
  int ret = 0;

  std::string sensor_name;
  for (int i = 0; i < MAX_NUM_OF_SUPPORT_SENSOR; i++) {
    *id = getSensorListId(i);
    if (*id == 0) {
      break;
    }
    sensor_name = getSensorListName(i);
    find_cnt = sensor_name.find_first_of("_");
    sensor_name =
        sensor_name.substr(0, find_cnt);  // get sensor id string, the string in
                                          // sensor_name before "_"
    if (entity_name.find(sensor_name) != std::string::npos) {
      find_cnt = entity_name.find_first_of("-");
      entity_name = entity_name.substr(
          find_cnt - 1, 1);  // get i2c num, one char in entity_name before "-"
      ret = atoi(entity_name.c_str());  // return i2c num
      *id = getSensorListId(i);
      CAM_LOGI("%d 0x%x", ret, *id);
      break;
    }
  }
  return ret;
}

int HalSensorList::findSubdev(void) {
  struct media_device_info mdev_info;
  int findsensorif = 0;
  int findcamio = 0;
  std::string seninf_name("seninf");
  std::string p1_node_name("mtk-cam-p1");
  std::string entity_name;
  char subdev_name[32];
  int rc = 0;
  int dev_fd = 0;
  int find_cnt;
  int id;
  int i2c_num;

  CAM_LOGI("[%s] start ", __FUNCTION__);

  const base::FilePath path("/dev/media?");
  base::FileEnumerator enumerator(path.DirName(), false,
                                  base::FileEnumerator::FILES,
                                  path.BaseName().value());

  while (!enumerator.Next().empty()) {
    int num_entities = 1;

    if (findsensorif && (sensor_nums == 2) && findcamio)
      break;

    const base::FileEnumerator::FileInfo info = enumerator.GetInfo();
    const std::string name = info.GetName().value();
    const base::FilePath target_path = path.DirName().Append(name);

    CAM_LOGI("[%s] media dev name [%s] ", __FUNCTION__,
             target_path.value().c_str());
    dev_fd = open(target_path.value().c_str(), O_RDWR | O_NONBLOCK);
    if (dev_fd < 0) {
      CAM_LOGE("[%s] Open %s error, %d %s", __FUNCTION__,
               target_path.value().c_str(), errno, strerror(errno));
      rc = -1;
      continue;
    }

    rc = ioctl(dev_fd, MEDIA_IOC_DEVICE_INFO, &mdev_info);
    if (rc < 0) {
      CAM_LOGD("MEDIA_IOC_DEVICE_INFO error, rc %d", rc);
      close(dev_fd);
      continue;
    }

    find_cnt = MAX_ENTITY_CNT;
    while (find_cnt) {
      struct media_entity_desc entity = {};
      entity.id = num_entities++;
      find_cnt--;
      if (findsensorif && (sensor_nums == 2) && findcamio) {
        break;
      }
      rc = ioctl(dev_fd, MEDIA_IOC_ENUM_ENTITIES, &entity);
      if (rc < 0) {
        rc = 0;
        continue;
      }

      entity_name = entity.name;
      if (entity_name.find(p1_node_name) != std::string::npos &&
          entity.type ==
              MEDIA_ENT_T_V4L2_SUBDEV) {  // tmp : force to user main sensor
        snprintf(subdev_name, sizeof(subdev_name), "/dev/char/%d:%d",
                 entity.dev.major, entity.dev.minor);
        p1NodeName = subdev_name;
        CAM_LOGI("camio subdevname[%s]-(%d)", p1NodeName.c_str(), entity.id);
        findcamio = 1;
        p1NodeEntId = entity.id;
      }
      i2c_num = findsensor(entity_name, &id);
      int index = -1;
      if (i2c_num == MAIN_SENSOR_I2C_NUM) {
        index = 0;
      } else if (i2c_num == SUB_SENSOR_I2C_NUM) {
        index = 1;
      }
      if (index >= 0) {
        snprintf(subdev_name, sizeof(subdev_name), "/dev/char/%d:%d",
                 entity.dev.major, entity.dev.minor);
        sensorSubdevName[index] = subdev_name;
        CAM_LOGI("sensor 0 subdevname[%s]-(%d) 0x%x",
                 sensorSubdevName[0].c_str(), entity.id, id);
        sensor_nums++;
        sensorEntId[index] = entity.id;
        sensorId[index] = id;
      }
      if (entity_name.find(seninf_name) != std::string::npos) {
        snprintf(subdev_name, sizeof(subdev_name), "/dev/char/%d:%d",
                 entity.dev.major, entity.dev.minor);
        seninfSubdevName = subdev_name;
        CAM_LOGI("seninf subdevname[%s]-(%d)", seninfSubdevName.c_str(),
                 entity.id);
        devName = target_path.value();
        CAM_LOGI("devName %s", devName.c_str());
        seninfEntId = entity.id;
        findsensorif = 1;
      }
    }

    if (dev_fd >= 0) {
      close(dev_fd);
    }
  }

  CAM_LOGI("[%s] end ", __FUNCTION__);

  return rc;
}

MUINT
HalSensorList::searchSensors() {
  std::unique_lock<std::mutex> lk(mEnumSensorMutex);

  CAM_LOGI("searchSensors");
  findSubdev();
  CAM_LOGI("sensor_nums %d", sensor_nums);

  if (sensor_nums == 0)
    return 0;

#ifdef MTK_SUB_IMGSENSOR
  CAM_LOGD("impSearchSensor search to sub");
  for (MUINT i = IMGSENSOR_SENSOR_IDX_MIN_NUM; i <= IMGSENSOR_SENSOR_IDX_SUB;
       i++) {
#else
  CAM_LOGD("impSearchSensor search to main");
  for (MUINT i = IMGSENSOR_SENSOR_IDX_MIN_NUM; i < IMGSENSOR_SENSOR_IDX_SUB;
       i++) {
#endif
    // query sensorinfo
    querySensorInfo((IMGSENSOR_SENSOR_IDX)i);
    // fill in metadata
    buildSensorMetadata((IMGSENSOR_SENSOR_IDX)i);
    addAndInitSensorEnumInfo_Locked(
        (IMGSENSOR_SENSOR_IDX)i,
        mapToSensorType(
            (IMAGE_SENSOR_TYPE)getSensorType((IMGSENSOR_SENSOR_IDX)i)),
        reinterpret_cast<const char*>(getSensorName((IMGSENSOR_SENSOR_IDX)i)));
  }

  // If support SANDBOX, we have to saves SensorStaticInfo to IIPCHalSensorList
  // while the SensorStaticInfo has been updated.
#ifdef MTKCAM_HAVE_SANDBOX_SUPPORT
  IIPCHalSensorListProv* pIPCHalSensorList =
      IIPCHalSensorListProv::getInstance();
  if (CC_UNLIKELY(pIPCHalSensorList == nullptr)) {
    CAM_LOGE("IIPCHalSensorListProv is nullptr");
  } else {
    for (size_t i = 0; i < mEnumSensorList.size(); i++) {
      const EnumInfo& info = mEnumSensorList[i];
      // retrieve sensor static info, device id and type
      // and saves into IIPCIHalSensorList
      MUINT32 type = info.getSensorType();
      MUINT32 deviceId = info.getDeviceId();

      const SensorStaticInfo* staticInfo =
          gQuerySensorStaticInfo(static_cast<IMGSENSOR_SENSOR_IDX>(deviceId));

      if (CC_UNLIKELY(staticInfo == nullptr)) {
        CAM_LOGW("no static info of sensor device %u", deviceId);
        continue;
      }

      pIPCHalSensorList->ipcSetSensorStaticInfo(i, type, deviceId, *staticInfo);

      pIPCHalSensorList->ipcSetStaticInfo(i, info.mMetadata);

      CAM_LOGD("IPCHalSensorList: sensor (idx,type,deviceid)=(%#x, %#x, %#x)",
               i, type, deviceId);
    }
    if (CC_UNLIKELY(mEnumSensorList.empty())) {
      CAM_LOGW("no enumerated sensor (mEnumSensorList.size() is 0)");
    }
  }
#endif

  return sensor_nums;
}

/******************************************************************************
 *
 ******************************************************************************/
MUINT
HalSensorList::queryNumberOfSensors() const {
  std::unique_lock<std::mutex> lk(mEnumSensorMutex);
  return sensor_nums;
}

/******************************************************************************
 *
 ******************************************************************************/
IMetadata const& HalSensorList::queryStaticInfo(MUINT const index) const {
  EnumInfo const* pInfo = queryEnumInfoByIndex(index);
  MY_LOGF_IF(pInfo == NULL, "NULL EnumInfo for sensor %d", index);

  return pInfo->mMetadata;
}

/******************************************************************************
 *
 ******************************************************************************/
char const* HalSensorList::queryDriverName(MUINT const index) const {
  EnumInfo const* pInfo = queryEnumInfoByIndex(index);
  MY_LOGF_IF(pInfo == NULL, "NULL EnumInfo for sensor %d", index);
  return (char const*)pInfo->getSensorDrvName().c_str();
}

/******************************************************************************
 *
 ******************************************************************************/
MUINT
HalSensorList::queryType(MUINT const index) const {
  EnumInfo const* pInfo = queryEnumInfoByIndex(index);
  MY_LOGF_IF(pInfo == NULL, "NULL EnumInfo for sensor %d", index);

  return pInfo->getSensorType();
}

/******************************************************************************
 *
 ******************************************************************************/
MUINT
HalSensorList::queryFacingDirection(MUINT const index) const {
  if (SensorStaticInfo const* p = querySensorStaticInfo(index)) {
    return p->facingDirection;
  }
  return 0;
}

/******************************************************************************
 *
 ******************************************************************************/
MUINT
HalSensorList::querySensorDevIdx(MUINT const index) const {
  const EnumInfo* pEnumInfo = queryEnumInfoByIndex(index);
  return (pEnumInfo) ? IMGSENSOR_SENSOR_IDX2DUAL(pEnumInfo->getDeviceId()) : 0;
}

/******************************************************************************
 *
 ******************************************************************************/
SensorStaticInfo const* HalSensorList::gQuerySensorStaticInfo(
    IMGSENSOR_SENSOR_IDX sensorIDX) const {
  if (sensorIDX >= IMGSENSOR_SENSOR_IDX_MIN_NUM &&
      sensorIDX < IMGSENSOR_SENSOR_IDX_MAX_NUM) {
    return &sensorStaticInfo[sensorIDX];
  } else {
    CAM_LOGE("bad sensorDev:%#x", sensorIDX);
    return NULL;
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
HalSensorList::querySensorStaticInfo(
    MUINT indexDual, SensorStaticInfo* pSensorStaticInfo) const {
  std::unique_lock<std::mutex> lk(mEnumSensorMutex);

  SensorStaticInfo const* pSensorInfo =
      gQuerySensorStaticInfo(IMGSENSOR_SENSOR_IDX_MAP(indexDual));
  if (pSensorStaticInfo != NULL && pSensorInfo != NULL) {
    ::memcpy(pSensorStaticInfo, pSensorInfo, sizeof(SensorStaticInfo));
  }
}

/******************************************************************************
 *
 ******************************************************************************/
SensorStaticInfo const* HalSensorList::querySensorStaticInfo(
    MUINT const index) const {
  EnumInfo const* pEnumInfo = queryEnumInfoByIndex(index);
  if (!pEnumInfo) {
    CAM_LOGE("No EnumInfo for index:%d", index);
    return NULL;
  }

  std::unique_lock<std::mutex> lk(mEnumSensorMutex);

  return gQuerySensorStaticInfo((IMGSENSOR_SENSOR_IDX)pEnumInfo->getDeviceId());
}

/******************************************************************************
 *
 ******************************************************************************/
HalSensorList::OpenInfo::OpenInfo(MINT iRefCount, HalSensor* pHalSensor)
    : miRefCount(iRefCount), mpHalSensor(pHalSensor) {}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
HalSensorList::closeSensor(HalSensor* pHalSensor, char const* szCallerName) {
  std::unique_lock<std::mutex> lk(mOpenSensorMutex);

#ifdef DEBUG_SENSOR_OPEN_CLOSE
  CAM_LOGD("caller =%s", szCallerName);
#endif

  OpenList_t::iterator it = mOpenSensorList.begin();
  for (; it != mOpenSensorList.end(); ++it) {
    if (pHalSensor == it->mpHalSensor) {
#ifdef DEBUG_SENSOR_OPEN_CLOSE
      CAM_LOGD("closeSensor mpHalSensor : %p, pHalSensor = %p, refcnt= %d",
               it->mpHalSensor, pHalSensor, it->miRefCount);
#endif
      //  Last one reference ?
      if (1 == it->miRefCount) {
        CAM_LOGD("<%s> last user", (szCallerName ? szCallerName : "Unknown"));

        //  remove from open list.
        mOpenSensorList.erase(it);
        //  destroy and free this instance.
        pHalSensor->onDestroy();
        delete pHalSensor;
      }
      return;
    }
  }

  CAM_LOGE("<%s> HalSensor:%p not exist",
           (szCallerName ? szCallerName : "Unknown"), pHalSensor);
}

/******************************************************************************
 *
 ******************************************************************************/
HalSensor* HalSensorList::openSensor(vector<MUINT> const& vSensorIndex,
                                     char const* szCallerName) {
  std::unique_lock<std::mutex> lk(mOpenSensorMutex);

#ifdef DEBUG_SENSOR_OPEN_CLOSE
  CAM_LOGD("caller =%s", szCallerName);
#endif

  OpenList_t::iterator it = mOpenSensorList.begin();
  for (; it != mOpenSensorList.end(); ++it) {
    if (it->mpHalSensor->isMatch(vSensorIndex)) {
      //  The open list holds a created instance.
      //  just increment reference count and return the instance.
      it->miRefCount++;
#ifdef DEBUG_SENSOR_OPEN_CLOSE
      CAM_LOGD("openSensor mpHalSensor : %p,idx %d, %d, %d, refcnt %d",
               it->mpHalSensor, vSensorIndex[0], vSensorIndex[1],
               vSensorIndex[2], it->miRefCount);
#endif
      return it->mpHalSensor;
    }
  }
#ifdef DEBUG_SENSOR_OPEN_CLOSE
  CAM_LOGD(
      "new created vSensorIdx[0] = %d, vSensorIdx[1] = %d, vSensorIdx[2] = %d",
      vSensorIndex[0], vSensorIndex[1], vSensorIndex[2]);
#endif

  //  It does not exist in the open list.
  //  We must create a new one and add it to open list.
  HalSensor* pHalSensor = NULL;

  pHalSensor = new HalSensor();

  if (NULL != pHalSensor) {
    //  onCreate callback
    if (!pHalSensor->onCreate(vSensorIndex)) {
      CAM_LOGE("HalSensor::onCreate");
      delete pHalSensor;
      return NULL;
    }

    //  push into open list (with ref. count = 1).
    mOpenSensorList.push_back(OpenInfo(1, pHalSensor));

    CAM_LOGD("<%s> 1st user", (szCallerName ? szCallerName : "Unknown"));
    return pHalSensor;
  }

  MY_LOGF("<%s> Never Be Here...No memory ?",
          (szCallerName ? szCallerName : "Unknown"));
  return NULL;
}

/******************************************************************************
 *
 ******************************************************************************/
IHalSensor* HalSensorList::createSensor(char const* szCallerName,
                                        MUINT const index) {
  std::unique_lock<std::mutex> lk(mEnumSensorMutex);
  vector<MUINT> vSensorIndex;

  vSensorIndex.push_back(index);
  return openSensor(vSensorIndex, szCallerName);
}

/******************************************************************************
 *
 ******************************************************************************/
IHalSensor* HalSensorList::createSensor(char const* szCallerName,
                                        MUINT const uCountOfIndex,
                                        MUINT const* pArrayOfIndex) {
  std::unique_lock<std::mutex> lk(mEnumSensorMutex);
  MY_LOGF_IF(0 == uCountOfIndex || 0 == pArrayOfIndex,
             "<%s> Bad uCountOfIndex:%d pArrayOfIndex:%p", szCallerName,
             uCountOfIndex, pArrayOfIndex);

  vector<MUINT> vSensorIndex(pArrayOfIndex, pArrayOfIndex + uCountOfIndex);
  return openSensor(vSensorIndex, szCallerName);
}

/******************************************************************************
 *
 ******************************************************************************/
HalSensorList::EnumInfo const* HalSensorList::queryEnumInfoByIndex(
    MUINT index) const {
  std::unique_lock<std::mutex> lk(mEnumSensorMutex);

  if (index >= mEnumSensorList.size()) {
    CAM_LOGE("bad sensorIdx:%d >= size:%zu", index, mEnumSensorList.size());
    return NULL;
  }

  return &mEnumSensorList[index];
}

/******************************************************************************
 *
 ******************************************************************************/
HalSensorList::EnumInfo const* HalSensorList::addAndInitSensorEnumInfo_Locked(
    IMGSENSOR_SENSOR_IDX eSensorDev,
    MUINT eSensorType,
    const char* szSensorDrvName) {
  mEnumSensorList.push_back(EnumInfo());
  EnumInfo& rEnumInfo = mEnumSensorList.back();
  std::string drvName;

  rEnumInfo.setDeviceId(eSensorDev);
  rEnumInfo.setSensorType(eSensorType);
  drvName.append("SENSOR_DRVNAME_");
  drvName.append(szSensorDrvName);
  // toUpper
  std::transform(drvName.begin(), drvName.end(), drvName.begin(), ::toupper);
  rEnumInfo.setSensorDrvName(drvName);
  buildStaticInfo(rEnumInfo, &(rEnumInfo.mMetadata));

  return &rEnumInfo;
}

/******************************************************************************
 *
 ******************************************************************************/
IMAGE_SENSOR_TYPE getType(MUINT8 dataFmt) {
  if (dataFmt >= SENSOR_OUTPUT_FORMAT_RAW_B &&
      dataFmt <= SENSOR_OUTPUT_FORMAT_RAW_R) {
    return IMAGE_SENSOR_TYPE_RAW;
  } else if (dataFmt >= SENSOR_OUTPUT_FORMAT_RAW8_B &&
             dataFmt <= SENSOR_OUTPUT_FORMAT_RAW8_R) {
    return IMAGE_SENSOR_TYPE_RAW8;
  } else if (dataFmt >= SENSOR_OUTPUT_FORMAT_UYVY &&
             dataFmt <= SENSOR_OUTPUT_FORMAT_YVYU) {
    return IMAGE_SENSOR_TYPE_YUV;
  } else if (dataFmt >= SENSOR_OUTPUT_FORMAT_CbYCrY &&
             dataFmt <= SENSOR_OUTPUT_FORMAT_YCrYCb) {
    return IMAGE_SENSOR_TYPE_YCBCR;
  } else if (dataFmt >= SENSOR_OUTPUT_FORMAT_RAW_RWB_B &&
             dataFmt <= SENSOR_OUTPUT_FORMAT_RAW_RWB_R) {
    return IMAGE_SENSOR_TYPE_RAW;
  } else if (dataFmt >= SENSOR_OUTPUT_FORMAT_RAW_4CELL_B &&
             dataFmt <= SENSOR_OUTPUT_FORMAT_RAW_4CELL_R) {
    return IMAGE_SENSOR_TYPE_RAW;
  } else if (dataFmt >= SENSOR_OUTPUT_FORMAT_RAW_4CELL_HW_BAYER_B &&
             dataFmt <= SENSOR_OUTPUT_FORMAT_RAW_4CELL_HW_BAYER_R) {
    return IMAGE_SENSOR_TYPE_RAW;

  } else if (dataFmt >= SENSOR_OUTPUT_FORMAT_RAW_4CELL_BAYER_B &&
             dataFmt <= SENSOR_OUTPUT_FORMAT_RAW_4CELL_BAYER_R) {
    return IMAGE_SENSOR_TYPE_RAW;
  } else if (dataFmt == SENSOR_OUTPUT_FORMAT_RAW_MONO) {
    return IMAGE_SENSOR_TYPE_RAW;
  }

  return IMAGE_SENSOR_TYPE_UNKNOWN;
}

struct imgsensor_info_struct* HalSensorList::getSensorInfo(
    IMGSENSOR_SENSOR_IDX idx) {
  MUINT32 sensor_id;
  struct imgsensor_info_struct* info;
  int i, num;

  if (idx >= IMGSENSOR_SENSOR_IDX_MAX_NUM) {
    return NULL;
  }
  sensor_id = sensorId[idx];
  num = getNumOfSupportSensor();
  CAM_LOGI("Support sensor num %d", num);
  for (i = 0; i < num; i++) {
    info = getImgsensorInfo(i);
    if (sensor_id == info->sensor_id) {
      CAM_LOGI("info %d %d 0x%x", idx, i, sensor_id);
      return info;
    }
  }

  return NULL;
}

MUINT32 HalSensorList::getSensorType(IMGSENSOR_SENSOR_IDX idx) {
  MUINT32 sensor_id;
  struct imgsensor_info_struct* info;
  int i, num;

  if (idx >= IMGSENSOR_SENSOR_IDX_MAX_NUM) {
    return IMAGE_SENSOR_TYPE_UNKNOWN;
  }
  sensor_id = sensorId[idx];
  num = getNumOfSupportSensor();
  for (i = 0; i < num; i++) {
    info = getImgsensorInfo(i);
    if (sensor_id == info->sensor_id) {
      CAM_LOGI("type %d %d 0x%x", idx, i, sensor_id);
      return getImgsensorType(i);
    }
  }
  return IMAGE_SENSOR_TYPE_UNKNOWN;
}

const char* HalSensorList::getSensorName(IMGSENSOR_SENSOR_IDX idx) {
  MUINT32 sensor_id;
  struct IMGSENSOR_SENSOR_LIST* psensor_list;
  int i, num;

  if (idx >= IMGSENSOR_SENSOR_IDX_MAX_NUM) {
    return NULL;
  }
  sensor_id = sensorId[idx];
  num = getNumOfSupportSensor();
  for (i = 0; i < num; i++) {
    psensor_list = getSensorList(i);
    if (sensor_id == psensor_list->id) {
      CAM_LOGI("sensorName %d %d %s", idx, i, psensor_list->name);
      return reinterpret_cast<char*>(psensor_list->name);
    }
  }
  return NULL;
}

SENSOR_WINSIZE_INFO_STRUCT* HalSensorList::getWinSizeInfo(
    IMGSENSOR_SENSOR_IDX idx, MUINT32 scenario) const {
  MUINT32 sensor_id;
  struct imgsensor_info_struct* info;
  int i, num;

  if (idx >= IMGSENSOR_SENSOR_IDX_MAX_NUM) {
    return NULL;
  }
  sensor_id = sensorId[idx];
  num = getNumOfSupportSensor();
  for (i = 0; i < num; i++) {
    info = getImgsensorInfo(i);
    if (sensor_id == info->sensor_id) {
      CAM_LOGI("win size %d %d 0x%x", idx, i, sensor_id);
      return getImgWinSizeInfo(i, scenario);
    }
  }
  return NULL;
}

MVOID HalSensorList::querySensorInfo(IMGSENSOR_SENSOR_IDX idx) {
  SensorStaticInfo* pSensorStaticInfo;

  if (idx >= IMGSENSOR_SENSOR_IDX_MAX_NUM) {
    return;
  }
  pSensorStaticInfo = &sensorStaticInfo[idx];

  CUSTOM_CFG* pCustomCfg = NSCamCustomSensor::getCustomConfig(idx);

  struct imgsensor_info_struct* pImgsensorInfo = getSensorInfo(idx);
  if (pImgsensorInfo == NULL) {
    CAM_LOGE("querySensorInfo fail, cannot get sensor info");
    return;
  }

  pSensorStaticInfo->sensorDevID = pImgsensorInfo->sensor_id;
  pSensorStaticInfo->orientationAngle = pCustomCfg->orientation;
  pSensorStaticInfo->facingDirection = pCustomCfg->dir;
  pSensorStaticInfo->horizontalViewAngle = pCustomCfg->horizontalFov;
  pSensorStaticInfo->verticalViewAngle = pCustomCfg->verticalFov;
  pSensorStaticInfo->previewFrameRate = pImgsensorInfo->pre.max_framerate;
  pSensorStaticInfo->captureFrameRate = pImgsensorInfo->cap.max_framerate;
  pSensorStaticInfo->videoFrameRate =
      pImgsensorInfo->normal_video.max_framerate;
  pSensorStaticInfo->video1FrameRate = pImgsensorInfo->hs_video.max_framerate;
  pSensorStaticInfo->video2FrameRate = pImgsensorInfo->slim_video.max_framerate;
  pSensorStaticInfo->custom1FrameRate = pImgsensorInfo->custom1.max_framerate;
  pSensorStaticInfo->custom2FrameRate = pImgsensorInfo->custom2.max_framerate;
  pSensorStaticInfo->custom3FrameRate = pImgsensorInfo->custom3.max_framerate;
  pSensorStaticInfo->custom4FrameRate = pImgsensorInfo->custom4.max_framerate;
  pSensorStaticInfo->custom5FrameRate = pImgsensorInfo->custom5.max_framerate;

  switch (getType(pImgsensorInfo->sensor_output_dataformat)) {
    case IMAGE_SENSOR_TYPE_RAW:
      pSensorStaticInfo->sensorType = SENSOR_TYPE_RAW;
      pSensorStaticInfo->rawSensorBit = RAW_SENSOR_10BIT;
      break;
    case IMAGE_SENSOR_TYPE_RAW8:
      pSensorStaticInfo->sensorType = SENSOR_TYPE_RAW;
      pSensorStaticInfo->rawSensorBit = RAW_SENSOR_8BIT;
      break;

    case IMAGE_SENSOR_TYPE_RAW12:
      pSensorStaticInfo->sensorType = SENSOR_TYPE_RAW;
      pSensorStaticInfo->rawSensorBit = RAW_SENSOR_12BIT;
      break;

    case IMAGE_SENSOR_TYPE_RAW14:
      pSensorStaticInfo->sensorType = SENSOR_TYPE_RAW;
      pSensorStaticInfo->rawSensorBit = RAW_SENSOR_14BIT;
      break;
    case IMAGE_SENSOR_TYPE_YUV:
    case IMAGE_SENSOR_TYPE_YCBCR:
      pSensorStaticInfo->sensorType = SENSOR_TYPE_YUV;
      pSensorStaticInfo->rawSensorBit = RAW_SENSOR_ERROR;
      break;
    case IMAGE_SENSOR_TYPE_RGB565:
      pSensorStaticInfo->sensorType = SENSOR_TYPE_RGB;
      pSensorStaticInfo->rawSensorBit = RAW_SENSOR_ERROR;
      break;
    case IMAGE_SENSOR_TYPE_JPEG:
      pSensorStaticInfo->sensorType = SENSOR_TYPE_JPEG;
      pSensorStaticInfo->rawSensorBit = RAW_SENSOR_ERROR;
      break;
    default:
      pSensorStaticInfo->sensorType = SENSOR_TYPE_UNKNOWN;
      pSensorStaticInfo->rawSensorBit = RAW_SENSOR_ERROR;
      break;
  }

  switch (pImgsensorInfo->sensor_output_dataformat) {
    case SENSOR_OUTPUT_FORMAT_RAW_B:
    case SENSOR_OUTPUT_FORMAT_RAW8_B:
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_B;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_Gb:
    case SENSOR_OUTPUT_FORMAT_RAW8_Gb:
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_Gb;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_Gr:
    case SENSOR_OUTPUT_FORMAT_RAW8_Gr:
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_Gr;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_R:
    case SENSOR_OUTPUT_FORMAT_RAW8_R:
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_R;
      break;
    case SENSOR_OUTPUT_FORMAT_UYVY:
    case SENSOR_OUTPUT_FORMAT_CbYCrY:
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_UYVY;
      break;
    case SENSOR_OUTPUT_FORMAT_VYUY:
    case SENSOR_OUTPUT_FORMAT_CrYCbY:
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_VYUY;
      break;
    case SENSOR_OUTPUT_FORMAT_YUYV:
    case SENSOR_OUTPUT_FORMAT_YCbYCr:
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_YUYV;
      break;
    case SENSOR_OUTPUT_FORMAT_YVYU:
    case SENSOR_OUTPUT_FORMAT_YCrYCb:
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_YVYU;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_RWB_B:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_RWB;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_B;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_RWB_Wb:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_RWB;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_Gb;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_RWB_Wr:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_RWB;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_Gr;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_RWB_R:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_RWB;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_R;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_MONO:
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_B;
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_MONO;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_4CELL_B:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_4CELL;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_B;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_4CELL_Gb:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_4CELL;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_Gb;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_4CELL_Gr:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_4CELL;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_Gr;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_4CELL_R:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_4CELL;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_R;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_4CELL_HW_BAYER_B:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_4CELL_HW_BAYER;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_B;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_4CELL_HW_BAYER_Gb:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_4CELL_HW_BAYER;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_Gb;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_4CELL_HW_BAYER_Gr:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_4CELL_HW_BAYER;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_Gr;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_4CELL_HW_BAYER_R:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_4CELL_HW_BAYER;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_R;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_4CELL_BAYER_B:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_4CELL_BAYER;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_B;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_4CELL_BAYER_Gb:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_4CELL_BAYER;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_Gb;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_4CELL_BAYER_Gr:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_4CELL_BAYER;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_Gr;
      break;
    case SENSOR_OUTPUT_FORMAT_RAW_4CELL_BAYER_R:
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_4CELL_BAYER;
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_RAW_R;
      break;
    default:
      pSensorStaticInfo->sensorFormatOrder = SENSOR_FORMAT_ORDER_NONE;
      pSensorStaticInfo->rawFmtType = SENSOR_RAW_FMT_NONE;
      break;
  }

  pSensorStaticInfo->previewDelayFrame = pImgsensorInfo->pre_delay_frame;
  pSensorStaticInfo->captureDelayFrame = pImgsensorInfo->cap_delay_frame;
  pSensorStaticInfo->videoDelayFrame = pImgsensorInfo->video_delay_frame;
  pSensorStaticInfo->video1DelayFrame = pImgsensorInfo->hs_video_delay_frame;
  pSensorStaticInfo->video2DelayFrame = pImgsensorInfo->slim_video_delay_frame;
  pSensorStaticInfo->Custom1DelayFrame = 0;
  pSensorStaticInfo->Custom2DelayFrame = 0;
  pSensorStaticInfo->Custom3DelayFrame = 0;
  pSensorStaticInfo->Custom4DelayFrame = 0;
  pSensorStaticInfo->Custom5DelayFrame = 0;
  pSensorStaticInfo->aeShutDelayFrame = pImgsensorInfo->ae_shut_delay_frame;
  pSensorStaticInfo->aeSensorGainDelayFrame =
      pImgsensorInfo->ae_sensor_gain_delay_frame;
  pSensorStaticInfo->aeISPGainDelayFrame =
      pImgsensorInfo->ae_ispGain_delay_frame;
  pSensorStaticInfo->FrameTimeDelayFrame = 0;
  pSensorStaticInfo->SensorGrabStartX_PRV = 0;
  pSensorStaticInfo->SensorGrabStartY_PRV = 0;
  pSensorStaticInfo->SensorGrabStartX_CAP = 0;
  pSensorStaticInfo->SensorGrabStartY_CAP = 0;
  pSensorStaticInfo->SensorGrabStartX_VD = 0;
  pSensorStaticInfo->SensorGrabStartY_VD = 0;
  pSensorStaticInfo->SensorGrabStartX_VD1 = 0;
  pSensorStaticInfo->SensorGrabStartY_VD1 = 0;
  pSensorStaticInfo->SensorGrabStartX_VD2 = 0;
  pSensorStaticInfo->SensorGrabStartY_VD2 = 0;
  pSensorStaticInfo->SensorGrabStartX_CST1 = 0;
  pSensorStaticInfo->SensorGrabStartY_CST1 = 0;
  pSensorStaticInfo->SensorGrabStartX_CST2 = 0;
  pSensorStaticInfo->SensorGrabStartY_CST2 = 0;
  pSensorStaticInfo->SensorGrabStartX_CST3 = 0;
  pSensorStaticInfo->SensorGrabStartY_CST3 = 0;
  pSensorStaticInfo->SensorGrabStartX_CST4 = 0;
  pSensorStaticInfo->SensorGrabStartY_CST4 = 0;
  pSensorStaticInfo->SensorGrabStartX_CST5 = 0;
  pSensorStaticInfo->SensorGrabStartY_CST5 = 0;
  pSensorStaticInfo->iHDR_First_IS_LE = pImgsensorInfo->ihdr_le_firstline;
  pSensorStaticInfo->SensorModeNum = pImgsensorInfo->sensor_mode_num;
  pSensorStaticInfo->PerFrameCTL_Support = 0;
  pSensorStaticInfo->sensorModuleID = 0;
  pSensorStaticInfo->previewWidth = pImgsensorInfo->pre.grabwindow_width;
  pSensorStaticInfo->previewHeight = pImgsensorInfo->pre.grabwindow_height;
  pSensorStaticInfo->captureWidth = pImgsensorInfo->cap.grabwindow_width;
  pSensorStaticInfo->captureHeight = pImgsensorInfo->cap.grabwindow_height;
  pSensorStaticInfo->videoWidth = pImgsensorInfo->normal_video.grabwindow_width;
  pSensorStaticInfo->videoHeight =
      pImgsensorInfo->normal_video.grabwindow_height;
  pSensorStaticInfo->video1Width = pImgsensorInfo->hs_video.grabwindow_width;
  pSensorStaticInfo->video1Height = pImgsensorInfo->hs_video.grabwindow_height;
  pSensorStaticInfo->video2Width = pImgsensorInfo->slim_video.grabwindow_width;
  pSensorStaticInfo->video2Height =
      pImgsensorInfo->slim_video.grabwindow_height;
  pSensorStaticInfo->SensorCustom1Width =
      pImgsensorInfo->custom1.grabwindow_width;
  pSensorStaticInfo->SensorCustom1Height =
      pImgsensorInfo->custom1.grabwindow_height;
  pSensorStaticInfo->SensorCustom2Width =
      pImgsensorInfo->custom2.grabwindow_width;
  pSensorStaticInfo->SensorCustom2Height =
      pImgsensorInfo->custom2.grabwindow_height;
  pSensorStaticInfo->SensorCustom3Width =
      pImgsensorInfo->custom3.grabwindow_width;
  pSensorStaticInfo->SensorCustom3Height =
      pImgsensorInfo->custom3.grabwindow_height;
  pSensorStaticInfo->SensorCustom4Width =
      pImgsensorInfo->custom4.grabwindow_width;
  pSensorStaticInfo->SensorCustom4Height =
      pImgsensorInfo->custom4.grabwindow_height;
  pSensorStaticInfo->SensorCustom5Width =
      pImgsensorInfo->custom5.grabwindow_width;
  pSensorStaticInfo->SensorCustom5Height =
      pImgsensorInfo->custom5.grabwindow_height;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID HalSensorList::buildSensorMetadata(IMGSENSOR_SENSOR_IDX idx) {
  MINT64 exposureTime1 = 0x4000;
  MINT64 exposureTime2 = 0x4000;
  MUINT8 u8Para = 0;
  MINT32 s32Para = 0;

  CAM_LOGD("impBuildSensorInfo start!");

  IMetadata metadataA;
  SensorStaticInfo* pSensorStaticInfo = &sensorStaticInfo[idx];

  {
    IMetadata::IEntry entryA(MTK_SENSOR_EXPOSURE_TIME);
    entryA.push_back(exposureTime1, Type2Type<MINT64>());
    entryA.push_back(exposureTime2, Type2Type<MINT64>());
    metadataA.update(MTK_SENSOR_EXPOSURE_TIME, entryA);
  }

  {
    IMetadata::IEntry entryA(MTK_SENSOR_INFO_ACTIVE_ARRAY_REGION);
    MRect region1(MPoint(pSensorStaticInfo->captureHoizontalOutputOffset,
                         pSensorStaticInfo->captureVerticalOutputOffset),
                  MSize(pSensorStaticInfo->captureWidth,
                        pSensorStaticInfo->captureHeight));
    entryA.push_back(region1, Type2Type<MRect>());
    metadataA.update(MTK_SENSOR_INFO_ACTIVE_ARRAY_REGION, entryA);
  }

  {
    IMetadata::IEntry entryA(MTK_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT);
    switch (pSensorStaticInfo->sensorFormatOrder) {
      case SENSOR_FORMAT_ORDER_RAW_B:
        u8Para = 0x3;  // BGGR
        break;
      case SENSOR_FORMAT_ORDER_RAW_Gb:
        u8Para = 0x2;  // GBRG
        break;
      case SENSOR_FORMAT_ORDER_RAW_Gr:
        u8Para = 0x1;  // GRBG
        break;
      case SENSOR_FORMAT_ORDER_RAW_R:
        u8Para = 0x0;  // RGGB
        break;
      default:
        u8Para = 0x4;  // BGR not bayer
        break;
    }
    entryA.push_back(u8Para, Type2Type<MUINT8>());
    metadataA.update(MTK_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT, entryA);
  }

  {
    // need to add query from kernel
    IMetadata::IEntry entryA(MTK_SENSOR_INFO_PIXEL_ARRAY_SIZE);
    MSize Size1(pSensorStaticInfo->captureWidth,
                pSensorStaticInfo->captureHeight);
    entryA.push_back(Size1, Type2Type<MSize>());
    metadataA.update(MTK_SENSOR_INFO_PIXEL_ARRAY_SIZE, entryA);
  }

  {
    // need to add query from kernel
    IMetadata::IEntry entryA(MTK_SENSOR_INFO_WHITE_LEVEL);
    switch (pSensorStaticInfo->rawSensorBit) {
      case RAW_SENSOR_8BIT:
        s32Para = 256;
        break;
      case RAW_SENSOR_10BIT:
        s32Para = 1024;
        break;
      case RAW_SENSOR_12BIT:
        s32Para = 4096;
        break;
      case RAW_SENSOR_14BIT:
        s32Para = 16384;
        break;
      default:
        s32Para = 256;
        break;
    }
    entryA.push_back(s32Para, Type2Type<MINT32>());
    metadataA.update(MTK_SENSOR_INFO_WHITE_LEVEL, entryA);
  }

  {
    IMetadata::IEntry entryA(MTK_SENSOR_INFO_PACKAGE);
    {
      IMetadata metadataB;
      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_SCENARIO_ID);
        entryB.push_back((MINT32)SENSOR_SCENARIO_ID_NORMAL_PREVIEW,
                         Type2Type<MINT32>());
        metadataB.update(MTK_SENSOR_INFO_SCENARIO_ID, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_FRAME_RATE);
        entryB.push_back((MINT32)pSensorStaticInfo->previewFrameRate,
                         Type2Type<MINT32>());
        metadataB.update(MTK_SENSOR_INFO_FRAME_RATE, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_REAL_OUTPUT_SIZE);
        MSize size1(pSensorStaticInfo->previewWidth,
                    pSensorStaticInfo->previewHeight);
        entryB.push_back(size1, Type2Type<MSize>());
        metadataB.update(MTK_SENSOR_INFO_REAL_OUTPUT_SIZE, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_OUTPUT_REGION_ON_ACTIVE_ARRAY);
        MRect region1(MPoint(0, 0), MSize(pSensorStaticInfo->previewWidth,
                                          pSensorStaticInfo->previewHeight));
        entryB.push_back(region1, Type2Type<MRect>());
        metadataB.update(MTK_SENSOR_INFO_OUTPUT_REGION_ON_ACTIVE_ARRAY, entryB);
      }

      entryA.push_back(metadataB, Type2Type<IMetadata>());
    }

    {
      IMetadata metadataB;
      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_SCENARIO_ID);
        entryB.push_back((MINT32)SENSOR_SCENARIO_ID_NORMAL_CAPTURE,
                         Type2Type<MINT32>());
        metadataB.update(MTK_SENSOR_INFO_SCENARIO_ID, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_FRAME_RATE);
        entryB.push_back((MINT32)pSensorStaticInfo->captureFrameRate,
                         Type2Type<MINT32>());
        metadataB.update(MTK_SENSOR_INFO_FRAME_RATE, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_REAL_OUTPUT_SIZE);
        MSize size1(pSensorStaticInfo->captureWidth,
                    pSensorStaticInfo->captureHeight);
        entryB.push_back(size1, Type2Type<MSize>());
        metadataB.update(MTK_SENSOR_INFO_REAL_OUTPUT_SIZE, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_OUTPUT_REGION_ON_ACTIVE_ARRAY);
        MRect region1(MPoint(0, 0), MSize(pSensorStaticInfo->captureWidth,
                                          pSensorStaticInfo->captureHeight));
        entryB.push_back(region1, Type2Type<MRect>());
        metadataB.update(MTK_SENSOR_INFO_OUTPUT_REGION_ON_ACTIVE_ARRAY, entryB);
      }

      entryA.push_back(metadataB, Type2Type<IMetadata>());
    }

    {
      IMetadata metadataB;
      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_SCENARIO_ID);
        entryB.push_back((MINT32)SENSOR_SCENARIO_ID_NORMAL_VIDEO,
                         Type2Type<MINT32>());
        metadataB.update(MTK_SENSOR_INFO_SCENARIO_ID, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_FRAME_RATE);
        entryB.push_back((MINT32)pSensorStaticInfo->videoFrameRate,
                         Type2Type<MINT32>());
        metadataB.update(MTK_SENSOR_INFO_FRAME_RATE, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_REAL_OUTPUT_SIZE);
        MSize size1(pSensorStaticInfo->videoWidth,
                    pSensorStaticInfo->videoHeight);
        entryB.push_back(size1, Type2Type<MSize>());
        metadataB.update(MTK_SENSOR_INFO_REAL_OUTPUT_SIZE, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_OUTPUT_REGION_ON_ACTIVE_ARRAY);
        MRect region1(MPoint(0, 0), MSize(pSensorStaticInfo->videoWidth,
                                          pSensorStaticInfo->videoHeight));
        entryB.push_back(region1, Type2Type<MRect>());
        metadataB.update(MTK_SENSOR_INFO_OUTPUT_REGION_ON_ACTIVE_ARRAY, entryB);
      }

      entryA.push_back(metadataB, Type2Type<IMetadata>());
    }

    {
      IMetadata metadataB;
      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_SCENARIO_ID);
        entryB.push_back((MINT32)SENSOR_SCENARIO_ID_SLIM_VIDEO1,
                         Type2Type<MINT32>());
        metadataB.update(MTK_SENSOR_INFO_SCENARIO_ID, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_FRAME_RATE);
        entryB.push_back((MINT32)pSensorStaticInfo->video1FrameRate,
                         Type2Type<MINT32>());
        metadataB.update(MTK_SENSOR_INFO_FRAME_RATE, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_REAL_OUTPUT_SIZE);
        MSize size1(pSensorStaticInfo->video1Width,
                    pSensorStaticInfo->video1Height);
        entryB.push_back(size1, Type2Type<MSize>());
        metadataB.update(MTK_SENSOR_INFO_REAL_OUTPUT_SIZE, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_OUTPUT_REGION_ON_ACTIVE_ARRAY);
        MRect region1(MPoint(0, 0), MSize(pSensorStaticInfo->video1Width,
                                          pSensorStaticInfo->video1Height));
        entryB.push_back(region1, Type2Type<MRect>());
        metadataB.update(MTK_SENSOR_INFO_OUTPUT_REGION_ON_ACTIVE_ARRAY, entryB);
      }

      entryA.push_back(metadataB, Type2Type<IMetadata>());
    }

    {
      IMetadata metadataB;
      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_SCENARIO_ID);
        entryB.push_back((MINT32)SENSOR_SCENARIO_ID_SLIM_VIDEO2,
                         Type2Type<MINT32>());
        metadataB.update(MTK_SENSOR_INFO_SCENARIO_ID, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_FRAME_RATE);
        entryB.push_back((MINT32)pSensorStaticInfo->video2FrameRate,
                         Type2Type<MINT32>());
        metadataB.update(MTK_SENSOR_INFO_FRAME_RATE, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_REAL_OUTPUT_SIZE);
        MSize size1(pSensorStaticInfo->video2Width,
                    pSensorStaticInfo->video2Height);
        entryB.push_back(size1, Type2Type<MSize>());
        metadataB.update(MTK_SENSOR_INFO_REAL_OUTPUT_SIZE, entryB);
      }

      {
        IMetadata::IEntry entryB(MTK_SENSOR_INFO_OUTPUT_REGION_ON_ACTIVE_ARRAY);
        MRect region1(MPoint(0, 0), MSize(pSensorStaticInfo->video2Width,
                                          pSensorStaticInfo->video2Height));
        entryB.push_back(region1, Type2Type<MRect>());
        metadataB.update(MTK_SENSOR_INFO_OUTPUT_REGION_ON_ACTIVE_ARRAY, entryB);
      }

      entryA.push_back(metadataB, Type2Type<IMetadata>());
    }
    metadataA.update(MTK_SENSOR_INFO_PACKAGE, entryA);
  }
  metadataA.sort();

  CAM_LOGD("impBuildSensorInfo end!");
}

/******************************************************************************
 *
 ******************************************************************************/
static MBOOL impConstructStaticMetadata_by_SymbolName(
    std::string const& s8Symbol, Info const& rInfo, IMetadata* rMetadata) {
  typedef MBOOL (*PFN_T)(IMetadata * metadata, Info const& info);

  PFN_T pfn;
  MBOOL ret = MTRUE;
  string const s8LibPath("libmtk_halsensor.so");
  void* handle = ::dlopen(s8LibPath.c_str(), RTLD_NOW);

  if (!handle) {
    char const* err_str = ::dlerror();
    CAM_LOGW("dlopen library=%s %s", s8LibPath.c_str(),
             err_str ? err_str : "unknown");
    ret = MFALSE;
    goto lbExit;
  }
  pfn = (PFN_T)::dlsym(handle, s8Symbol.c_str());
  if (!pfn) {
    CAM_LOGD("%s not found", s8Symbol.c_str());
    ret = MFALSE;
    goto lbExit;
  }

  ret = pfn(rMetadata, rInfo);
  CAM_LOGD_IF(!ret, "%s fail", s8Symbol.c_str());

lbExit:
  if (handle) {
    ::dlclose(handle);
    handle = NULL;
  }
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
static MBOOL impBuildStaticInfo(Info const& rInfo, IMetadata* rMetadata) {
  static char const* const kStaticMetadataTypeNames[] = {
      "LENS",    "SENSOR", "TUNING_3A", "FLASHLIGHT", "SCALER",
      "FEATURE", "CAMERA", "REQUEST",   NULL};

  auto constructMetadata = [](Info const& rInfo, IMetadata* rMetadata,
                              std::string attr) {
    std::string s8Symbol_Sensor;
    std::string s8Symbol_Common;
    char strTemp[100];
    for (int i = 0; NULL != kStaticMetadataTypeNames[i]; i++) {
      char const* const pTypeName = kStaticMetadataTypeNames[i];

      MBOOL status = MTRUE;

      snprintf(strTemp, sizeof(strTemp), "%s_%s_%s_%s",
               PREFIX_FUNCTION_STATIC_METADATA, attr.c_str(), pTypeName,
               rInfo.getSensorDrvName().c_str());
      s8Symbol_Sensor = strTemp;
      status = impConstructStaticMetadata_by_SymbolName(s8Symbol_Sensor, rInfo,
                                                        rMetadata);
      if (MTRUE == status) {
        continue;
      }

      snprintf(strTemp, sizeof(strTemp), "%s_%s_%s_%s",
               PREFIX_FUNCTION_STATIC_METADATA, attr.c_str(), pTypeName,
               "COMMON");
      s8Symbol_Common = strTemp;
      status = impConstructStaticMetadata_by_SymbolName(s8Symbol_Common, rInfo,
                                                        rMetadata);
      if (MTRUE == status) {
        continue;
      }
      CAM_LOGE_IF(0, "Fail for both %s & %s", s8Symbol_Sensor.c_str(),
                  s8Symbol_Common.c_str());
    }
  };

  constructMetadata(rInfo, rMetadata, "DEVICE");
  constructMetadata(rInfo, rMetadata, "PROJECT");

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HalSensorList::buildStaticInfo(Info const& rInfo, IMetadata* pMetadata) const {
  IMetadata& rMetadata = *pMetadata;
  const SensorStaticInfo* pSensorStaticInfo =
      &sensorStaticInfo[rInfo.getDeviceId()];

  MUINT8 u8Para = 0;
  if (!impBuildStaticInfo(rInfo, &rMetadata)) {
    CAM_LOGE("Fail to build static info for %s index:%d",
             rInfo.getSensorDrvName().c_str(), rInfo.getDeviceId());
  }

  // METEDATA Ref  //system/media/camera/docs/docs.html
  // using full size
  {
    IMetadata::IEntry entryA(MTK_SENSOR_INFO_ACTIVE_ARRAY_REGION);
    MRect region1(MPoint(pSensorStaticInfo->SensorGrabStartX_CAP,
                         pSensorStaticInfo->SensorGrabStartY_CAP),
                  MSize(pSensorStaticInfo->captureWidth,
                        pSensorStaticInfo->captureHeight));
    entryA.push_back(region1, Type2Type<MRect>());
    rMetadata.update(MTK_SENSOR_INFO_ACTIVE_ARRAY_REGION, entryA);

    CAM_LOGD("MTK_SENSOR_INFO_ACTIVE_ARRAY_REGION(%d, %d, %d, %d)",
             pSensorStaticInfo->SensorGrabStartX_CAP,
             pSensorStaticInfo->SensorGrabStartY_CAP,
             pSensorStaticInfo->captureWidth, pSensorStaticInfo->captureHeight);
  }
  // using full size(No correction)
  {
    IMetadata::IEntry entryA(MTK_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE);
    entryA.push_back(pSensorStaticInfo->SensorGrabStartX_CAP,
                     Type2Type<MINT32>());
    entryA.push_back(pSensorStaticInfo->SensorGrabStartY_CAP,
                     Type2Type<MINT32>());
    entryA.push_back(pSensorStaticInfo->captureWidth, Type2Type<MINT32>());
    entryA.push_back(pSensorStaticInfo->captureHeight, Type2Type<MINT32>());
    rMetadata.update(MTK_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE, entryA);

    CAM_LOGD("MTK_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE(%d, %d, %d, %d)",
             pSensorStaticInfo->SensorGrabStartX_CAP,
             pSensorStaticInfo->SensorGrabStartY_CAP,
             pSensorStaticInfo->captureWidth, pSensorStaticInfo->captureHeight);
  }
  // Pixel arry
  {
    SensorCropWinInfo rSensorCropInfo = {};
    SENSOR_WINSIZE_INFO_STRUCT* ptr;
    MUINT32 scenario = MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG; /*capture mode*/

    memset(&rSensorCropInfo, 0, sizeof(SensorCropWinInfo));
    ptr = getWinSizeInfo((IMGSENSOR_SENSOR_IDX)rInfo.getDeviceId(),
                         scenario); /*DeviceId count from 1*/
    if (ptr) {
      memcpy(&rSensorCropInfo, reinterpret_cast<void*>(ptr),
             sizeof(SENSOR_WINSIZE_INFO_STRUCT));
    }
    CAM_LOGD("Pixel arry: device id %d full_w %d full_h %d",
             rInfo.getDeviceId(), rSensorCropInfo.full_w,
             rSensorCropInfo.full_h);

    IMetadata::IEntry entryA(MTK_SENSOR_INFO_PIXEL_ARRAY_SIZE);
    MSize Size1(rSensorCropInfo.full_w, rSensorCropInfo.full_h);
    entryA.push_back(Size1, Type2Type<MSize>());
    rMetadata.update(MTK_SENSOR_INFO_PIXEL_ARRAY_SIZE, entryA);
  }
  // Color filter
  {
    IMetadata::IEntry entryA(MTK_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT);
    switch (pSensorStaticInfo->sensorFormatOrder) {
      case SENSOR_FORMAT_ORDER_RAW_B:
        u8Para = 0x3;  // BGGR
        break;
      case SENSOR_FORMAT_ORDER_RAW_Gb:
        u8Para = 0x2;  // GBRG
        break;
      case SENSOR_FORMAT_ORDER_RAW_Gr:
        u8Para = 0x1;  // GRBG
        break;
      case SENSOR_FORMAT_ORDER_RAW_R:
        u8Para = 0x0;  // RGGB
        break;
      default:
        u8Para = 0x4;  // BGR not bayer
        break;
    }
    entryA.push_back(u8Para, Type2Type<MUINT8>());
    rMetadata.update(MTK_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT, entryA);
  }

  {
    NSCam::TuningUtils::TuningPlatformInfo tuning_info;
    NSCam::TuningUtils::PlatformInfo sensor_info;
    tuning_info.getTuningInfo(&sensor_info);

    if (rInfo.getDeviceId() == 0) {
      rMetadata.remove(MTK_SENSOR_INFO_ORIENTATION);
      IMetadata::IEntry entryA(MTK_SENSOR_INFO_ORIENTATION);
      entryA.push_back(sensor_info.wf_sensor.orientation, Type2Type<MINT32>());
      rMetadata.update(MTK_SENSOR_INFO_ORIENTATION, entryA);

      rMetadata.remove(MTK_SENSOR_INFO_FACING);
      IMetadata::IEntry entryC(MTK_SENSOR_INFO_FACING);
      entryC.push_back(MTK_LENS_FACING_BACK, Type2Type<MUINT8>());
      rMetadata.update(MTK_SENSOR_INFO_FACING, entryC);
    } else if (rInfo.getDeviceId() == 1) {
      rMetadata.remove(MTK_SENSOR_INFO_ORIENTATION);
      IMetadata::IEntry entryA(MTK_SENSOR_INFO_ORIENTATION);
      entryA.push_back(sensor_info.uf_sensor.orientation, Type2Type<MINT32>());
      rMetadata.update(MTK_SENSOR_INFO_ORIENTATION, entryA);

      rMetadata.remove(MTK_SENSOR_INFO_FACING);
      IMetadata::IEntry entryC(MTK_SENSOR_INFO_FACING);
      entryC.push_back(MTK_LENS_FACING_FRONT, Type2Type<MUINT8>());
      rMetadata.update(MTK_SENSOR_INFO_FACING, entryC);
    }
    // AF
    if (rInfo.getDeviceId() == 0) {
      MY_LOGD("main_sensor.minFocusDistance: %f, update AF modes & regions",
              sensor_info.wf_sensor.minFocusDistance);
      if (sensor_info.wf_sensor.minFocusDistance == 0) {  // fixed focus
        // MTK_LENS_INFO_MINIMUM_FOCUS_DISTANCE
        rMetadata.remove(MTK_LENS_INFO_MINIMUM_FOCUS_DISTANCE);
        IMetadata::IEntry eMinFocusDistance(
            MTK_LENS_INFO_MINIMUM_FOCUS_DISTANCE);
        eMinFocusDistance.push_back(0, Type2Type<MFLOAT>());
        rMetadata.update(MTK_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
                         eMinFocusDistance);

        // MTK_CONTROL_AF_AVAILABLE_MODES
        rMetadata.remove(MTK_CONTROL_AF_AVAILABLE_MODES);
        IMetadata::IEntry afAvailableModes(MTK_CONTROL_AF_AVAILABLE_MODES);
        afAvailableModes.push_back(MTK_CONTROL_AF_MODE_OFF,
                                   Type2Type<MUINT8>());
        rMetadata.update(MTK_CONTROL_AF_AVAILABLE_MODES, afAvailableModes);

        // MTK_CONTROL_MAX_REGIONS
        rMetadata.remove(MTK_CONTROL_MAX_REGIONS);
        IMetadata::IEntry maxRegions(MTK_CONTROL_MAX_REGIONS);
        maxRegions.push_back(1, Type2Type<MINT32>());
        maxRegions.push_back(1, Type2Type<MINT32>());
        maxRegions.push_back(0, Type2Type<MINT32>());
        rMetadata.update(MTK_CONTROL_MAX_REGIONS, maxRegions);
      }
    }
  }

  rMetadata.sort();

  return MTRUE;
}
