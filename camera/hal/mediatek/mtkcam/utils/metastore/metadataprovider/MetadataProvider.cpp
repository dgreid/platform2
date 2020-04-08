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

#define LOG_TAG "MtkCam/MetadataProvider"
//

#include "MyUtils.h"
#include <hardware/camera3.h>
#include <memory>
#include <mtkcam/drv/IHalSensor.h>
#include <mtkcam/utils/LogicalCam/IHalLogicalDeviceList.h>
#include <mtkcam/utils/metadata/IMetadataConverter.h>
#include <mtkcam/utils/metadata/IMetadataTagSet.h>
#include <mtkcam/utils/metadata/client/TagMap.h>
#include <mtkcam/utils/metadata/mtk_metadata_types.h>
#include <mtkcam/utils/metastore/metadataprovider/MetadataProvider.h>
#include <system/camera_metadata.h>

using NSCam::IHalLogicalDeviceList;
using NSCam::IMetadataTagSet;
using NSCam::Type2Type;
using NSCam::Type2TypeEnum;
using NSMetadataProvider::MetadataProvider;

/******************************************************************************
 *
 ******************************************************************************/
static bool setTagInfo(IMetadataTagSet* rtagInfo);

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<IMetadataProvider> IMetadataProvider::create(
    int32_t const i4OpenId) {
  std::shared_ptr<MetadataProvider> p =
      std::make_shared<MetadataProvider>(i4OpenId);
  if (!p) {
    MY_LOGE("No Memory");
    return nullptr;
  }
  //
  if (OK != p->onCreate()) {
    MY_LOGE("onCreate");
    return nullptr;
  }
  //
  return std::dynamic_pointer_cast<IMetadataProvider>(p);
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<IMetadataProvider> IMetadataProvider::create(
    int32_t const i4OpenId,
    const IMetadata& aMeta_mtk,
    camera_metadata* aMeta) {
  std::shared_ptr<MetadataProvider> p =
      std::make_shared<MetadataProvider>(i4OpenId, aMeta_mtk, aMeta);
  if (!p) {
    MY_LOGE("No Memory");
    return nullptr;
  }
  //
  return std::dynamic_pointer_cast<IMetadataProvider>(p);
}

/******************************************************************************
 *
 ******************************************************************************/
MetadataProvider::~MetadataProvider() {
  MY_LOGD("+ OpenId:%d", mInfo.getDeviceId());
  //
  if (mpStaticCharacteristics) {
    ::free_camera_metadata(mpStaticCharacteristics);
    mpStaticCharacteristics = NULL;
  }
  //
  pthread_rwlock_destroy(&mRWLock);
  MY_LOGD("- OpenId:%d", mInfo.getDeviceId());
}

/******************************************************************************
 *
 ******************************************************************************/
MetadataProvider::MetadataProvider(int32_t const i4OpenId)
    : mpStaticCharacteristics(NULL), mpHALMetadata() {
  IHalLogicalDeviceList* pHalDeviceList;
  pHalDeviceList = MAKE_HalLogicalDeviceList();
  int32_t sensorType = pHalDeviceList->queryType(i4OpenId);
  int32_t sensorDev = pHalDeviceList->querySensorDevIdx(i4OpenId);
  const char* sensorDrvName = pHalDeviceList->queryDriverName(i4OpenId);
  MY_LOGD("sensorDrvName : %s, %p", sensorDrvName, sensorDrvName);
  //
  mInfo = Info(i4OpenId, sensorDev, sensorType, sensorDrvName);
  //
  showCustInfo();
  pthread_rwlock_init(&mRWLock, NULL);
}

/******************************************************************************
 *
 ******************************************************************************/
MetadataProvider::MetadataProvider(int32_t const i4OpenId,
                                   const IMetadata& aMeta_mtk,
                                   camera_metadata* aMeta)
    : mpStaticCharacteristics(aMeta), mpHALMetadata(aMeta_mtk) {
  IHalLogicalDeviceList* pHalDeviceList;
  pHalDeviceList = MAKE_HalLogicalDeviceList();
  int32_t sensorType = pHalDeviceList->queryType(i4OpenId);
  int32_t sensorDev = pHalDeviceList->querySensorDevIdx(i4OpenId);
  const char* sensorDrvName = pHalDeviceList->queryDriverName(i4OpenId);
  //
  mInfo = Info(i4OpenId, sensorDev, sensorType, sensorDrvName);
  //
  showCustInfo();
  pthread_rwlock_init(&mRWLock, NULL);
}

/******************************************************************************
 *
 ******************************************************************************/
uint32_t MetadataProvider::getDeviceVersion() const {
  pthread_rwlock_rdlock(&mRWLock);
  uint32_t ret = 0;
#if 1
  ret = CAMERA_DEVICE_API_VERSION_3_3;
#else
  ret = mpHALMetadata.entryFor(MTK_HAL_VERSION).itemAt(0, Type2Type<MINT32>());
#endif
  pthread_rwlock_unlock(&mRWLock);
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
int MetadataProvider::getDeviceFacing() const {
  pthread_rwlock_rdlock(&mRWLock);
  int ret = mpHALMetadata.entryFor(MTK_SENSOR_INFO_FACING)
                .itemAt(0, Type2Type<MUINT8>());
  pthread_rwlock_unlock(&mRWLock);
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
int MetadataProvider::getDeviceWantedOrientation() const {
  pthread_rwlock_rdlock(&mRWLock);
  if (mpHALMetadata.entryFor(MTK_SENSOR_INFO_ORIENTATION).isEmpty()) {
    return mInfo.getDeviceId() == 0 ? 90 : 270;
  }

  int ret = mpHALMetadata.entryFor(MTK_SENSOR_INFO_ORIENTATION)
                .itemAt(0, Type2Type<MINT32>());
  pthread_rwlock_unlock(&mRWLock);
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
int MetadataProvider::getDeviceSetupOrientation() const {
  pthread_rwlock_rdlock(&mRWLock);
  int ret = mpHALMetadata.entryFor(MTK_SENSOR_INFO_ORIENTATION)
                .itemAt(0, Type2Type<MINT32>());
  pthread_rwlock_unlock(&mRWLock);
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
int MetadataProvider::getDeviceHasFlashLight() const {
  pthread_rwlock_rdlock(&mRWLock);
  int ret = 0;
  if (!mpHALMetadata.entryFor(MTK_FLASH_INFO_AVAILABLE).isEmpty()) {
    ret = mpHALMetadata.entryFor(MTK_FLASH_INFO_AVAILABLE)
              .itemAt(0, Type2Type<MUINT8>());
  }
  pthread_rwlock_unlock(&mRWLock);
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
int MetadataProvider::setStaticData(IMetadata* pMeta) {
  pthread_rwlock_wrlock(&mRWLock);
  mpUpdatedHALMetadata = mpHALMetadata;
  for (MUINT i = 0; i < pMeta->count(); i++) {
    mpHALMetadata.remove(pMeta->entryAt(i).tag());
    mpHALMetadata.update(pMeta->entryAt(i).tag(), pMeta->entryAt(i));
  }
  pthread_rwlock_unlock(&mRWLock);
  return 0;
}

/******************************************************************************
 *
 ******************************************************************************/
int MetadataProvider::restoreStaticData() {
  pthread_rwlock_wrlock(&mRWLock);
  if (!mpUpdatedHALMetadata.isEmpty()) {
    mpHALMetadata.clear();
    mpHALMetadata = mpUpdatedHALMetadata;
    mpUpdatedHALMetadata.clear();
  }
  pthread_rwlock_unlock(&mRWLock);
  return 0;
}

/******************************************************************************
 *
 ******************************************************************************/
status_t MetadataProvider::onCreate() {
  status_t status = OK;

  // prepare data for TagInfo
  IMetadataTagSet mtagInfo;
  setTagInfo(&mtagInfo);

  // create IMetadataConverter
  std::shared_ptr<IMetadataConverter> pMetadataConverter =
      IMetadataConverter::createInstance(mtagInfo);

  {
    pthread_rwlock_wrlock(&mRWLock);
    status = constructStaticMetadata(pMetadataConverter,
                                     &mpStaticCharacteristics, &mpHALMetadata);
    if (OK != status) {
      MY_LOGE("constructStaticMetadata - status[%s(%d)]", ::strerror(-status),
              -status);
      pthread_rwlock_unlock(&mRWLock);
      return status;
    }
    pthread_rwlock_unlock(&mRWLock);
  }

  return status;
}

static bool setTagInfo(IMetadataTagSet* rtagInfo) {
#define _IMP_SECTION_INFO_(...)
#undef _IMP_TAG_INFO_
#define _IMP_TAG_INFO_(_tag_, _type_, _name_) \
  rtagInfo->addTag(_tag_, _name_, Type2TypeEnum<_type_>::typeEnum);

#include <custom_metadata/custom_metadata_tag_info.inl>
#undef _IMP_TAG_INFO_

#undef _IMP_TAGCONVERT_
#define _IMP_TAGCONVERT_(_android_tag_, _mtk_tag_) \
  rtagInfo->addTagMap(_android_tag_, _mtk_tag_);
#if (PLATFORM_SDK_VERSION >= 21)
  ADD_ALL_MEMBERS;
#endif

#undef _IMP_TAGCONVERT_

  return MTRUE;
}
