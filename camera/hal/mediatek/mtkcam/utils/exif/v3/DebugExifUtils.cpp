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

#define LOG_TAG "MtkCam/DebugExifUtils"

#include <map>
#include <vector>
//
#include <mtkcam/custom/ExifFactory.h>
//
#include <mtkcam/utils/std/Log.h>
#include <mtkcam/utils/metadata/IMetadata.h>
#include <mtkcam/utils/exif/DebugExifUtils.h>
//
using NSCam::DebugExifUtils;
using NSCam::IMetadata;
using NSCam::MPoint;
using NSCam::MPointF;
using NSCam::MRect;
using NSCam::MRectF;
using NSCam::MSize;
using NSCam::MSizeF;
using NSCam::Type2Type;

/******************************************************************************
 *
 ******************************************************************************/
static auto getDebugExif() {
  static auto const inst = MAKE_DebugExif();
  return inst;
}

static auto getBufInfo_cam() {
  static auto const inst =
      ((NULL != getDebugExif())
           ? getDebugExif()->getBufInfo(DEBUG_EXIF_KEYID_CAM)
           : NULL);
  return inst;
}

// ---------------------------------------------------------------------------

template <typename T>
static inline MVOID updateEntry(IMetadata* metadata,
                                const MUINT32 tag,
                                const T& val) {
  if (metadata == NULL) {
    CAM_LOGW("pMetadata is NULL");
    return;
  }

  IMetadata::IEntry entry(tag);
  entry.push_back(val, Type2Type<T>());
  metadata->update(tag, entry);
}

template <class T>
static MBOOL tryGetMetaData(IMetadata* pMetadata, MUINT32 const tag, T* rVal) {
  if (pMetadata == NULL) {
    return MFALSE;
  }

  IMetadata::IEntry entry = pMetadata->entryFor(tag);
  if (!entry.isEmpty()) {
    *rVal = entry.itemAt(0, Type2Type<T>());
    return MTRUE;
  }

  return MFALSE;
}

static bool setDebugExifMF(const MUINT32 tagKey,
                           const MUINT32 tagData,
                           const std::map<MUINT32, MUINT32>& debugInfoList,
                           IMetadata* exifMetadata) {
  auto it = getBufInfo_cam()->body_layout.find(DEBUG_EXIF_MID_CAM_MF);
  if (it == getBufInfo_cam()->body_layout.end()) {
    CAM_LOGE("cannot find the layout: DEBUG_EXIF_MID_CAM_MF");
    return false;
  }

  auto const& info = it->second;

  // allocate memory of debug information
  IMetadata::Memory debugInfoSet;
  debugInfoSet.resize(info.size);

  // add debug information
  {
    auto const tagId_MF_TAG_VERSION = getDebugExif()->getTagId_MF_TAG_VERSION();
    auto pTag = reinterpret_cast<debug_exif_field*>(debugInfoSet.editArray());
    pTag[tagId_MF_TAG_VERSION].u4FieldID = (0x1000000 | tagId_MF_TAG_VERSION);
    pTag[tagId_MF_TAG_VERSION].u4FieldValue = info.version;

    for (const auto& item : debugInfoList) {
      const MUINT32 index = item.first;
      pTag[index].u4FieldID = (0x1000000 | index);
      pTag[index].u4FieldValue = item.second;
    }
  }

  // update debug exif metadata
  updateEntry<MINT32>(exifMetadata, tagKey, DEBUG_EXIF_MID_CAM_MF);
  updateEntry<IMetadata::Memory>(exifMetadata, tagData, debugInfoSet);

  return true;
}
static bool setDebugExifRESERVE3(const MUINT32 tagKey,
                                 const MUINT32 tagData,
                                 const MUINT32 size,
                                 const void* debugInfoList,
                                 IMetadata* exifMetadata) {
  auto it =
      getBufInfo_cam()->body_layout.find(DEBUG_EXIF_MID_CAM_RESERVE3);  // MDP
  if (it == getBufInfo_cam()->body_layout.end()) {
    CAM_LOGE("cannot find the layout: DEBUG_EXIF_MID_CAM_RESERVE3");
    return false;
  }

  auto const& info = it->second;

  // allocate memory of debug information
  IMetadata::Memory debugInfoValue;
  debugInfoValue.resize(info.size);

  // add debug information
  {
    auto pTag = reinterpret_cast<MUINT32*>(debugInfoValue.editArray());
    ::memcpy(pTag, debugInfoList, size);
  }

  // update debug exif metadata
  updateEntry<MINT32>(exifMetadata, tagKey, DEBUG_EXIF_MID_CAM_RESERVE3);
  updateEntry<IMetadata::Memory>(exifMetadata, tagData, debugInfoValue);

  return true;
}

