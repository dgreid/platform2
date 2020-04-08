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

#define LOG_TAG "MtkCam/MetadataProvider.constructStatic"
//

#include <algorithm>
#include <camera_custom_logicaldevice.h>
#include <custom_metadata/custom_metadata_tag.h>
#include <dlfcn.h>
//
#include "MyUtils.h"
#include <memory>
#include <mtkcam/aaa/IDngInfo.h>
#include <mtkcam/drv/IHalSensor.h>
#include <mtkcam/utils/LogicalCam/IHalLogicalDeviceList.h>
#include <mtkcam/utils/metadata/client/mtk_metadata_tag.h>
#include <mtkcam/utils/metadata/IMetadataConverter.h>
#include <mtkcam/utils/TuningUtils/TuningPlatformInfo.h>
//
#include <property_lib.h>
#include <string>
#include <vector>

#include "mojo/cros_camera_enum.mojom.h"

using NSCam::Type2Type;
using NSMetadataProvider::MetadataProvider;

/******************************************************************************
 *
 ******************************************************************************/

status_t MetadataProvider::impConstructStaticMetadata_by_SymbolName(
    std::string const& s8Symbol, IMetadata* metadata) {
  typedef status_t (*PFN_T)(IMetadata * metadata, Info const& info);
  //
  PFN_T pfn = (PFN_T)::dlsym(RTLD_DEFAULT, s8Symbol.c_str());
  if (!pfn) {
    MY_LOGD("%s not found", s8Symbol.c_str());
    return NAME_NOT_FOUND;
  }
  //
  status_t const status = pfn(metadata, mInfo);
  MY_LOGD_IF(status, "%s: returns status[%s(%d)]", s8Symbol.c_str(),
             ::dlerror(), -status);
  //
  return status;
}

/******************************************************************************
 *
 ******************************************************************************/