static bool setDebugExifCAM(const MUINT32 tagKey,
                            const MUINT32 tagData,
                            const std::map<MUINT32, MUINT32>& debugInfoList,
                            IMetadata* exifMetadata) {
  auto it = getBufInfo_cam()->body_layout.find(DEBUG_EXIF_MID_CAM_CMN);
  if (it == getBufInfo_cam()->body_layout.end()) {
    CAM_LOGE("cannot find the layout: DEBUG_EXIF_MID_CAM_CMN");
    return false;
  }

  IMetadata::Memory debugInfoSet;
  if (!tryGetMetaData(exifMetadata, tagData, &debugInfoSet)) {
    // CAM_LOGD("[debug mode] debugInfoList size: %d", debugInfoList.size());
    // allocate memory of debug information
    auto const& info = it->second;
    debugInfoSet.resize(info.size);
  }
  // allocate memory of debug information
  auto pTag = reinterpret_cast<debug_exif_field*>(debugInfoSet.editArray());
  for (const auto& item : debugInfoList) {
    const MUINT32 index = item.first;
    // CAM_LOGD("[debug mode] item: %d value: %d", item.first, item.second);
    pTag[index].u4FieldID = (0x1000000 | index);
    pTag[index].u4FieldValue = item.second;
  }
  // update debug exif metadata
  updateEntry<MINT32>(exifMetadata, tagKey, DEBUG_EXIF_MID_CAM_CMN);
  updateEntry<IMetadata::Memory>(exifMetadata, tagData, debugInfoSet);

  return true;
}

// ---------------------------------------------------------------------------

IMetadata* DebugExifUtils::setDebugExif(
    const DebugExifType type,
    const MUINT32 tagKey,
    const MUINT32 tagData,
    const std::map<MUINT32, MUINT32>& debugInfoList,
    IMetadata* exifMetadata) {
  if (exifMetadata == NULL) {
    CAM_LOGW("invalid metadata(%p)", exifMetadata);
    return nullptr;
  }

  if (!getDebugExif()) {
    CAM_LOGE("bad getDebugExif()");
    return nullptr;
  }

  if (!getBufInfo_cam()) {
    CAM_LOGE("bad getBufInfo_cam()");
    return nullptr;
  }

  bool ret = [=, &type, &debugInfoList](IMetadata* metadata) -> bool {
    switch (type) {
      case DebugExifType::DEBUG_EXIF_MF:
        return setDebugExifMF(tagKey, tagData, debugInfoList, metadata);
      case DebugExifType::DEBUG_EXIF_CAM:
        return setDebugExifCAM(tagKey, tagData, debugInfoList, metadata);
      default:
        CAM_LOGW("invalid debug exif type, do nothing");
        return false;
    }
  }(exifMetadata);

  return ret ? exifMetadata : nullptr;
}
// ---------------------------------------------------------------------------

IMetadata* DebugExifUtils::setDebugExif(const DebugExifType type,
                                        const MUINT32 tagKey,
                                        const MUINT32 tagData,
                                        const MUINT32 size,
                                        const void* debugInfoList,
                                        IMetadata* exifMetadata) {
  if (exifMetadata == NULL) {
    CAM_LOGW("invalid metadata(%p)", exifMetadata);
    return nullptr;
  }

  if (!getDebugExif()) {
    CAM_LOGE("bad getDebugExif()");
    return nullptr;
  }

  if (!getBufInfo_cam()) {
    CAM_LOGE("bad getBufInfo_cam()");
    return nullptr;
  }

  bool ret = [=, &type, &debugInfoList](IMetadata* metadata) -> bool {
    switch (type) {
      case DebugExifType::DEBUG_EXIF_RESERVE3:
        return setDebugExifRESERVE3(tagKey, tagData, size, debugInfoList,
                                    metadata);
      default:
        CAM_LOGW("invalid debug exif type, do nothing");
        return false;
    }
  }(exifMetadata);

  return ret ? exifMetadata : nullptr;
}