status_t MetadataProvider::impConstructStaticMetadata(IMetadata* metadata) {
  size_t count = (sizeof(kStaticMetadataTypeNames) / sizeof(char const*));
  std::vector<bool> vMap(count, false);
  //
  for (int i = 0; NULL != kStaticMetadataTypeNames[i]; i++) {
    char const* const pTypeName = kStaticMetadataTypeNames[i];
    status_t status = OK;
    //
    std::string const s8Symbol_Sensor =
        base::StringPrintf("%s_DEVICE_%s_%s", PREFIX_FUNCTION_STATIC_METADATA,
                           pTypeName, mInfo.getSensorDrvName());
    status =
        impConstructStaticMetadata_by_SymbolName(s8Symbol_Sensor, metadata);
    if (OK == status) {
      vMap[i] = true;
      continue;
    }
    //
    std::string const s8Symbol_Common =
        base::StringPrintf("%s_DEVICE_%s_%s", PREFIX_FUNCTION_STATIC_METADATA,
                           pTypeName, "COMMON");
    status =
        impConstructStaticMetadata_by_SymbolName(s8Symbol_Common, metadata);
    if (OK == status) {
      vMap[i] = true;
      continue;
    }
    //
    MY_LOGE_IF(status, "Fail for both %s & %s", s8Symbol_Sensor.c_str(),
               s8Symbol_Common.c_str());
  }
  //
  for (int i = 0; NULL != kStaticMetadataTypeNames[i]; i++) {
    if (vMap[i] == false) {
      MY_LOGE("Fail to load %s in all PLATFORM/PROJECT combinations",
              kStaticMetadataTypeNames[i]);
      return NAME_NOT_FOUND;
    }
  }
  //
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
status_t MetadataProvider::constructStaticMetadata(
    std::shared_ptr<IMetadataConverter> pConverter,
    camera_metadata** rpDstMetadata,
    IMetadata* mtkMetadata) {
  MY_LOGD("construct static metadata");

  status_t status = OK;

  // -----(1)-----//
  // get static informtation from customization (with camera_metadata format)
  // calculate its entry count and data count
  if (OK != (status = impConstructStaticMetadata(mtkMetadata))) {
    MY_LOGE("Unable evaluate the size for camera static info - status[%s(%d)]",
            ::strerror(-status), -status);
    return status;
  }
  MY_LOGD("Allocating %d entries from customization", mtkMetadata->count());

  // -----(2.1)------//
  // get static informtation from sensor hal moduls (with IMetadata format)
  IMetadata sensorMetadata =
      MAKE_HalLogicalDeviceList()->queryStaticInfo(mInfo.getDeviceId());
  MY_LOGD("Allocating %d entries from sensor HAL", sensorMetadata.count());

  //
  for (size_t i = 0; i < sensorMetadata.count(); i++) {
    IMetadata::Tag_t mTag = sensorMetadata.entryAt(i).tag();
    mtkMetadata->update(mTag, sensorMetadata.entryAt(i));
  }
  MY_LOGD("Allocating %d entries from customization + sensor HAL + Dng Info",
          mtkMetadata->count());

  // overwrite
  updateData(mtkMetadata);
  //
  // get platform info
  // TODO(MTK): to update AF related metadata here
  NSCam::TuningUtils::TuningPlatformInfo tuning_info;
  NSCam::TuningUtils::PlatformInfo sensor_info;
  tuning_info.getTuningInfo(&sensor_info);

  {
    if (mInfo.getDeviceId() == 0) {
      mtkMetadata->remove(MTK_SENSOR_INFO_ORIENTATION);
      IMetadata::IEntry entryA(MTK_SENSOR_INFO_ORIENTATION);
      entryA.push_back(sensor_info.wf_sensor.orientation, Type2Type<MINT32>());
      mtkMetadata->update(MTK_SENSOR_INFO_ORIENTATION, entryA);

      mtkMetadata->remove(MTK_SENSOR_INFO_FACING);
      IMetadata::IEntry entryC(MTK_SENSOR_INFO_FACING);
      entryC.push_back(MTK_LENS_FACING_BACK, Type2Type<MUINT8>());
      mtkMetadata->update(MTK_SENSOR_INFO_FACING, entryC);

      MY_LOGD("sensor %d update orientation %d", mInfo.getDeviceId(),
              sensor_info.wf_sensor.orientation);
    } else if (mInfo.getDeviceId() == 1) {
      mtkMetadata->remove(MTK_SENSOR_INFO_ORIENTATION);
      IMetadata::IEntry entryA(MTK_SENSOR_INFO_ORIENTATION);
      entryA.push_back(sensor_info.uf_sensor.orientation, Type2Type<MINT32>());
      mtkMetadata->update(MTK_SENSOR_INFO_ORIENTATION, entryA);

      mtkMetadata->remove(MTK_SENSOR_INFO_FACING);
      IMetadata::IEntry entryC(MTK_SENSOR_INFO_FACING);
      entryC.push_back(MTK_LENS_FACING_FRONT, Type2Type<MUINT8>());
      mtkMetadata->update(MTK_SENSOR_INFO_FACING, entryC);

      MY_LOGD("sensor %d update orientation %d", mInfo.getDeviceId(),
              sensor_info.uf_sensor.orientation);
    }
    // AF
    if (mInfo.getDeviceId() == 0) {
      if (sensor_info.wf_sensor.minFocusDistance == 0) {  // fixed focus, update
        MY_LOGD("main.minFocusDistance: %f, remove AF regions in availableKeys",
                sensor_info.wf_sensor.minFocusDistance);
        // MTK_CONTROL_AF_REGIONS
        auto availRequestEntry =
            mtkMetadata->entryFor(MTK_REQUEST_AVAILABLE_REQUEST_KEYS);
        auto availResultEntry =
            mtkMetadata->entryFor(MTK_REQUEST_AVAILABLE_RESULT_KEYS);
        for (size_t i = 0; i < availRequestEntry.count(); i++) {
          if (availRequestEntry.itemAt(i, Type2Type<MINT32>()) ==
              MTK_CONTROL_AF_REGIONS) {
            availRequestEntry.removeAt(i);
            break;
          }
        }
        for (size_t i = 0; i < availResultEntry.count(); i++) {
          if (availResultEntry.itemAt(i, Type2Type<MINT32>()) ==
              MTK_CONTROL_AF_REGIONS) {
            availResultEntry.removeAt(i);
            break;
          }
        }
        mtkMetadata->update(MTK_REQUEST_AVAILABLE_REQUEST_KEYS,
                            availRequestEntry);
        mtkMetadata->update(MTK_REQUEST_AVAILABLE_RESULT_KEYS,
                            availResultEntry);
      }
    }
  }

#if (PLATFORM_SDK_VERSION >= 21)
  pConverter->convert(*mtkMetadata, rpDstMetadata);
  //
  ::sort_camera_metadata(*rpDstMetadata);
#endif

  return status;
}

/******************************************************************************
 *
 ******************************************************************************/
template <class T>
struct converter {
  converter(T const& tag,
            T const& srcFormat,
            T const& dstFormat,
            IMetadata* data) {
    IMetadata::IEntry entry = data->entryFor(tag);
    copy(srcFormat, dstFormat, &entry);
    data->update(tag, entry);
  }

  void copy(T const& srcFormat, T const& dstFormat, IMetadata::IEntry* entry) {
    T input = MTK_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT;
    for (size_t i = 0; i < entry->count(); i += 4) {
      if (entry->itemAt(i, Type2Type<T>()) != srcFormat ||
          entry->itemAt(i + 3, Type2Type<T>()) == input) {
        continue;
      }
      entry->push_back(dstFormat, Type2Type<T>());
      entry->push_back(entry->itemAt(i + 1, Type2Type<T>()), Type2Type<T>());
      entry->push_back(entry->itemAt(i + 2, Type2Type<T>()), Type2Type<T>());
      entry->push_back(entry->itemAt(i + 3, Type2Type<T>()), Type2Type<T>());
    }
  }
};

void MetadataProvider::updateData(IMetadata* rMetadata) {
  {
    MINT32 maxJpegsize = 0;
    IMetadata::IEntry blobEntry =
        rMetadata->entryFor(MTK_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
    for (size_t i = 0; i < blobEntry.count(); i += 4) {
      if (blobEntry.itemAt(i, Type2Type<MINT32>()) != HAL_PIXEL_FORMAT_BLOB) {
        continue;
      }
      // avaiblable blob size list should order in descedning.
      MSize maxBlob = MSize(blobEntry.itemAt(i + 1, Type2Type<MINT32>()),
                            blobEntry.itemAt(i + 2, Type2Type<MINT32>()));
      MINT32 jpegsize = maxBlob.size() * 1.5;
      if (jpegsize > maxJpegsize) {
        maxJpegsize = jpegsize;
      }
      IMetadata::IEntry entry(MTK_JPEG_MAX_SIZE);
      entry.push_back(maxJpegsize, Type2Type<MINT32>());
      rMetadata->update(MTK_JPEG_MAX_SIZE, entry);
    }
  }

  // update implementation defined
  {
    converter<MINT32>(MTK_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                      HAL_PIXEL_FORMAT_YCbCr_420_888,
                      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, rMetadata);
    //
    converter<MINT64>(MTK_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                      HAL_PIXEL_FORMAT_YCbCr_420_888,
                      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, rMetadata);
    //
    converter<MINT64>(MTK_SCALER_AVAILABLE_STALL_DURATIONS,
                      HAL_PIXEL_FORMAT_YCbCr_420_888,
                      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, rMetadata);
  }

  // update HDR Request Common Type
  {
    IMetadata::IEntry availReqEntry =
        rMetadata->entryFor(MTK_REQUEST_AVAILABLE_REQUEST_KEYS);
    availReqEntry.push_back(MTK_HDR_FEATURE_HDR_MODE, Type2Type<MINT32>());
    rMetadata->update(availReqEntry.tag(), availReqEntry);

    availReqEntry.push_back(MTK_HDR_FEATURE_SESSION_PARAM_HDR_MODE,
                            Type2Type<MINT32>());
    rMetadata->update(availReqEntry.tag(), availReqEntry);

    IMetadata::IEntry availResultEntry =
        rMetadata->entryFor(MTK_REQUEST_AVAILABLE_RESULT_KEYS);
    availResultEntry.push_back(MTK_HDR_FEATURE_HDR_DETECTION_RESULT,
                               Type2Type<MINT32>());
    rMetadata->update(availResultEntry.tag(), availResultEntry);

    IMetadata::IEntry availCharactsEntry =
        rMetadata->entryFor(MTK_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS);
    availCharactsEntry.push_back(MTK_HDR_FEATURE_AVAILABLE_HDR_MODES_PHOTO,
                                 Type2Type<MINT32>());
    availCharactsEntry.push_back(MTK_HDR_FEATURE_AVAILABLE_HDR_MODES_VIDEO,
                                 Type2Type<MINT32>());
    rMetadata->update(availCharactsEntry.tag(), availCharactsEntry);
  }

  {
    IMetadata::IEntry availReqEntry =
        rMetadata->entryFor(MTK_REQUEST_AVAILABLE_REQUEST_KEYS);
    availReqEntry.push_back(MTK_EIS_FEATURE_EIS_MODE, Type2Type<MINT32>());
    rMetadata->update(availReqEntry.tag(), availReqEntry);
  }

  // update Available vHDR Mode & HDR Modes
  {
    // -----------------
    // -- isHDRSensor --
    // -----------------
    // Available vHDR mode
    IMetadata::IEntry availVhdrEntry =
        rMetadata->entryFor(MTK_HDR_FEATURE_AVAILABLE_VHDR_MODES);
    if (availVhdrEntry.isEmpty()) {
      IMetadata::IEntry entry(MTK_HDR_FEATURE_AVAILABLE_VHDR_MODES);
      entry.push_back(MTK_HDR_FEATURE_VHDR_MODE_OFF, Type2Type<MINT32>());
      rMetadata->update(entry.tag(), entry);
      availVhdrEntry = entry;
    }
    MBOOL isHDRSensor = isHdrSensor(availVhdrEntry.count());

    // --------------------------
    // -- isSingleFrameSupport --
    // --------------------------
    IMetadata::IEntry singleFrameHdrEntry =
        rMetadata->entryFor(MTK_HDR_FEATURE_AVAILABLE_SINGLE_FRAME_HDR);
    MBOOL isSingleFrameSupport =
        (singleFrameHdrEntry.count() > 0) &&
        (singleFrameHdrEntry.itemAt(0, Type2Type<MUINT8>()) ==
         MTK_HDR_FEATURE_SINGLE_FRAME_HDR_SUPPORTED);
    MINT32 singleFrameProp =
        property_get_int32("debug.camera.hal3.singleFrame", -1);
    isSingleFrameSupport =
        (singleFrameProp != -1) ? (singleFrameProp > 0) : isSingleFrameSupport;

    // ----------------------
    // -- hdrDetectionMode --
    // ----------------------
    MINT32 hdrDetectionMode = MTKCAM_HDR_DETECTION_MODE;
    // 1 : hdr sensor,  2 : generic sensor, 3 : all sensors*/
    // HDR Detection support force switch
    MINT32 hdrDetectProp =
        property_get_int32("debug.camera.hal3.hdrDetection", 0);
    hdrDetectionMode = (hdrDetectProp != -1) ? hdrDetectProp : hdrDetectionMode;

    // update availHdrPhoto & availHdrVideo Metadata
    updateHdrData(isHDRSensor, isSingleFrameSupport, hdrDetectionMode,
                  rMetadata);
  }

  // update Available 3DNR Mode
  {
    IMetadata::IEntry avail3DNREntry =
        rMetadata->entryFor(MTK_NR_FEATURE_AVAILABLE_3DNR_MODES);
#ifndef NR3D_SUPPORTED
    avail3DNREntry.clear();
    avail3DNREntry.push_back(MTK_NR_FEATURE_3DNR_MODE_OFF, Type2Type<MINT32>());
    rMetadata->update(avail3DNREntry.tag(), avail3DNREntry);
#else
    IMetadata::IEntry availSessionEntry =
        rMetadata->entryFor(MTK_REQUEST_AVAILABLE_SESSION_KEYS);
    if (!availSessionEntry.isEmpty()) {
      availSessionEntry.push_back(MTK_NR_FEATURE_3DNR_MODE,
                                  Type2Type<MINT32>());
      rMetadata->update(availSessionEntry.tag(), availSessionEntry);
    } else {
      IMetadata::IEntry entry(MTK_REQUEST_AVAILABLE_SESSION_KEYS);
      entry.push_back(MTK_NR_FEATURE_3DNR_MODE, Type2Type<MINT32>());
      rMetadata->update(entry.tag(), entry);
    }
#endif
  }

#if 1  // MTKCAM_HAVE_MFB_SUPPORT fill Default value = off even MFB is not
       // support
  // update AIS Request Common Type
  IMetadata::IEntry availAisModeEntry =
      rMetadata->entryFor(MTK_MFNR_FEATURE_AVAILABLE_AIS_MODES);
  if (availAisModeEntry.isEmpty()) {
    IMetadata::IEntry availReqEntry =
        rMetadata->entryFor(MTK_REQUEST_AVAILABLE_REQUEST_KEYS);
    availReqEntry.push_back(MTK_MFNR_FEATURE_AIS_MODE, Type2Type<MINT32>());
    rMetadata->update(availReqEntry.tag(), availReqEntry);

    IMetadata::IEntry availResultEntry =
        rMetadata->entryFor(MTK_REQUEST_AVAILABLE_RESULT_KEYS);
    availResultEntry.push_back(MTK_MFNR_FEATURE_AIS_RESULT,
                               Type2Type<MINT32>());
    rMetadata->update(availResultEntry.tag(), availResultEntry);

    IMetadata::IEntry entry(MTK_MFNR_FEATURE_AVAILABLE_AIS_MODES);
    entry.push_back(MTK_MFNR_FEATURE_AIS_OFF, Type2Type<MINT32>());
    rMetadata->update(entry.tag(), entry);
  }
  // update MFB Request Common Type
  IMetadata::IEntry availMfbModeEntry =
      rMetadata->entryFor(MTK_MFNR_FEATURE_AVAILABLE_MFB_MODES);
  if (availAisModeEntry.isEmpty()) {
    IMetadata::IEntry availReqEntry =
        rMetadata->entryFor(MTK_REQUEST_AVAILABLE_REQUEST_KEYS);
    availReqEntry.push_back(MTK_MFNR_FEATURE_MFB_MODE, Type2Type<MINT32>());
    rMetadata->update(availReqEntry.tag(), availReqEntry);

    IMetadata::IEntry availResultEntry =
        rMetadata->entryFor(MTK_REQUEST_AVAILABLE_RESULT_KEYS);
    availResultEntry.push_back(MTK_MFNR_FEATURE_MFB_RESULT,
                               Type2Type<MINT32>());
    rMetadata->update(availResultEntry.tag(), availResultEntry);

    IMetadata::IEntry entry(MTK_MFNR_FEATURE_AVAILABLE_MFB_MODES);
    entry.push_back(MTK_MFNR_FEATURE_MFB_OFF, Type2Type<MINT32>());
#if MTKCAM_HAVE_MFB_SUPPORT == 1
    entry.push_back(MTK_MFNR_FEATURE_MFB_MFLL, Type2Type<MINT32>());
    entry.push_back(MTK_MFNR_FEATURE_MFB_AUTO, Type2Type<MINT32>());
#elif MTKCAM_HAVE_MFB_SUPPORT == 2
    entry.push_back(MTK_MFNR_FEATURE_MFB_AIS, Type2Type<MINT32>());
    entry.push_back(MTK_MFNR_FEATURE_MFB_AUTO, Type2Type<MINT32>());
#elif MTKCAM_HAVE_MFB_SUPPORT == 3
    entry.push_back(MTK_MFNR_FEATURE_MFB_MFLL, Type2Type<MINT32>());
    entry.push_back(MTK_MFNR_FEATURE_MFB_AIS, Type2Type<MINT32>());
    entry.push_back(MTK_MFNR_FEATURE_MFB_AUTO, Type2Type<MINT32>());
#endif
    rMetadata->update(entry.tag(), entry);
  }
#endif  // MTKCAM_HAVE_MFB_SUPPORT

  // update Streaming Request Common Type
  {
    IMetadata::IEntry availReqEntry =
        rMetadata->entryFor(MTK_REQUEST_AVAILABLE_REQUEST_KEYS);
    availReqEntry.push_back(MTK_STREAMING_FEATURE_RECORD_STATE,
                            Type2Type<MINT32>());
    rMetadata->update(availReqEntry.tag(), availReqEntry);

    IMetadata::IEntry availCharactsEntry =
        rMetadata->entryFor(MTK_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS);
    availCharactsEntry.push_back(MTK_STREAMING_FEATURE_AVAILABLE_RECORD_STATES,
                                 Type2Type<MINT32>());
    rMetadata->update(availCharactsEntry.tag(), availCharactsEntry);
  }
  // update Streaming Available EIS ControlFlow
  {
    IMetadata::IEntry availRecordEntry =
        rMetadata->entryFor(MTK_STREAMING_FEATURE_AVAILABLE_RECORD_STATES);
    if (availRecordEntry.isEmpty()) {
      IMetadata::IEntry entry(MTK_STREAMING_FEATURE_AVAILABLE_RECORD_STATES);
      entry.push_back(MTK_STREAMING_FEATURE_RECORD_STATE_PREVIEW,
                      Type2Type<MINT32>());
      entry.push_back(MTK_STREAMING_FEATURE_RECORD_STATE_RECORD,
                      Type2Type<MINT32>());
      rMetadata->update(entry.tag(), entry);
    }
  }

#ifndef EIS_SUPPORTED
  // update EIS support
  {
    IMetadata::IEntry availEISEntry =
        rMetadata->entryFor(MTK_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES);
    if (!availEISEntry.isEmpty()) {
      availEISEntry.clear();
      availEISEntry.push_back(MTK_CONTROL_VIDEO_STABILIZATION_MODE_OFF,
                              Type2Type<MUINT8>());
      rMetadata->update(availEISEntry.tag(), availEISEntry);
    }
    IMetadata::IEntry availAdvEISEntry =
        rMetadata->entryFor(MTK_EIS_FEATURE_EIS_MODE);
    if (!availAdvEISEntry.isEmpty()) {
      availAdvEISEntry.clear();
      availAdvEISEntry.push_back(MTK_EIS_FEATURE_EIS_MODE_OFF,
                                 Type2Type<MINT32>());
      rMetadata->update(availAdvEISEntry.tag(), availAdvEISEntry);
    }
  }
#else
  {
    IMetadata::IEntry availSessionEntry =
        rMetadata->entryFor(MTK_REQUEST_AVAILABLE_SESSION_KEYS);
    if (!availSessionEntry.isEmpty()) {
      availSessionEntry.push_back(MTK_CONTROL_VIDEO_STABILIZATION_MODE,
                                  Type2Type<MINT32>());
      availSessionEntry.push_back(MTK_EIS_FEATURE_EIS_MODE,
                                  Type2Type<MINT32>());
      rMetadata->update(availSessionEntry.tag(), availSessionEntry);
    } else {
      IMetadata::IEntry entry(MTK_REQUEST_AVAILABLE_SESSION_KEYS);
      entry.push_back(MTK_CONTROL_VIDEO_STABILIZATION_MODE,
                      Type2Type<MINT32>());
      entry.push_back(MTK_EIS_FEATURE_EIS_MODE, Type2Type<MINT32>());
      rMetadata->update(entry.tag(), entry);
    }
  }
#endif
  // update sensor sync timestamp Type
  {
    IMetadata::IEntry timestampEntry(MTK_SENSOR_SYNC_TIMESTAMP);
    timestampEntry.push_back(
        static_cast<int32_t>(cros::mojom::CameraSensorSyncTimestamp::NONE),
        Type2Type<MINT32>());
    rMetadata->update(MTK_SENSOR_SYNC_TIMESTAMP, timestampEntry);
    for (size_t i = 0; i < timestampEntry.count(); i += 4) {
      MY_LOGI("MTK_SENSOR_SYNC_TIMESTAMP i:%d,value:%d", i,
              timestampEntry.itemAt(i, Type2Type<MINT32>()));
    }
  }

  // update multi-cam feature mode to static metadata
  // vendor tag
  {
    auto pHalDeviceList = MAKE_HalLogicalDeviceList();
    auto physicIdsList = pHalDeviceList->getSensorIds(mInfo.getDeviceId());
    if (physicIdsList.size() > 1) {
      MBOOL needAddCharactersticsKeys = MFALSE;
      auto supportedFeature =
          pHalDeviceList->getSupportedFeature(mInfo.getDeviceId());
      // add feature mode
      {
        IMetadata::IEntry entry(MTK_MULTI_CAM_FEATURE_AVAILABLE_MODE);
        if (supportedFeature & DEVICE_FEATURE_ZOOM) {
          MY_LOGD("deviceid(%d) support zoom feature", mInfo.getDeviceId());
          entry.push_back(MTK_MULTI_CAM_FEATURE_MODE_ZOOM, Type2Type<MINT32>());
          needAddCharactersticsKeys = MTRUE;
        }
        if (supportedFeature & DEVICE_FEATURE_DENOISE) {
          MY_LOGD("deviceid(%d) support denoise feature", mInfo.getDeviceId());
          entry.push_back(MTK_MULTI_CAM_FEATURE_MODE_DENOISE,
                          Type2Type<MINT32>());
          needAddCharactersticsKeys = MTRUE;
        }
        rMetadata->update(entry.tag(), entry);
        if (needAddCharactersticsKeys) {
          MY_LOGD("AddCharactersticsKeys for feature mode");

          IMetadata::IEntry availCharactsEntry =
              rMetadata->entryFor(MTK_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS);
          availCharactsEntry.push_back(MTK_MULTI_CAM_FEATURE_AVAILABLE_MODE,
                                       Type2Type<MINT32>());
          rMetadata->update(availCharactsEntry.tag(), availCharactsEntry);
        }
      }
    }
  }
  // update logic device related metadata tag
  {
    auto pHalDeviceList = MAKE_HalLogicalDeviceList();
    auto physicIdsList = pHalDeviceList->getSensorIds(mInfo.getDeviceId());
    if (physicIdsList.size() > 1) {
      // update logic physic ids
      {
        std::string idsListString;
        IMetadata::IEntry entry(MTK_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS);
        for (auto id : physicIdsList) {
          // ascii
          entry.push_back(id + 48, Type2Type<MUINT8>());
          entry.push_back('\0', Type2Type<MUINT8>());
          idsListString += std::to_string(id);
          idsListString += " ";
        }
        MY_LOGD("update logic id (%s:%x)", idsListString.c_str(),
                MTK_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS);
        rMetadata->update(entry.tag(), entry);
      }
      // update sync type
      {
        auto syncType = pHalDeviceList->getSyncType(mInfo.getDeviceId());
        if (NSCam::SensorSyncType::NOT_SUPPORT != syncType) {
          MUINT8 value = MTK_LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE_APPROXIMATE;
          switch (syncType) {
            case NSCam::SensorSyncType::APPROXIMATE:
              value = MTK_LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE_APPROXIMATE;
              break;
            case NSCam::SensorSyncType::CALIBRATED:
              value = MTK_LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE_CALIBRATED;
              break;
            default:
              MY_LOGE("invaild sync type");
              break;
          }
          IMetadata::IEntry entry(MTK_LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE);
          entry.push_back(value, Type2Type<MUINT8>());
          rMetadata->update(entry.tag(), entry);
          auto toString = [&value]() {
            switch (value) {
              case MTK_LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE_APPROXIMATE:
                return "Approximate";
              case MTK_LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE_CALIBRATED:
                return "Calibrated";
              default:
                return "not support";
            }
          };
          MY_LOGD("update sync type (%s:%x)", toString(),
                  MTK_LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE);
        }
      }
    }
  }
}

MBOOL
MetadataProvider::isHdrSensor(MUINT const availVhdrEntryCount) {
  MBOOL isHDRSensor = (availVhdrEntryCount > 1);
  char strVhdrLog[100];
  memset(strVhdrLog, '\0', sizeof(strVhdrLog));

  // query sensor static info from sensor driver to decide Hal3 vHDR support
  NSCam::IHalLogicalDeviceList* pHalDeviceList;
  pHalDeviceList = MAKE_HalLogicalDeviceList();
  if (pHalDeviceList == NULL) {
    MY_LOGE("pHalDeviceList::get fail");
  } else {
    MUINT32 sensorDev =
        (MUINT32)pHalDeviceList->querySensorDevIdx(mInfo.getDeviceId());
    NSCam::SensorStaticInfo sensorStaticInfo;
    pHalDeviceList->querySensorStaticInfo(sensorDev, &sensorStaticInfo);
    isHDRSensor = (sensorStaticInfo.HDR_Support > 0) ? isHDRSensor : false;
    snprintf(strVhdrLog, sizeof(strVhdrLog),
             " sensorDev:%d, sensorStaticInfo.HDR_Support:%d,", sensorDev,
             sensorStaticInfo.HDR_Support);
  }
  // force set ON/OFF Hal3 vHDR support
  MINT32 vhdrHal3Prop = property_get_int32("debug.camera.hal3.vhdrSupport", -1);
  isHDRSensor = (vhdrHal3Prop != -1) ? (vhdrHal3Prop > 0) : isHDRSensor;
  MY_LOGD("isHDRSensor:%d, vhdrHal3Prop:%d,%s availVhdrEntry.count():%d",
          isHDRSensor, vhdrHal3Prop, strVhdrLog, availVhdrEntryCount);

  return isHDRSensor;
}

MVOID
MetadataProvider::updateHdrData(MBOOL const isHDRSensor,
                                MBOOL const isSingleFrameSupport,
                                MINT32 const hdrDetectionMode,
                                IMetadata* rMetadata) {
  // Available HDR modes for Photo & Video
  IMetadata::IEntry availHdrPhotoEntry(
      MTK_HDR_FEATURE_AVAILABLE_HDR_MODES_PHOTO);
  IMetadata::IEntry availHdrVideoEntry(
      MTK_HDR_FEATURE_AVAILABLE_HDR_MODES_VIDEO);

  // --- MODE_OFF ----
  availHdrPhotoEntry.push_back(MTK_HDR_FEATURE_HDR_MODE_OFF,
                               Type2Type<MINT32>());
  availHdrVideoEntry.push_back(MTK_HDR_FEATURE_HDR_MODE_OFF,
                               Type2Type<MINT32>());

#if (MTKCAM_HAVE_VHDR_SUPPORT == 1)
  // --- MODE_VIDEO_ON ----
  if (isHDRSensor) {
    availHdrPhotoEntry.push_back(MTK_HDR_FEATURE_HDR_MODE_VIDEO_ON,
                                 Type2Type<MINT32>());
    availHdrVideoEntry.push_back(MTK_HDR_FEATURE_HDR_MODE_VIDEO_ON,
                                 Type2Type<MINT32>());
  }
#endif

#if (MTKCAM_HAVE_HDR_SUPPORT == 1)
  // --- MODE_ON ----
  availHdrPhotoEntry.push_back(MTK_HDR_FEATURE_HDR_MODE_ON,
                               Type2Type<MINT32>());
  /* Video mode not support MODE_ON*/

  // --- MODE_AUTO ----
  if (hdrDetectionMode == 3 || (hdrDetectionMode == 2 && !isHDRSensor) ||
      (hdrDetectionMode == 1 && isHDRSensor)) {
    availHdrPhotoEntry.push_back(MTK_HDR_FEATURE_HDR_MODE_AUTO,
                                 Type2Type<MINT32>());
  }
  /* Video mode not support MODE_AUTO*/

  // --- MODE_VIDEO_AUTO ----
  if (isHDRSensor && (hdrDetectionMode == 1 || hdrDetectionMode == 3)) {
    availHdrVideoEntry.push_back(MTK_HDR_FEATURE_HDR_MODE_VIDEO_AUTO,
                                 Type2Type<MINT32>());
    if (isSingleFrameSupport) {
      availHdrPhotoEntry.push_back(MTK_HDR_FEATURE_HDR_MODE_VIDEO_AUTO,
                                   Type2Type<MINT32>());
    }
  }
#endif  // MTKCAM_HAVE_HDR_SUPPORT endif

  rMetadata->update(availHdrPhotoEntry.tag(), availHdrPhotoEntry);
  rMetadata->update(availHdrVideoEntry.tag(), availHdrVideoEntry);
}
