/*
 * Copyright (C) 2013-2019 Intel Corporation
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

#define LOG_TAG "Profiles"

#include <expat.h>
#include <limits.h>
#include <dirent.h>
#include <system/camera_metadata.h>

#include "LogHelper.h"
#include "CameraProfiles.h"
#include "Metadata.h"
#include "CameraMetadataHelper.h"
#include "GraphConfigManager.h"
#include "Utils.h"
#include "NodeTypes.h"
#include "IPU3Types.h"

#define STATIC_ENTRY_CAP 256
#define STATIC_DATA_CAP 6688 // TODO: we may need to increase it again if more metadata is added
#define MAX_METADATA_NAME_LENTGTH 128
#define MAX_METADATA_ATTRIBUTE_NAME_LENTGTH 128
#define MAX_METADATA_ATTRIBUTE_VALUE_LENTGTH 6144
#define CIO2_MEDIA_DEVICE "ipu3-cio2"
#define IMGU_MEDIA_DEVICE "ipu3-imgu"
#define NVM_DATA_PATH "/sys/bus/i2c/devices/"
#define GRAPH_SETTINGS_FILE_PATH "/etc/camera/"
#define DEFAULT_XML_FILE_NAME "/etc/camera/camera3_profiles.xml"

namespace cros {
namespace intel {
CameraProfiles::CameraProfiles(CameraHWInfo *cameraHWInfo) :
    mCurrentDataField(FIELD_INVALID),
    mMetadataCache(nullptr),
    mXmlSensorIndex(-1),
    mItemsCount(-1),
    mUseEntry(true),
    mCameraCommon(cameraHWInfo)
{
    CLEAR(mProfileEnd);
    CLEAR(mFaceAeEnabled);
    for (int i = 0; i < MAX_CAMERAS; i++) {
        mFaceEngineRunningInterval[i] = FACE_ENGINE_DEFAULT_RUNNING_INTERVAL;
    }
}

status_t CameraProfiles::init()
{
    LOG1("@%s", __FUNCTION__);
    CheckError(!mCameraCommon, BAD_VALUE, "CameraHWInfo is nullptr");

    struct stat st;
    int pathExists = stat(DEFAULT_XML_FILE_NAME, &st);
    CheckError(pathExists != 0, UNKNOWN_ERROR, "Error, could not find camera3_profiles.xml!");

    status_t status = mCameraCommon->init(getMediaDeviceByName(CIO2_MEDIA_DEVICE));
    CheckError(status != OK, UNKNOWN_ERROR, "Failed to init camera HW");

    // Assumption: Driver enumeration order will match the CameraId
    // CameraId in camera_profiles.xml. Main camera is always at
    // index 0, front camera at index 1.
    if (mCameraCommon->mSensorInfo.empty()) {
        LOGE("No sensor Info available, exit parsing");
        return UNKNOWN_ERROR;
    }

    mSensorNames = mCameraCommon->mSensorInfo;

    for (int i = 0;i < MAX_CAMERAS; i++)
         mCharacteristicsKeys[i].clear();

    getDataFromXmlFile();
    getGraphConfigFromXmlFile();

    return status;
}

int CameraProfiles::getXmlCameraId(int cameraId) const
{
    LOG2("@%s", __FUNCTION__);
    bool findMatchCameraId = false;

    auto it = std::find(mCameraIdPool.begin(), mCameraIdPool.end(), cameraId);
    if (it != mCameraIdPool.end())
        findMatchCameraId = true;

    return findMatchCameraId ? cameraId : NAME_NOT_FOUND;
}

bool CameraProfiles::isFaceAeEnabled(int cameraId) const
{
    CheckError(cameraId < 0 || cameraId >= MAX_CAMERAS, false, "cameraId:%d is incorrect", cameraId);
    return mFaceAeEnabled[cameraId];
}

int CameraProfiles::faceEngineRunningInterval(int cameraId) const
{
    CheckError(cameraId < 0 || cameraId >= MAX_CAMERAS, FACE_ENGINE_DEFAULT_RUNNING_INTERVAL, "cameraId:%d is incorrect", cameraId);
    return mFaceEngineRunningInterval[cameraId];
}

CameraProfiles::~CameraProfiles()
{
    LOG2("@%s", __FUNCTION__);
    for (unsigned int i = 0; i < mStaticMeta.size(); i++) {
        if (mStaticMeta[i])
            free_camera_metadata(mStaticMeta[i]);
    }

    mStaticMeta.clear();
    mCameraIdPool.clear();
    mSensorNames.clear();

    while (!mCaps.empty()) {
        IPU3CameraCapInfo* info = static_cast<IPU3CameraCapInfo*>(mCaps.front());
        mCaps.erase(mCaps.begin());
        delete info;
    }

    for (unsigned int i = 0; i < mDefaultRequests.size(); i++) {
       if (mDefaultRequests[i])
            free_camera_metadata(mDefaultRequests[i]);
    }

    mDefaultRequests.clear();
}

const CameraCapInfo *CameraProfiles::getCameraCapInfo(int cameraId)
{
    auto it = std::find(mCameraIdPool.begin(), mCameraIdPool.end(), cameraId);
    CheckError(it == mCameraIdPool.end(), nullptr, "Failed to find match camera id.");

    return mCaps[cameraId];
}

uint8_t CameraProfiles::selectAfMode(const camera_metadata_t *staticMeta,
                                    int reqTemplate)
{
    // For initial value, use AF_MODE_OFF. That is the minimum,
    // for fixed-focus sensors. For request templates the desired
    // values will be defined below.
    uint8_t afMode = ANDROID_CONTROL_AF_MODE_OFF;

    const int MAX_AF_MODES = 6;
    // check this is the maximum number of enums defined by:
    // camera_metadata_enum_android_control_af_mode_t defined in
    // camera_metadata_tags.h
    camera_metadata_ro_entry ro_entry;
    bool modesAvailable[MAX_AF_MODES];
    CLEAR(modesAvailable);
    find_camera_metadata_ro_entry(staticMeta, ANDROID_CONTROL_AF_AVAILABLE_MODES,
                                  &ro_entry);
    if (ro_entry.count > 0) {
        for (size_t i = 0; i < ro_entry.count; i++) {
            if (ro_entry.data.u8[i] < MAX_AF_MODES)
                modesAvailable[ro_entry.data.u8[i]] = true;
        }
    } else {
        LOGE("@%s: Incomplete camera3_profiles.xml: available AF modes missing!!", __FUNCTION__);
        // we only support AUTO
        modesAvailable[ANDROID_CONTROL_AF_MODE_AUTO] = true;
    }

    switch (reqTemplate) {
       case ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE:
       case ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG:
       case ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW:
           if (modesAvailable[ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE])
               afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
           break;
       case ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD:
       case ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT:
           if (modesAvailable[ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO])
               afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
           break;
       case ANDROID_CONTROL_CAPTURE_INTENT_MANUAL:
           if (modesAvailable[ANDROID_CONTROL_AF_MODE_OFF])
               afMode = ANDROID_CONTROL_AF_MODE_OFF;
           break;
       case ANDROID_CONTROL_CAPTURE_INTENT_START:
       default:
           afMode = ANDROID_CONTROL_AF_MODE_AUTO;
           break;
       }
    return afMode;
}

camera_metadata_t *CameraProfiles::constructDefaultMetadata(int cameraId, int requestTemplate)
{
    LOG2("@%s: camera id: %d, request template: %d", __FUNCTION__, cameraId, requestTemplate);
    if (requestTemplate >= CAMERA_TEMPLATE_COUNT) {
        LOGE("ERROR @%s: bad template %d", __FUNCTION__, requestTemplate);
        return nullptr;
    }

    int index = cameraId *CAMERA_TEMPLATE_COUNT + requestTemplate;
    camera_metadata_t *req = mDefaultRequests[index];

    if (req)
        return req;

    camera_metadata_t *meta = nullptr;
    meta = allocate_camera_metadata(DEFAULT_ENTRY_CAP, DEFAULT_DATA_CAP);
    if (meta == nullptr) {
        LOGE("ERROR @%s: Allocate memory failed", __FUNCTION__);
        return nullptr;
    }

    const camera_metadata_t *staticMeta = nullptr;
    staticMeta = PlatformData::getStaticMetadata(cameraId);
    if (staticMeta == nullptr) {
        LOGE("ERROR @%s: Could not get static metadata", __FUNCTION__);
        free_camera_metadata(meta);
        return nullptr;
    }

    android::CameraMetadata metadata; // no constructor from const camera_metadata_t*
    metadata = staticMeta; // but assignment operator exists for const

    int64_t bogusValue = 0;  // 8 bytes of bogus
    int64_t bogusValueArray[] = {0, 0, 0, 0, 0}; // 40 bytes of bogus

    uint8_t requestType = ANDROID_REQUEST_TYPE_CAPTURE;
    uint8_t intent = 0;

    uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    uint8_t afMode = selectAfMode(staticMeta, requestTemplate);
    uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    uint8_t nrMode = ANDROID_NOISE_REDUCTION_MODE_OFF;
    uint8_t eeMode = ANDROID_EDGE_MODE_OFF;
    camera_metadata_entry entry;

    switch (requestTemplate) {
    case ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW:
        intent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
        entry = metadata.find(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES);
        if (entry.count > 0) {
            nrMode = entry.data.u8[0];
            for (size_t i = 0; i < entry.count; i++) {
                if (entry.data.u8[i]
                        == ANDROID_NOISE_REDUCTION_MODE_FAST) {
                    nrMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
                    break;
                }
            }
        }
        entry = metadata.find(ANDROID_EDGE_AVAILABLE_EDGE_MODES);
        if (entry.count > 0) {
            eeMode = entry.data.u8[0];
            for (size_t i = 0; i < entry.count; i++) {
                if (entry.data.u8[i]
                        == ANDROID_EDGE_MODE_FAST) {
                    eeMode = ANDROID_EDGE_MODE_FAST;
                    break;
                }
            }
        }
        break;
    case ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE:
        intent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
        entry = metadata.find(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES);
        if (entry.count > 0) {
            nrMode = entry.data.u8[0];
            for (size_t i = 0; i < entry.count; i++) {
                if (entry.data.u8[i]
                        == ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY) {
                    nrMode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY;
                    break;
                }
            }
        }
        entry = metadata.find(ANDROID_EDGE_AVAILABLE_EDGE_MODES);
        if (entry.count > 0) {
            eeMode = entry.data.u8[0];
            for (size_t i = 0; i < entry.count; i++) {
                if (entry.data.u8[i]
                        == ANDROID_EDGE_MODE_HIGH_QUALITY) {
                    eeMode = ANDROID_EDGE_MODE_HIGH_QUALITY;
                    break;
                }
            }
        }
        break;
    case ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD:
        intent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
        entry = metadata.find(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES);
        if (entry.count > 0) {
            nrMode = entry.data.u8[0];
            for (size_t i = 0; i < entry.count; i++) {
                if (entry.data.u8[i]
                        == ANDROID_NOISE_REDUCTION_MODE_FAST) {
                    nrMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
                    break;
                }
            }
        }
        entry = metadata.find(ANDROID_EDGE_AVAILABLE_EDGE_MODES);
        if (entry.count > 0) {
            eeMode = entry.data.u8[0];
            for (size_t i = 0; i < entry.count; i++) {
                if (entry.data.u8[i]
                        == ANDROID_EDGE_MODE_FAST) {
                    eeMode = ANDROID_EDGE_MODE_FAST;
                    break;
                }
            }
        }
        break;
    case ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT:
        intent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
        break;
    case ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG:
        intent = ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
        entry = metadata.find(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES);
        if (entry.count > 0) {
            nrMode = entry.data.u8[0];
            for (size_t i = 0; i < entry.count; i++) {
                if (entry.data.u8[i]
                        == ANDROID_NOISE_REDUCTION_MODE_ZERO_SHUTTER_LAG) {
                    nrMode = ANDROID_NOISE_REDUCTION_MODE_ZERO_SHUTTER_LAG;
                    break;
                }
            }
        }
        entry = metadata.find(ANDROID_EDGE_AVAILABLE_EDGE_MODES);
        if (entry.count > 0) {
            eeMode = entry.data.u8[0];
            for (size_t i = 0; i < entry.count; i++) {
                if (entry.data.u8[i]
                        == ANDROID_EDGE_MODE_ZERO_SHUTTER_LAG) {
                    eeMode = ANDROID_EDGE_MODE_ZERO_SHUTTER_LAG;
                    break;
                }
            }
        }
        break;
    case ANDROID_CONTROL_CAPTURE_INTENT_MANUAL:
        controlMode = ANDROID_CONTROL_MODE_OFF;
        aeMode = ANDROID_CONTROL_AE_MODE_OFF;
        awbMode = ANDROID_CONTROL_AWB_MODE_OFF;
        intent = ANDROID_CONTROL_CAPTURE_INTENT_MANUAL;
        break;
    default:
        intent = ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
        break;
    }

    camera_metadata_ro_entry ro_entry;
    find_camera_metadata_ro_entry(staticMeta, ANDROID_CONTROL_MAX_REGIONS,
                                  &ro_entry);
    // AE, AWB, AF
    if (ro_entry.count == 3) {
        int meteringRegion[METERING_RECT_SIZE] = {0,0,0,0,0};
        if (ro_entry.data.i32[0] == 1) {
            add_camera_metadata_entry(meta, ANDROID_CONTROL_AE_REGIONS,
                                      meteringRegion, METERING_RECT_SIZE);
        }
        if (ro_entry.data.i32[2] == 1) {
            add_camera_metadata_entry(meta, ANDROID_CONTROL_AF_REGIONS,
                                      meteringRegion, METERING_RECT_SIZE);
        }
        // we do not support AWB region
    }
#define TAGINFO(tag, data) \
    add_camera_metadata_entry(meta, tag, &data, 1)
#define TAGINFO_ARRAY(tag, data, count) \
    add_camera_metadata_entry(meta, tag, data, count)

    TAGINFO(ANDROID_CONTROL_CAPTURE_INTENT, intent);

    TAGINFO(ANDROID_CONTROL_MODE, controlMode);
    TAGINFO(ANDROID_CONTROL_EFFECT_MODE, bogusValue);
    TAGINFO(ANDROID_CONTROL_SCENE_MODE, bogusValue);
    TAGINFO(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, bogusValue);
    TAGINFO(ANDROID_CONTROL_AE_MODE, aeMode);
    TAGINFO(ANDROID_CONTROL_AE_LOCK, bogusValue);
    uint8_t value = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
    TAGINFO(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, value);
    value = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    TAGINFO(ANDROID_CONTROL_AF_TRIGGER, value);
    value = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    TAGINFO(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, value);
    int32_t mode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
    TAGINFO(ANDROID_SENSOR_TEST_PATTERN_MODE, mode);
    TAGINFO(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW, bogusValue);
    entry = metadata.find(ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES);
    if (entry.count > 0) {
      TAGINFO(ANDROID_HOT_PIXEL_MODE, entry.data.u8[0]);
    }
    value = ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
    TAGINFO(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE, value);
    value = ANDROID_STATISTICS_SCENE_FLICKER_NONE;
    TAGINFO(ANDROID_STATISTICS_SCENE_FLICKER, value);
    value = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
    TAGINFO(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, value);
    TAGINFO(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, bogusValue);

    TAGINFO(ANDROID_SYNC_FRAME_NUMBER, bogusValue);

    // Default fps target range
    int32_t fpsRange[] = {15, 30};
    camera_metadata_ro_entry fpsRangesEntry;

    fpsRangesEntry.count = 0;
    find_camera_metadata_ro_entry(
        staticMeta, ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
        &fpsRangesEntry);
    if ((fpsRangesEntry.count >= 2) && (fpsRangesEntry.count % 2 == 0)) {
      //choose closest (15,30) range
      size_t fpsNums = fpsRangesEntry.count / 2;
      int32_t delta = INT32_MAX;

      for (size_t i = 0; i < fpsNums; i += 2) {
          int32_t diff = abs(fpsRangesEntry.data.i32[i] - 15) +
            abs(fpsRangesEntry.data.i32[i+1] - 30);

          if (delta > diff) {
              fpsRange[0] = fpsRangesEntry.data.i32[i];
              fpsRange[1] = fpsRangesEntry.data.i32[i+1];

              delta = diff;
          }
      }
    } else {
      LOGW("No AE FPS range found in profile, use default [15, 30]");
    }
    if (requestTemplate == ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD) {
      // Stable range requried for video recording
      fpsRange[0] = fpsRange[1];
    }
    TAGINFO_ARRAY(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, fpsRange, 2);

    value = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    TAGINFO(ANDROID_CONTROL_AE_ANTIBANDING_MODE, value);
    TAGINFO(ANDROID_CONTROL_AWB_MODE, awbMode);
    TAGINFO(ANDROID_CONTROL_AWB_LOCK, bogusValue);
    TAGINFO(ANDROID_BLACK_LEVEL_LOCK, bogusValue);
    TAGINFO(ANDROID_CONTROL_AWB_STATE, bogusValue);
    TAGINFO(ANDROID_CONTROL_AF_MODE, afMode);

    value = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
    TAGINFO(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, value);

    TAGINFO(ANDROID_FLASH_MODE, bogusValue);

    TAGINFO(ANDROID_LENS_FOCUS_DISTANCE, bogusValue);

    TAGINFO(ANDROID_REQUEST_TYPE, requestType);
    TAGINFO(ANDROID_REQUEST_METADATA_MODE, bogusValue);
    TAGINFO(ANDROID_REQUEST_FRAME_COUNT, bogusValue);

    TAGINFO_ARRAY(ANDROID_SCALER_CROP_REGION, bogusValueArray, 4);

    TAGINFO(ANDROID_STATISTICS_FACE_DETECT_MODE, bogusValue);

    entry = metadata.find(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS);
    if (entry.count > 0) {
        TAGINFO(ANDROID_LENS_FOCAL_LENGTH, entry.data.f[0]);
    }
    // todo enable when region support is implemented
    // TAGINFO_ARRAY(ANDROID_CONTROL_AE_REGIONS, bogusValueArray, 5);
    TAGINFO(ANDROID_SENSOR_EXPOSURE_TIME, bogusValue);
    TAGINFO(ANDROID_SENSOR_SENSITIVITY, bogusValue);
    int64_t frameDuration = 33333333;
    TAGINFO(ANDROID_SENSOR_FRAME_DURATION, frameDuration);

    TAGINFO(ANDROID_JPEG_QUALITY, JPEG_QUALITY_DEFAULT);
    TAGINFO(ANDROID_JPEG_THUMBNAIL_QUALITY, THUMBNAIL_QUALITY_DEFAULT);

    entry = metadata.find(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES);
    int32_t thumbSize[] = { 0, 0 };
    if (entry.count >= 4) {
        thumbSize[0] = entry.data.i32[2];
        thumbSize[1] = entry.data.i32[3];
    } else {
        LOGE("Thumbnail size should have more than two resolutions: 0x0 and non zero size. Fix your camera profile");
        thumbSize[0] = 0;
        thumbSize[1] = 0;
    }

    TAGINFO_ARRAY(ANDROID_JPEG_THUMBNAIL_SIZE, thumbSize, 2);

    entry = metadata.find(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES);
    if (entry.count > 0) {
        value = entry.data.u8[0];
        for (uint32_t i = 0; i < entry.count; i++) {
            if (entry.data.u8[i] == ANDROID_TONEMAP_MODE_HIGH_QUALITY) {
                value = ANDROID_TONEMAP_MODE_HIGH_QUALITY;
                break;
            }
        }
        TAGINFO(ANDROID_TONEMAP_MODE, value);
    }

    TAGINFO(ANDROID_NOISE_REDUCTION_MODE, nrMode);
    TAGINFO(ANDROID_EDGE_MODE, eeMode);

    float colorTransform[9] = {1.0, 0.0, 0.0,
                               0.0, 1.0, 0.0,
                               0.0, 0.0, 1.0};

    camera_metadata_rational_t transformMatrix[9];
    for (int i = 0; i < 9; i++) {
        transformMatrix[i].numerator = colorTransform[i];
        transformMatrix[i].denominator = 1.0;
    }
    TAGINFO_ARRAY(ANDROID_COLOR_CORRECTION_TRANSFORM, transformMatrix, 9);

    float colorGains[4] = {1.0, 1.0, 1.0, 1.0};
    TAGINFO_ARRAY(ANDROID_COLOR_CORRECTION_GAINS, colorGains, 4);
    TAGINFO(ANDROID_COLOR_CORRECTION_MODE, bogusValue);

#undef TAGINFO
#undef TAGINFO_ARRAY

    int entryCount = get_camera_metadata_entry_count(meta);
    int dataCount = get_camera_metadata_data_count(meta);
    LOG2("%s: Real metadata entry count %d, data count %d", __FUNCTION__,
        entryCount, dataCount);
    if ((entryCount > DEFAULT_ENTRY_CAP - ENTRY_RESERVED)
        || (dataCount > DEFAULT_DATA_CAP - DATA_RESERVED))
        LOGW("%s: Need more memory, now entry %d (%d), data %d (%d)", __FUNCTION__,
        entryCount, DEFAULT_ENTRY_CAP, dataCount, DEFAULT_DATA_CAP);

    // sort the metadata before storing
    sort_camera_metadata(meta);
    if (mDefaultRequests.at(index)) {
        free_camera_metadata(mDefaultRequests.at(index));
    }
    mDefaultRequests.at(index) = meta;
    return meta;
}

status_t CameraProfiles::addCamera(int cameraId, const std::string &sensorName)
{
    LOG1("%s: for camera %d, name: %s", __FUNCTION__, cameraId, sensorName.c_str());

    camera_metadata_t *emptyReq = nullptr;
    CLEAR(emptyReq);

    camera_metadata_t *meta = allocate_camera_metadata(STATIC_ENTRY_CAP, STATIC_DATA_CAP);
    if (!meta) {
        LOGE("No memory for camera metadata!");
        return NO_MEMORY;
    }
    LOG2("Add cameraId: %d to mStaticMeta", cameraId);
    mStaticMeta.push_back(meta);

    SensorType type = SENSOR_TYPE_RAW;

    IPU3CameraCapInfo *info = new IPU3CameraCapInfo(type);
    info->mSensorName = sensorName;
    mCaps.push_back(info);

    for (int i = 0; i < CAMERA_TEMPLATE_COUNT; i++)
        mDefaultRequests.push_back(emptyReq);

    return NO_ERROR;
}

/**
 * convertEnum
 * Converts from the string provided as src to an enum value.
 * It uses a table to convert from the string to an enum value (usually BYTE)
 * \param [IN] dest: data buffer where to store the result
 * \param [IN] src: pointer to the string to parse
 * \param [IN] table: table to convert from string to value
 * \param [IN] tableNum: size of the enum table
 * \param [IN] type: data type to be parsed (byte, int32, int64 etc..)
 * \param [OUT] newDest: pointer to the new write location
 */
int CameraProfiles::convertEnum(void *dest, const char *src, int type,
                                const metadata_value_t *table, int tableLen,
                                void **newDest)
{
    int ret = 0;
    union {
        uint8_t *u8;
        int32_t *i32;
        int64_t *i64;
    } data;

    *newDest = dest;
    data.u8 = (uint8_t *)dest;

    /* ignore any spaces in front of the string */
    size_t blanks = strspn(src," ");
    src = src + blanks;

    for (int i = 0; i < tableLen; i++ ) {
        if (!strncasecmp(src, table[i].name, STRLEN_S(table[i].name))
            && (STRLEN_S(src) == STRLEN_S(table[i].name))) {
            if (type == TYPE_BYTE) {
                data.u8[0] = table[i].value;
                LOG1("byte    - %s: %d -", table[i].name, data.u8[0]);
                *newDest = (void*) &data.u8[1];
            } else if (type == TYPE_INT32) {
                data.i32[0] = table[i].value;
                LOG1("int    - %s: %d -", table[i].name, data.i32[0]);
                *newDest = (void*) &data.i32[1];
            } else if (type == TYPE_INT64) {
                data.i64[0] = table[i].value;
                LOG1("int64    - %s: %" PRId64 " -", table[i].name, data.i64[0]);
                *newDest = (void*) &data.i64[1];
            }
            ret = 1;
            break;
        }
    }

    return ret;
}

/**
 * parseEnum
 * parses an enumeration type or a list of enumeration types it stores the data
 * in the member buffer mMetadataCache that is of size mMetadataCacheSize.
 *
 * \param src: string to be parsed
 * \param tagInfo: structure with the description of the static metadata
 * \param metadataCacheSize:the upper limited size of the dest buffer
 * \param metadataCache: the dest buffer to store the medatada after persed
 * \return number of elements parsed
 */
int CameraProfiles::parseEnum(const char *src,
                              const metadata_tag_t *tagInfo,
                              int metadataCacheSize,
                              int64_t* metadataCache)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    int count = 0;
    int maxCount = metadataCacheSize / camera_metadata_type_size[tagInfo->type];
    char *endPtr = nullptr;

    /**
     * pointer to the metadata cache buffer
     */
    void *storeBuf = metadataCache;
    void *next;

    do {
        endPtr = (char*) strchr(src, ',');
        if (endPtr)
            *endPtr = 0;

        count += convertEnum(storeBuf, src,tagInfo->type,
                             tagInfo->enumTable, tagInfo->tableLength, &next);
        if (endPtr) {
            src = endPtr + 1;
            storeBuf = next;
        }
    } while (count < maxCount && endPtr);

    return count;
}

/**
 * parseEnumAndNumbers
 * parses an enumeration type or a list of enumeration types or tries to convert string to a number
 * it stores the data in the member buffer mMetadataCache that is of size mMetadataCacheSize.
 *
 * \param src: string to be parsed
 * \param tagInfo: structure with the description of the static metadata
  * \param metadataCacheSize:the upper limited size of the dest buffer
 * \param metadataCache: the dest buffer to store the medatada after persed
 * \return number of elements parsed
 */
int CameraProfiles::parseEnumAndNumbers(const char *src,
                                        const metadata_tag_t *tagInfo,
                                        int metadataCacheSize,
                                        int64_t* metadataCache)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    int count = 0;
    int maxCount = metadataCacheSize / camera_metadata_type_size[tagInfo->type];
    char *endPtr = nullptr;

    /**
     * pointer to the metadata cache buffer
     */
    void *storeBuf = metadataCache;
    void *next;

    do {
        endPtr = (char *) strchr(src, ',');
        if (endPtr)
            *endPtr = 0;

        count += convertEnum(storeBuf, src,tagInfo->type,
                             tagInfo->enumTable, tagInfo->tableLength, &next);
        /* Try to convert value to number */
        if (count == 0) {
            long int *number = reinterpret_cast<long int*>(storeBuf);
            *number = strtol(src, &endPtr, 10);
            if (*number == LONG_MAX || *number == LONG_MIN)
                LOGW("You might have invalid value in the camera profiles: %s", src);
            count++;
        }

        if (endPtr) {
            src = endPtr + 1;
            storeBuf = next;
        }
    } while (count < maxCount && endPtr);

    return count;
}

/**
 * parseData
 * parses a generic array type. It stores the data in the member buffer
 * mMetadataCache that is of size mMetadataCacheSize.
 *
 * \param src: string to be parsed
 * \param tagInfo: structure with the description of the static metadata
 * \param metadataCacheSize:the upper limited size of the dest buffer
 * \param metadataCache: the dest buffer to store the medatada after persed
 * \return number of elements parsed
 */
int CameraProfiles::parseData(const char *src,
                              const metadata_tag_t *tagInfo,
                              int metadataCacheSize,
                              int64_t* metadataCache)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    int index = 0;
    int maxIndex = metadataCacheSize/sizeof(double); // worst case

    char *endPtr = nullptr;
    union {
        uint8_t *u8;
        int32_t *i32;
        int64_t *i64;
        float *f;
        double *d;
    } data;
    data.u8 = (uint8_t *)metadataCache;

    do {
        switch (tagInfo->type) {
        case TYPE_BYTE:
            data.u8[index] = (char)strtol(src, &endPtr, 10);
            LOG2("    - %d -", data.u8[index]);
            break;
        case TYPE_INT32:
        case TYPE_RATIONAL:
            data.i32[index] = strtol(src, &endPtr, 10);
            LOG2("    - %d -", data.i32[index]);
            break;
        case TYPE_INT64:
            data.i64[index] = strtol(src, &endPtr, 10);
            LOG2("    - %" PRId64 " -", data.i64[index]);
            break;
        case TYPE_FLOAT:
            data.f[index] = strtof(src, &endPtr);
            LOG2("    - %8.3f -", data.f[index]);
            break;
        case TYPE_DOUBLE:
            data.d[index] = strtof(src, &endPtr);
            LOG2("    - %8.3f -", data.d[index]);
            break;
        }
        index++;
        if (endPtr != nullptr) {
            if (*endPtr == ',' || *endPtr == 'x')
                src = endPtr + 1;
            else if (*endPtr == ')')
                src = endPtr + 3;
            else if (*endPtr == 0)
                break;
        }
    } while (index < maxIndex);

    if (tagInfo->type == TYPE_RATIONAL) {
        if (index % 2) {
            LOGW("Invalid number of entries to define rational (%d) in tag %s. It should be even",
                index, tagInfo->name);
            // lets make it even
            index -= 1;
        }
        index = index / 2;
        // we divide by 2 because one rational is made of 2 ints
    }

    return index;
}

const char *CameraProfiles::skipWhiteSpace(const char *src)
{
    /* Skip whitespace. (space, tab, newline, vertical tab, feed, carriage return) */
    while( *src == '\n' || *src == '\t' || *src == ' ' || *src == '\v' || *src == '\r' || *src == '\f'  ) {
        src++;
    }
    return src;
}

/**
 * Parses the string with the supported stream configurations
 * a stream configuration is made of 3 elements
 * - Format
 * - Resolution
 * - Direction (input or output)
 * we parse the string in 3 steps
 * example of valid stream configuration is:
 * RAW16,4208x3120,INPUT
 * \param src: string to be parsed
 * \param tagInfo: descriptor of the static metadata. this is the entry from the
 * table defined in the autogenerated code
 * \param metadataCacheSize:the upper limited size of the dest buffer
 * \param metadataCache: the dest buffer to store the medatada after persed
 *
 * \return number of int32 entries to be stored (i.e. 4 per configuration found)
 */
int CameraProfiles::parseStreamConfig(const char *src,
        const metadata_tag_t *tagInfo,
        std::vector<MetaValueRefTable> refTables,
        int metadataCacheSize,
        int64_t* metadataCache)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

    int count = 0; // entry count
    int maxCount = metadataCacheSize/sizeof(int32_t);
    int ret;
    char *endPtr = nullptr;
    int parseStep = 1;
    int32_t *i32;

    const metadata_value_t *activeTable;
    int activeTableLen = 0;

    void *storeBuf = metadataCache;
    void *next;

    if (refTables.size() < 2) {
        LOGE("incomplete reference table :%zu", refTables.size());
        return count;
    }

    do {
        endPtr = (char *) strchr(src, ',');
        if (endPtr)
            *endPtr = 0;

        if (parseStep == 1) {
            activeTable = refTables[0].table;
            activeTableLen = refTables[0].tableSize;
        } else if (parseStep == 3) {
            activeTable = refTables[1].table;
            activeTableLen = refTables[1].tableSize;
        }

        if (parseStep == 1 || parseStep == 3) {
            ret = convertEnum(storeBuf, src, tagInfo->type, activeTable,
                              activeTableLen, &next);
            if (ret == 1) {
                count++;
                storeBuf = next;
            } else {
                LOGE("Malformed enum in stream configuration %s", src);
                goto parseError;
            }

        } else {  // Step 2: Parse the resolution
            i32 = reinterpret_cast<int32_t*>(storeBuf);
            i32[0] = strtol(src, &endPtr, 10);
            if (endPtr == nullptr || *endPtr != 'x') {
                LOGE("Malformed resolution in stream configuration");
                goto parseError;
            }
            src = endPtr + 1;
            i32[1] = strtol(src, &endPtr, 10);
            storeBuf = reinterpret_cast<void*>(&i32[2]);
            count += 2;
            LOG1("  - %dx%d -", i32[0], i32[1]);
        }

        if (endPtr) {
            src = endPtr + 1;
            src = skipWhiteSpace(src);
            parseStep++;
            /* parsing steps go from 1 to 3 */
            if (parseStep == 4) {
                parseStep = 1;
                LOG1("Stream Configuration found");
            }
        } else {
            break;
        }
    } while (count < maxCount);

    if (endPtr != nullptr) {
        LOGW("Stream configuration stream too long for parser");
    }
    /**
     * Total number of entries per stream configuration is 4
     * - one for the format
     * - two for the resolution
     * - one for the direction
     * The total entry count should be multiple of 4
     */
    if (count % 4) {
        LOGE("Malformed string for stream configuration. ignoring last %d entries", count % 4);
        count -= count % 4;
    }
    return count;

parseError:
    LOGE("Error parsing stream configuration ");
    return 0;
}

/**
 * parseAvailableKeys
 * This method is used to parse the following two static metadata tags:
 * android.request.availableRequestKeys
 * android.request.availableResultKeys
 *
 * It uses the auto-generated table metadataNames to look for all the non
 * static tags.
 */
int CameraProfiles::parseAvailableKeys(const char *src,
                                       const metadata_tag_t *tagInfo,
                                       int metadataCacheSize,
                                       int64_t* metadataCache)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    int count = 0; // entry count
    int maxCount = metadataCacheSize/camera_metadata_type_size[tagInfo->type];
    size_t tableSize = ELEMENT(metadataNames);
    size_t blanks, tokenSize;
    const char *token;
    const char *cleanToken;
    const char delim = ',';
    int32_t* storeBuf = (int32_t*)metadataCache;
    std::vector<std::string> tokens;
    getTokens(src, delim, tokens);

    for (size_t i = 0; i < tokens.size(); i++) {
        token = tokens.at(i).c_str();
        /* ignore any spaces in front of the string */
        blanks = strspn(token," ");
        cleanToken = token + blanks;
        /**
         * Parse the token without blanks.
         * TODO: Add support for simple wildcard to allow things like
         * android.request.*
         */
        tokenSize = STRLEN_S(cleanToken);
        for (unsigned int i = 0; i< tableSize; i++) {
            if (strncmp(cleanToken, metadataNames[i].name, tokenSize) == 0) {
                storeBuf[count] = metadataNames[i].value;
                count++;
            }
        }
        if (count >= maxCount) {
            LOGW("Too many keys found (%d)- ignoring the rest", count);
            /* if this happens then we should increase the size of the mMetadataCache */
            break;
        }
    }
    return count;
}

/**
 * Parses the string with the avaialble input-output formats map
 * a format map is made of 3 elements
 * - Input Format
 * - Number output formats it can be converted in to
 * - List of the output formats.
 * we parse the string in 3 steps
 * example of valid input-output formats map is:
 * RAW_OPAQUE,3,BLOB,IMPLEMENTATION_DEFINED,YCbCr_420_888
 *
 * \param src: string to be parsed
 * \param tagInfo: descriptor of the static metadata. this is the entry from the
 * table defined in the autogenerated code
 * \param metadataCacheSize:the upper limit size of the dest buffer
 * \param metadataCache: the dest buffer to store the medatada after persed
 *
 * \return number of int32 entries to be stored
 */
int CameraProfiles::parseAvailableInputOutputFormatsMap(const char *src,
        const metadata_tag_t *tagInfo,
        std::vector<MetaValueRefTable> refTables,
        int metadataCacheSize,
        int64_t* metadataCache)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

    int count = 0; // entry count
    int maxCount = metadataCacheSize/camera_metadata_type_size[tagInfo->type];
    int ret;
    char *endPtr = nullptr;
    int parseStep = 1;
    int32_t *i32;
    int numOutputFormats = 0;

    const metadata_value_t *activeTable;
    int activeTableLen = 0;

    void *storeBuf = metadataCache;
    void *next;

    if (refTables.size() < 1) {
        LOGE("incomplete reference table :%zu", refTables.size());
        return count;
    }

    do {
        endPtr = (char *) strchr(src, ',');
        if (endPtr)
            *endPtr = 0;

        if (parseStep == 1) { // Step 1 parse the input format
            if (STRLEN_S(src) == 0) break;
            // detect empty string. It means we are done, so get out of the loop
            activeTable = refTables[0].table;
            activeTableLen = refTables[0].tableSize;
            ret = convertEnum(storeBuf, src, tagInfo->type, activeTable,
                              activeTableLen, &next);
            if (ret == 1) {
                count++;
                storeBuf = next;
            } else {
                LOGE("Malformed enum in format map %s", src);
                break;
            }
        } else if (parseStep == 2) { // Step 2: Parse the num of output formats
            i32 = reinterpret_cast<int32_t*>(storeBuf);
            i32[0] = strtol(src, &endPtr, 10);
            numOutputFormats = i32[0];
            count += 1;
            storeBuf = reinterpret_cast<void*>(&i32[1]);
            LOGD("Num of output formats = %d", i32[0]);

        } else { // Step3 parse the output formats
            activeTable = refTables[0].table;
            activeTableLen =  refTables[0].tableSize;
            for (int i = 0; i < numOutputFormats; i++) {
                ret = convertEnum(storeBuf, src, tagInfo->type, activeTable,
                                  activeTableLen, &next);
                if (ret == 1) {
                    count += 1;
                    if (endPtr == nullptr) return count;
                    src = endPtr + 1;
                    storeBuf = next;
                } else {
                    LOGE("Malformed enum in format map %s", src);
                    break;
                }
                if (i < numOutputFormats - 1) {
                    endPtr = (char *) strchr(src, ',');
                    if (endPtr)
                        *endPtr = 0;
                }
            }
        }

        if (endPtr) {
            src = endPtr + 1;
            src = skipWhiteSpace(src);
            parseStep++;
            /* parsing steps go from 1 to 3 */
            if (parseStep == 4) {
                parseStep = 1;
            }
        }
    } while (count < maxCount && endPtr);

    if (endPtr != nullptr) {
        LOGW("Formats Map string too long for parser");
    }

    return count;
}

int CameraProfiles::parseSizes(const char *src,
                               const metadata_tag_t *tagInfo,
                               int metadataCacheSize,
                               int64_t* metadataCache)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    int entriesFound = 0;

    entriesFound = parseData(src, tagInfo, metadataCacheSize, metadataCache);

    if (entriesFound % 2) {
        LOGE("Odd number of entries (%d), resolutions should have an even number of entries",
            entriesFound);
        entriesFound -= 1; //make it even Ignore the last one
    }
    return entriesFound;
}

int CameraProfiles::parseImageFormats(const char *src,
                                      const metadata_tag_t *tagInfo,
                                      int metadataCacheSize,
                                      int64_t *metadataCache)
{
    /**
     * DEPRECATED in V 3.2: TODO: add warning and extra checks
     */
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    int entriesFound = 0;

    entriesFound = parseEnum(src, tagInfo, metadataCacheSize, metadataCache);

    return entriesFound;
}

int CameraProfiles::parseRectangle(const char *src,
                                   const metadata_tag_t *tagInfo,
                                   int metadataCacheSize,
                                   int64_t *metadataCache)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    int entriesFound = 0;
    entriesFound = parseData(src, tagInfo, metadataCacheSize, metadataCache);

    if (entriesFound % 4) {
        LOGE("incorrect number of entries (%d), rectangles have 4 values",
                    entriesFound);
        entriesFound -= entriesFound % 4; //round to multiple of 4
    }

    return entriesFound;
}

int CameraProfiles::parseBlackLevelPattern(const char *src,
                                           const metadata_tag_t *tagInfo,
                                           int metadataCacheSize,
                                           int64_t *metadataCache)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    int entriesFound = 0;
    entriesFound = parseData(src, tagInfo, metadataCacheSize, metadataCache);
    if (entriesFound % 4) {
        LOGE("incorrect number of entries (%d), black level pattern have 4 values",
                    entriesFound);
        entriesFound -= entriesFound % 4; //round to multiple of 4
    }
    return entriesFound;
}

int CameraProfiles::parseStreamConfigDuration(const char *src,
        const metadata_tag_t *tagInfo,
        std::vector<MetaValueRefTable> refTables,
        int metadataCacheSize,
        int64_t *metadataCache)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    int count = 0; // entry count
    int maxCount = metadataCacheSize/camera_metadata_type_size[tagInfo->type];
    int ret;
    char *endPtr = nullptr;
    int parseStep = 1;
    int64_t *i64;

    const metadata_value_t *activeTable;
    int activeTableLen = 0;

    void *storeBuf = metadataCache;
    void *next;

    if (refTables.size() < 1) {
        LOGE("incomplete reference table :%zu", refTables.size());
        return count;
    }

    do {
        endPtr = (char *) strchr(src, ',');
        if (endPtr)
            *endPtr = 0;

        if (parseStep == 1) { // Step 1 parse the format
            if (STRLEN_S(src) == 0) break;
            // detect empty string. It means we are done, so get out of the loop
            activeTable = refTables[0].table;
            activeTableLen = refTables[0].tableSize;
            ret = convertEnum(storeBuf, src, tagInfo->type, activeTable,
                              activeTableLen, &next);
            if (ret == 1) {
                count++;
                storeBuf = next;
            } else {
                LOGE("Malformed enum in stream configuration duration %s", src);
                break;
            }

        } else if (parseStep == 2) { // Step 2: Parse the resolution
            i64 = reinterpret_cast<int64_t*>(storeBuf);
            i64[0] = strtol(src, &endPtr, 10);
            if (endPtr == nullptr || *endPtr != 'x') {
                LOGE("Malformed resolution in stream duration configuration");
                break;
            }
            src = endPtr + 1;
            i64[1] = strtol(src, &endPtr, 10);
            storeBuf = reinterpret_cast<void*>(&i64[2]);
            count += 2;
            LOG1("  - %" PRId64 "x%" PRId64 " -", i64[0], i64[1]);

        } else { // Step3 parse the duration

            i64 = reinterpret_cast<int64_t*>(storeBuf);
            if (endPtr)
                i64[0] = strtol(src, &endPtr, 10);
            else
                i64[0] = strtol(src, nullptr, 10); // Do not update endPtr
            storeBuf = reinterpret_cast<void*>(&i64[1]);
            count += 1;
            LOG1("  - %" PRId64 " ns -", i64[0]);
        }

        if (endPtr) {
            src = endPtr + 1;
            src = skipWhiteSpace(src);
            parseStep++;
            /* parsing steps go from 1 to 3 */
            if (parseStep == 4) {
                parseStep = 1;
                LOG1("Stream Configuration Duration found");
            }
        }
    } while (count < maxCount && endPtr);

    if (endPtr != nullptr) {
        LOGW("Stream configuration duration string too long for parser");
    }
    /**
     * Total number of entries per stream configuration is 4
     * - one for the format
     * - two for the resolution
     * - one for the duration
     * The total entry count should be multiple of 4
     */
    if (count % 4) {
        LOGE("Malformed string for stream config duration. ignoring last %d entries", count % 4);
        count -= count % 4;
    }
    return count;
}

/**
 *
 * Checks whether the sensor named in a profile is present in the list of
 * runtime detected sensors.
 * The result of this method helps to decide whether to use a particular profile
 * from the XML file.
 *
 * \param[in] detectedSensors vector with the description of the runtime
 *                            detected sensors.
 * \param[in] profileName name of the sensor present in the XML config file.
 * \param[in] cameraId camera Id for the sensor name in the XML.
 *
 * \return true if the sensor named in the profile is available in HW.
 */
bool CameraProfiles::isSensorPresent(std::vector<SensorDriverDescriptor> &detectedSensors,
                                     const char *profileName, int cameraId) const
{
    for (unsigned int i = 0; i < detectedSensors.size(); i++) {
        /*
         * Logic for legacy platforms with only 1-2 sensors.
         */
        if ((detectedSensors[i].mIspPort == PRIMARY && cameraId == 0) ||
            (detectedSensors[i].mIspPort == SECONDARY && cameraId == 1) ||
            (detectedSensors[i].mIspPort == UNKNOWN_PORT)) {
            if (detectedSensors[i].mSensorName == profileName) {
                LOG1("@%s: mUseEntry is true, mXmlSensorIndex = %d, name = %s",
                        __FUNCTION__, cameraId, profileName);
                return true;
            }
        }
        /*
         * Logic for new platforms that support more than 2 sensors.
         * To uniquely match an XML profile to a sensor present in HW we will
         * use 2 pieces of information:
         * - sensor name
         * - CSI port
         * Current implementation only uses sensor name. CSI port is needed in
         * cases where we have same sensor name in different ports.
         * TODO add this to XML part
         */
        if (detectedSensors[i].mSensorDevType == SENSOR_DEVICE_MC) {
           if (detectedSensors[i].mSensorName == profileName) {
               LOG1("@%s: mUseEntry is true, mXmlSensorIndex = %d, name = %s",
                       __FUNCTION__, cameraId, profileName);
               return true;
           }
       }
    }

    return false;
}

/**
 * This function will check which field that the parser parses to.
 *
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::checkField(CameraProfiles *profiles,
                                const char *name,
                                const char **atts)
{
    if (!strcmp(name, "Profiles")) {
        mXmlSensorIndex = atoi(atts[1]);
        if (mXmlSensorIndex > (MAX_CAMERAS - 1)) {
            LOGE("ERROR: bad camera id %d!", mXmlSensorIndex);
            return;
        }
        mCameraIdPool.push_back(mXmlSensorIndex);

        profiles->mUseEntry = false;
        int attIndex = 2;
        std::string sensorName;
        if (atts[attIndex]) {
            if (strcmp(atts[attIndex], "name") == 0) {
                sensorName = atts[attIndex + 1];
                LOG1("@%s: mXmlSensorIndex = %d, name = %s, mSensorNames.size():%zu",
                        __FUNCTION__, mXmlSensorIndex,
                        sensorName.c_str(), profiles->mSensorNames.size());

                profiles->mUseEntry = isSensorPresent(profiles->mSensorNames,
                                                      sensorName.c_str(),
                                                      mXmlSensorIndex);

                if (profiles->mUseEntry) {
                    mCameraIdToSensorName.insert(make_pair(mXmlSensorIndex, std::string(sensorName)));
                }
            } else {
                LOGE("unknown attribute atts[%d] = %s", attIndex, atts[attIndex]);
            }
        }

        if (profiles->mUseEntry
                && !sensorName.empty()
                && mXmlSensorIndex >= mStaticMeta.size()
                && mStaticMeta.size() < profiles->mSensorNames.size())
            addCamera(mXmlSensorIndex, sensorName);
    } else if (strcmp(name, "Android_metadata") == 0) {
        mCurrentDataField = FIELD_ANDROID_STATIC_METADATA;
        mItemsCount = -1;
    } else if (strcmp(name, "Hal_tuning_IPU3") == 0) {
        mCurrentDataField = FIELD_HAL_TUNING_IPU3;
        mItemsCount = -1;
    } else if (strcmp(name, "Sensor_info_IPU3") == 0) {
        mCurrentDataField = FIELD_SENSOR_INFO_IPU3;
        mItemsCount = -1;
    } else if (strcmp(name, "MediaCtl_elements_IPU3") == 0) {
        mCurrentDataField = FIELD_MEDIACTL_ELEMENTS_IPU3;
        mItemsCount = -1;
    } else if (strcmp(name, "Common") == 0) {
        mCurrentDataField = FIELD_COMMON;
        mItemsCount = -1;
    }

    LOG1("@%s: name:%s, field %d", __FUNCTION__, name, mCurrentDataField);
    return;
}

/**
 * This function will handle all the common related elements.
 *
 * It will be called in the function startElement
 *
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::handleCommon(const char *name, const char **atts)
{
    LOG1("@%s, name:%s, atts[0]:%s", __FUNCTION__, name, atts[0]);

    if (strcmp(atts[0], "value") != 0) {
        LOGE("name:%s, atts[0]:%s, xml format wrong", name, atts[0]);
        return;
    }

    CheckError(atts[1] == nullptr, VOID_VALUE, "atts[1] is nullptr");
    CheckError(mXmlSensorIndex >= MAX_CAMERAS, VOID_VALUE, "mXmlSensorIndex:%d >= MAX_CAMERAS", mXmlSensorIndex);

    if (strcmp(name, "faceAeEnabled") == 0) {
        if (strcmp("true", atts[1]) == 0) {
            mFaceAeEnabled[mXmlSensorIndex] = true;
        } else {
            mFaceAeEnabled[mXmlSensorIndex] = false;
        }
    } else if (strcmp(name, "faceEngineRunningInterval") == 0) {
        int val = atoi(atts[1]);
        mFaceEngineRunningInterval[mXmlSensorIndex] = val > 0 ? val : FACE_ENGINE_DEFAULT_RUNNING_INTERVAL;
    }
}

/**
 * This function will handle all the android static metadata related elements of sensor.
 *
 * It will be called in the function startElement
 * This method parses the input from the XML file, that can be manipulated.
 * So extra care is applied in the validation of strings
 *
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::handleAndroidStaticMetadata(const char *name, const char **atts)
{
    if (!validateStaticMetadata(name, atts))
        return;

    CheckError(mStaticMeta.empty(), VOID_VALUE, "Camera isn't added, unable to get the static metadata");
    camera_metadata_t *currentMeta = nullptr;
    currentMeta = mStaticMeta.at(mXmlSensorIndex);

    // Find tag
    const metadata_tag_t *tagInfo = findTagInfo(name, android_static_tags_table, STATIC_TAGS_TABLE_SIZE);
    if (tagInfo == nullptr)
        return;

    int count = 0;
    MetaValueRefTable mvrt;
    std::vector<MetaValueRefTable> refTables;
    LOG1("@%s: Parsing static tag %s: value %s", __FUNCTION__, tagInfo->name,  atts[1]);

    /**
     * Complex parsing types done manually (exceptions)
     * scene overrides uses different tables for each entry one from ae/awb/af
     * mode
     */
    if (tagInfo->value == ANDROID_SCALER_AVAILABLE_INPUT_OUTPUT_FORMATS_MAP) {
        mvrt.table = android_scaler_availableFormats_values;
        mvrt.tableSize = ELEMENT(android_scaler_availableFormats_values);
        refTables.push_back(mvrt);
        count = parseAvailableInputOutputFormatsMap(atts[1], tagInfo, refTables,
                METADATASIZE, mMetadataCache);
    } else if ((tagInfo->value == ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS) ||
               (tagInfo->value == ANDROID_REQUEST_AVAILABLE_RESULT_KEYS)) {
        count = parseAvailableKeys(atts[1], tagInfo, METADATASIZE, mMetadataCache);
    } else if (tagInfo->value == ANDROID_SYNC_MAX_LATENCY) {
        count = parseEnumAndNumbers(atts[1], tagInfo, METADATASIZE, mMetadataCache);
    } else { /* Parsing of generic types */
        if (tagInfo->arrayTypedef == STREAM_CONFIGURATION) {
            mvrt.table = android_scaler_availableFormats_values;
            mvrt.tableSize = ELEMENT(android_scaler_availableFormats_values);
            refTables.push_back(mvrt);
            mvrt.table = android_scaler_availableStreamConfigurations_values;
            mvrt.tableSize = ELEMENT(android_scaler_availableStreamConfigurations_values);
            refTables.push_back(mvrt);
            count = parseStreamConfig(atts[1], tagInfo, refTables, METADATASIZE, mMetadataCache);
        } else if (tagInfo->arrayTypedef == STREAM_CONFIGURATION_DURATION) {
            mvrt.table = android_scaler_availableFormats_values;
            mvrt.tableSize = ELEMENT(android_scaler_availableFormats_values);
            refTables.push_back(mvrt);
            count = parseStreamConfigDuration(atts[1], tagInfo, refTables, METADATASIZE,
                    mMetadataCache);
        } else {
            count = parseGenericTypes(atts[1], tagInfo, METADATASIZE, mMetadataCache);
        }
    }
    CheckError(count == 0, VOID_VALUE, "Error parsing static tag %s. ignoring", tagInfo->name);

    LOG1("@%s: writing static tag %s: count %d", __FUNCTION__, tagInfo->name, count);

    status_t res = add_camera_metadata_entry(currentMeta, tagInfo->value, mMetadataCache, count);
    CheckError(res != OK, VOID_VALUE, "call add_camera_metadata_entry fail for tag:%s",
        get_camera_metadata_tag_name(tagInfo->value));

    // save the key to mCharacteristicsKeys used to update the
    // REQUEST_AVAILABLE_CHARACTERISTICS_KEYS
    mCharacteristicsKeys[mXmlSensorIndex].push_back(tagInfo->value);
}

/**
 * This function will handle all the HAL parameters that are different
 * depending on the camera
 *
 * It will be called in the function startElement
 *
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::handleHALTuning(const char *name, const char **atts)
{
    LOG2("@%s", __FUNCTION__);

    if (strcmp(atts[0], "value") != 0) {
        LOGE("@%s, name:%s, atts[0]:%s, xml format wrong", __func__, name, atts[0]);
        return;
    }

    IPU3CameraCapInfo *info = static_cast<IPU3CameraCapInfo*>(mCaps[mXmlSensorIndex]);
    if (strcmp(name, "flipping") == 0) {
        info->mSensorFlipping = SENSOR_FLIP_OFF;
        if (strcmp(atts[0], "value") == 0 && strcmp(atts[1], "SENSOR_FLIP_H") == 0)
            info->mSensorFlipping |= SENSOR_FLIP_H;
        if (strcmp(atts[2], "value_v") == 0 && strcmp(atts[3], "SENSOR_FLIP_V") == 0)
            info->mSensorFlipping |= SENSOR_FLIP_V;
    } else if (strcmp(name, "supportIsoMap") == 0) {
        info->mSupportIsoMap = ((strcmp(atts[1], "true") == 0) ? true : false);
    } else if (strcmp(name, "graphSettingsFile") == 0) {
        info->mGraphSettingsFile = atts[1];
    }
}

/**
 * This function will handle all the parameters describing characteristic of
 * the sensor itself
 *
 * It will be called in the function startElement
 *
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::handleSensorInfo(const char *name, const char **atts)
{
    LOG2("@%s", __FUNCTION__);
    if (strcmp(atts[0], "value") != 0) {
        LOGE("@%s, name:%s, atts[0]:%s, xml format wrong", __func__, name, atts[0]);
        return;
    }
    IPU3CameraCapInfo *info = static_cast<IPU3CameraCapInfo*>(mCaps[mXmlSensorIndex]);

    if (strcmp(name, "sensorType") == 0) {
        info->mSensorType = ((strcmp(atts[1], "SENSOR_TYPE_RAW") == 0) ? SENSOR_TYPE_RAW : SENSOR_TYPE_SOC);
    }  else if (strcmp(name, "exposure.sync") == 0) {
        info->mExposureSync = ((strcmp(atts[1], "true") == 0) ? true : false);
    }  else if (strcmp(name, "sensor.digitalGain") == 0) {
        info->mDigiGainOnSensor = ((strcmp(atts[1], "true") == 0) ? true : false);
    } else if (strcmp(name, "gain.lag") == 0) {
        info->mGainLag = atoi(atts[1]);
    } else if (strcmp(name, "exposure.lag") == 0) {
        info->mExposureLag = atoi(atts[1]);
    } else if (strcmp(name, "gainExposure.compensation") == 0) {
        info->mGainExposureComp = ((strcmp(atts[1], "true") == 0) ? true : false);
    } else if (strcmp(name, "fov") == 0) {
        info->mFov[0] = atof(atts[1]);
        info->mFov[1] = atof(atts[3]);
    } else if (strcmp(name, "cITMaxMargin") == 0) {
        info->mCITMaxMargin = atoi(atts[1]);
    } else if (strcmp(name, "maxNvmDataSize") == 0) {
        info->mMaxNvmDataSize = atoi(atts[1]);
    } else if (strcmp(name, "nvmDirectory") == 0) {
        info->mNvmDirectory = atts[1];
    } else if (strcmp(name, "testPattern.bayerFormat") == 0) {
        info->mTestPatternBayerFormat = atts[1];
    } else if (strcmp(name, "sensor.testPatternMap") == 0) {
        int size = strlen(atts[1]);
        char src[size + 1];
        memset(src, 0, sizeof(src));
        MEMCPY_S(src, size, atts[1], size);
        char *savePtr, *tablePtr;
        int mode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;

        tablePtr = strtok_r(src, ",", &savePtr);
        while (tablePtr) {
            if (strcmp(tablePtr, "Off") == 0) {
                mode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
            } else if (strcmp(tablePtr, "ColorBars") == 0) {
                mode = ANDROID_SENSOR_TEST_PATTERN_MODE_COLOR_BARS;
            } else if (strcmp(tablePtr, "SolidColor") == 0) {
                mode = ANDROID_SENSOR_TEST_PATTERN_MODE_SOLID_COLOR;
            } else if (strcmp(tablePtr, "ColorBarsFadeToGray") == 0) {
                mode = ANDROID_SENSOR_TEST_PATTERN_MODE_COLOR_BARS_FADE_TO_GRAY;
            } else if (strcmp(tablePtr, "PN9") == 0) {
                mode = ANDROID_SENSOR_TEST_PATTERN_MODE_PN9;
            } else if (strcmp(tablePtr, "Custom1") == 0) {
                mode = ANDROID_SENSOR_TEST_PATTERN_MODE_CUSTOM1;
            } else {
                LOGE("Test pattern string %s is unknown, please check", tablePtr);
                return;
            }

            tablePtr = strtok_r(nullptr, ",", &savePtr);
            CheckError(tablePtr == nullptr, VOID_VALUE, "Driver test pattern is nullptr");

            info->mTestPatternMap[mode] = atoi(tablePtr);

            tablePtr = strtok_r(nullptr, ",", &savePtr);
        }
    } else if (strcmp(name, "ag.multiplier") == 0) {
        info->mAgMultiplier = atoi(atts[1]);
    } else if (strcmp(name, "ag.maxRatio") == 0) {
        info->mAgMaxRatio = atoi(atts[1]);
    } else if (strcmp(name, "ag.smiaParameters") == 0) {
        int size = strlen(atts[1]);
        char src[size + 1];
        memset(src, 0, sizeof(src));
        MEMCPY_S(src, size, atts[1], size);
        char *savePtr, *tablePtr;
        bool smiaError = false;

        tablePtr = strtok_r(src, ",", &savePtr);
        if (tablePtr)
            info->mSMIAm0 = atoi(tablePtr);
        else
            smiaError = true;
        tablePtr = strtok_r(nullptr, ",", &savePtr);
        if (tablePtr)
            info->mSMIAm1 = atoi(tablePtr);
        else
            smiaError = true;
        tablePtr = strtok_r(nullptr, ",", &savePtr);
        if (tablePtr)
            info->mSMIAc0 = atoi(tablePtr);
        else
            smiaError = true;
        tablePtr = strtok_r(nullptr, ",", &savePtr);
        if (tablePtr)
            info->mSMIAc1 = atoi(tablePtr);
        else
            smiaError = true;

        if (smiaError) {
            LOGE("@%s,SMIA parameters fails", __func__);
            info->mSMIAm0 = info->mSMIAm1 = info->mSMIAc0 = info->mSMIAc1 = 0;
            return;
        }
    }
}

/**
 * This function will handle all the camera pipe elements existing.
 * The goal is to enumerate all available camera media-ctl elements
 * from the camera profile file for later usage.
 *
 * It will be called in the function startElement
 *
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::handleMediaCtlElements(const char *name, const char **atts)
{
    LOG1("@%s, type:%s", __FUNCTION__, name);

    IPU3CameraCapInfo *info = static_cast<IPU3CameraCapInfo*>(mCaps[mXmlSensorIndex]);

    if (strcmp(name, "element") == 0) {
        MediaCtlElement currentElement;
        currentElement.isysNodeName = IMGU_NODE_NULL;
        while (*atts) {
            const XML_Char* attr_name = *atts++;
            const XML_Char* attr_value = *atts++;
            if (strcmp(attr_name, "name") == 0) {
                currentElement.name =
                    PlatformData::getCameraHWInfo()->getFullMediaCtlElementName(mElementNames, attr_value);
            } else if (strcmp(attr_name, "type") == 0) {
                currentElement.type = attr_value;
            } else if (strcmp(attr_name, "isysNodeName") == 0) {
                currentElement.isysNodeName = getIsysNodeNameAsValue(attr_value);
            } else {
                LOGW("Unhandled xml attribute in MediaCtl element (%s)", attr_name);
            }
        }
        if ((currentElement.type == "video_node")) {
            LOGE("ISYS node name is not set for \"%s\"", currentElement.name.c_str());
            return;
        }
        info->mMediaCtlElements.push_back(currentElement);
    }
}

/**
 * The function reads a binary file containing NVM data from sysfs. NVM data is
 * camera module calibration data which is written into the camera module in
 * production line, and at runtime read by the driver and written into sysfs.
 * The data is in the format in which the module manufacturer has provided it in.
 */
int CameraProfiles::readNvmDataFromDevice(int cameraId)
{
    LOG1("@%s", __FUNCTION__);

    IPU3CameraCapInfo *info = static_cast<IPU3CameraCapInfo*>(mCaps[cameraId]);
    CheckError(!info, UNKNOWN_ERROR, "Could not get Camera capability info");

    // if the nvm data have been read from the device, skip to read it again.
    if (info->isNvmDataValid()) {
        return OK;
    }

    std::string sensorName = info->getSensorName();
    std::string nvmDirectory = info->getNvmDirectory();
    if (nvmDirectory.length() == 0) {
        LOGW("NVM dirctory from config is null");
        return UNKNOWN_ERROR;
    }

    // check separator of path name
    std::string nvmDataPath(NVM_DATA_PATH);
    if (nvmDataPath.back() != '/')
        nvmDataPath.append("/");

    nvmDataPath.append(nvmDirectory);
    // check separator of path name
    if (nvmDataPath.back() != '/')
        nvmDataPath.append("/");

    nvmDataPath.append("eeprom");
    LOG1("NVM data for %s is located in %s", sensorName.c_str(), nvmDataPath.c_str());

    FILE *nvmFile = fopen(nvmDataPath.c_str(), "rb");
    CheckError(!nvmFile, UNKNOWN_ERROR, "Failed to open NVM file: %s", nvmDataPath.c_str());

    fseek(nvmFile, 0, SEEK_END);
    int maxNvmDataSize = info->getMaxNvmDataSize();
    unsigned int nvmDataSize = ftell(nvmFile);
    if (maxNvmDataSize > 0) {
        nvmDataSize = MIN(maxNvmDataSize, nvmDataSize);
    }
    fseek(nvmFile, 0, SEEK_SET);

    std::unique_ptr<char[]> nvmData(new char[nvmDataSize]);

    LOG1("NVM data size: %d bytes", nvmDataSize);
    int ret = fread(nvmData.get(), nvmDataSize, 1, nvmFile);
    fclose(nvmFile);
    CheckError(ret == 0, UNKNOWN_ERROR, "Cannot read nvm data");

    info->setNvmData(nvmData, nvmDataSize);

    return OK;
}

std::string CameraProfiles::getSensorMediaDevice()
{
  HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

  return getMediaDeviceByName(CIO2_MEDIA_DEVICE);
}

std::string CameraProfiles::getImguMediaDevice()
{
  HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

  return getMediaDeviceByName(IMGU_MEDIA_DEVICE);
}

std::string CameraProfiles::getMediaDeviceByName(std::string driverName)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    LOG1("@%s, Target name: %s", __FUNCTION__, driverName.c_str());
    const char *MEDIADEVICES = "media";
    const char *DEVICE_PATH = "/dev/";

    std::string mediaDevicePath;
    DIR *dir;
    dirent *dirEnt;

    std::vector<std::string> candidates;

    candidates.clear();
    if ((dir = opendir(DEVICE_PATH)) != nullptr) {
        while ((dirEnt = readdir(dir)) != nullptr) {
            std::string candidatePath = dirEnt->d_name;
            std::size_t pos = candidatePath.find(MEDIADEVICES);
            if (pos != std::string::npos) {
                LOGD("Found media device candidate: %s", candidatePath.c_str());
                std::string found_one = DEVICE_PATH;
                found_one += candidatePath;
                candidates.push_back(found_one);
            }
        }
        closedir(dir);
    } else {
        LOGW("Failed to open directory: %s", DEVICE_PATH);
    }

    status_t retVal = NO_ERROR;
    for (const auto &candidate : candidates) {
        MediaController controller(candidate.c_str());
        retVal = controller.init();

        // We may run into devices that this HAL won't use -> skip to next
        if (retVal == PERMISSION_DENIED) {
            LOGD("Not enough permissions to access %s.", candidate.c_str());
            continue;
        }

        media_device_info info;
        int ret = controller.getMediaDevInfo(info);
        if (ret != OK) {
            LOGE("Cannot get media device information.");
            return mediaDevicePath;
        }

        if (strncmp(info.driver, driverName.c_str(),
                    MIN(sizeof(info.driver),
                    driverName.size())) == 0) {
            LOGD("Found device that matches: %s", driverName.c_str());
            mediaDevicePath += candidate;
            break;
        }
    }

    return mediaDevicePath;
}

/*
 * Helper function for converting string to int for the
 * ISYS node name.
 */
int CameraProfiles::getIsysNodeNameAsValue(const char* isysNodeName)
{
    if (!strcmp(isysNodeName, "ISYS_NODE_RAW")) {
        return ISYS_NODE_RAW;
    } else {
        LOGE("Unknown ISYS node name (%s)", isysNodeName);
        return IMGU_NODE_NULL;
    }
}

bool CameraProfiles::validateStaticMetadata(const char *name, const char **atts)
{
    /**
     * string validation
     */
    size_t nameSize = strnlen(name, MAX_METADATA_NAME_LENTGTH);
    size_t attrNameSize = strnlen(atts[0], MAX_METADATA_ATTRIBUTE_NAME_LENTGTH);
    size_t attrValueSize = strnlen(atts[1], MAX_METADATA_ATTRIBUTE_VALUE_LENTGTH);
    if ((attrValueSize == MAX_METADATA_ATTRIBUTE_VALUE_LENTGTH) ||
        (attrNameSize == MAX_METADATA_ATTRIBUTE_NAME_LENTGTH) ||
        (nameSize == MAX_METADATA_NAME_LENTGTH)) {
        LOGW("Warning XML strings too long ignoring this tag %s", name);
        return false;
    }

    if ((strncmp(atts[0], "value", attrNameSize) != 0) ||
            (attrValueSize == 0)) {
        LOGE("Check atts failed! name: %s, atts[0]: \"%s\", atts[1]: \"%s\", the format of xml is wrong!", name, atts[0], atts[1]);
        return false;
    }

    return true;
}

const metadata_tag_t *CameraProfiles::findTagInfo(const char *name,
                                                  const metadata_tag_t *tagsTable,
                                                  unsigned int size)
{
    size_t nameSize = strnlen(name, MAX_METADATA_NAME_LENTGTH);
    unsigned int index = 0;
    const metadata_tag_t *tagInfo = nullptr;

    for (index = 0; index < size; index++) {
        if (!strncmp(name, tagsTable[index].name, nameSize)) {
            tagInfo = &tagsTable[index];
            break;
        }
    }
    if (index >= size) {
        LOGW("Parser does not support tag %s! - ignoring", name);
    }

    return tagInfo;
}

int CameraProfiles::parseGenericTypes(const char *src,
                                      const metadata_tag_t *tagInfo,
                                      int metadataCacheSize,
                                      int64_t *metadataCache)
{
    int count = 0;
    switch (tagInfo->arrayTypedef) {
            case BOOLEAN:
            case ENUM_LIST:
                count = parseEnum(src, tagInfo, metadataCacheSize, metadataCache);
                break;
            case RANGE_INT:
            case RANGE_LONG:
                count = parseData(src, tagInfo, metadataCacheSize, metadataCache);
                break;
            case SIZE_F:
            case SIZE:
                count = parseSizes(src, tagInfo, metadataCacheSize, metadataCache);
                break;
            case RECTANGLE:
                count = parseRectangle(src, tagInfo, metadataCacheSize, metadataCache);
                break;
            case IMAGE_FORMAT:
                count = parseImageFormats(src, tagInfo, metadataCacheSize, metadataCache);
                break;
            case BLACK_LEVEL_PATTERN:
                count = parseBlackLevelPattern(src, tagInfo, metadataCacheSize, metadataCache);
                break;
            case TYPEDEF_NONE: /* Single values*/
                if (tagInfo->enumTable) {
                    count = parseEnum(src, tagInfo, metadataCacheSize, metadataCache);
                } else {
                    count = parseData(src, tagInfo, metadataCacheSize, metadataCache);
                }
                break;
            default:
                LOGW("Unsupported typedef %s", tagInfo->name);
                break;
            }

    return count;
}

/**
 * the callback function of the libexpat for handling of one element start
 *
 * When it comes to the start of one element. This function will be called.
 *
 * \param userData: the pointer we set by the function XML_SetUserData.
 * \param name: the element's name.
 */
void CameraProfiles::startElement(void *userData, const char *name, const char **atts)
{
    CameraProfiles *profiles = (CameraProfiles *)userData;

    if (profiles->mCurrentDataField == FIELD_INVALID) {
        profiles->checkField(profiles, name, atts);
        return;
    }
    LOG2("@%s: name:%s, for sensor %d", __FUNCTION__, name, profiles->mXmlSensorIndex);

    profiles->mItemsCount++;

    if (!profiles->mUseEntry) {
        return;
    }

    switch (profiles->mCurrentDataField) {
        case FIELD_ANDROID_STATIC_METADATA:
            profiles->handleAndroidStaticMetadata(name, atts);
            break;
        case FIELD_HAL_TUNING_IPU3:
            profiles->handleHALTuning(name, atts);
            break;
        case FIELD_SENSOR_INFO_IPU3:
            profiles->handleSensorInfo(name, atts);
            break;
        case FIELD_MEDIACTL_ELEMENTS_IPU3:
            profiles->handleMediaCtlElements(name, atts);
            break;
        case FIELD_COMMON:
            profiles->handleCommon(name, atts);
            break;
        default:
            LOGE("line:%d, go to default handling", __LINE__);
            break;
    }
}

/**
 * the callback function of the libexpat for handling of one element end
 *
 * When it comes to the end of one element. This function will be called.
 *
 * \param userData: the pointer we set by the function XML_SetUserData.
 * \param name: the element's name.
 */
void CameraProfiles::endElement(void *userData, const char *name)
{
    CameraProfiles *profiles = (CameraProfiles *)userData;
    if (!strcmp(name, "Profiles")) {
        profiles->mCurrentDataField = FIELD_INVALID;
        if (profiles->mUseEntry)
            profiles->mProfileEnd[profiles->mXmlSensorIndex] = true;
    } else if (!strcmp(name, "Android_metadata")
             ||!strcmp(name, "Hal_tuning_IPU3")
             || !strcmp(name, "Sensor_info_IPU3")
             || !strcmp(name, "MediaCtl_elements_IPU3")) {
        profiles->mCurrentDataField = FIELD_INVALID;
        profiles->mItemsCount = -1;
    }
    return;
}

/**
 * Get camera configuration from xml file
 *
 * The function will read the xml configuration file firstly.
 * Then it will parse out the camera settings.
 * The camera setting is stored inside this CameraProfiles class.
 */
void CameraProfiles::getDataFromXmlFile()
{
    int done;
    void *pBuf = nullptr;
    FILE *fp = nullptr;
    LOG1("@%s", __FUNCTION__);
    camera_metadata_t *currentMeta = nullptr;
    status_t res;
    int tag = ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS;

    fp = ::fopen(DEFAULT_XML_FILE_NAME, "r");
    if (nullptr == fp) {
        LOGE("line:%d, fp is nullptr", __LINE__);
        return;
    }

    XML_Parser parser = ::XML_ParserCreate(nullptr);
    if (nullptr == parser) {
        LOGE("line:%d, parser is nullptr", __LINE__);
        goto exit;
    }

    PlatformData::getCameraHWInfo()->getMediaCtlElementNames(mElementNames);
    ::XML_SetUserData(parser, this);
    ::XML_SetElementHandler(parser, startElement, endElement);

    pBuf = ::operator new(BUFFERSIZE);
    mMetadataCache = new int64_t[METADATASIZE];

    do {
        int len = (int)::fread(pBuf, 1, BUFFERSIZE, fp);
        if (!len) {
            if (ferror(fp)) {
                clearerr(fp);
                goto exit;
            }
        }
        done = len < BUFFERSIZE;
        if (XML_Parse(parser, (const char *)pBuf, len, done) == XML_STATUS_ERROR) {
            LOGE("line:%d, XML_Parse error", __LINE__);
            goto exit;
        }
    } while (!done);

    if (mStaticMeta.size() > 0) {
        for (unsigned int i = 0; i < mStaticMeta.size(); i++) {
            currentMeta = mStaticMeta.at(i);
            if (currentMeta == nullptr) {
                LOGE("can't get the static metadata");
                goto exit;
            }
            // update REQUEST_AVAILABLE_CHARACTERISTICS_KEYS
            int *keys = mCharacteristicsKeys[i].data();
            res = MetadataHelper::updateMetadata(currentMeta, tag, keys, mCharacteristicsKeys[i].size());
            if (res != OK)
                LOGE("call add/update_camera_metadata_entry fail for request.availableCharacteristicsKeys");
        }
    }
exit:
    if (parser)
        ::XML_ParserFree(parser);

    ::operator delete(pBuf);

    if (mMetadataCache) {
        delete [] mMetadataCache;
        mMetadataCache = nullptr;
    }
    if (fp)
        ::fclose(fp);
}

/**
 * Read graph descriptor and settings from configuration files.
 *
 * The resulting graphs represend all possible graphs for given sensor, and
 * they are stored in capinfo structure.
 */
void CameraProfiles::getGraphConfigFromXmlFile()
{
    // Assuming that PSL section from profiles is already parsed, and number
    // of cameras is known.
    GraphConfigManager::addAndroidMap();
    for (size_t i = 0; i < mCaps.size(); ++i) {
        IPU3CameraCapInfo *info = static_cast<IPU3CameraCapInfo*>(mCaps[i]);
        if (info->mGCMNodes) {
            LOGE("Camera %zu Graph Config already initialized - BUG", i);
            continue;
        }

        std::string settingsPath = GRAPH_SETTINGS_FILE_PATH;
        const std::string &fileName = info->getGraphSettingsFile();

        if (fileName.empty()) {
            settingsPath += GraphConfigManager::DEFAULT_SETTINGS_FILE;
        } else {
            settingsPath += fileName;
        }
        LOGI("Using settings file %s for camera %zu", settingsPath.c_str(), i);

        info->mGCMNodes = GraphConfigManager::parse(
            GraphConfigManager::DEFAULT_DESCRIPTOR_FILE, settingsPath.c_str());

        if (!info->mGCMNodes) {
            LOGE("Could not read graph descriptor from file for camera %zu", i);
            continue;
        }
    }
}

void CameraProfiles::dumpStaticMetadataSection(int cameraId)
{
    LOGD("@%s", __FUNCTION__);
    if (mStaticMeta.size() > 0)
        MetadataHelper::dumpMetadata(mStaticMeta[cameraId]);
    else {
        LOGE("Camera isn't added, unable to get the static metadata");
    }
}

void CameraProfiles::dumpHalTuningSection(int cameraId)
{
    LOGD("@%s", __FUNCTION__);

    IPU3CameraCapInfo * info = static_cast<IPU3CameraCapInfo*>(mCaps[cameraId]);

    LOGD("element name: flipping, element value = %d", info->mSensorFlipping);
}

void CameraProfiles::dumpSensorInfoSection(int cameraId)
{
    LOGD("@%s", __FUNCTION__);

    IPU3CameraCapInfo * info = static_cast<IPU3CameraCapInfo*>(mCaps[cameraId]);

    LOGD("element name: sensorType, element value = %d", info->mSensorType);
    LOGD("element name: gain.lag, element value = %d", info->mGainLag);
    LOGD("element name: exposure.lag, element value = %d", info->mExposureLag);
    LOGD("element name: fov, element value = %f, %f", info->mFov[0], info->mFov[1]);
    LOGD("element name: testPattern.bayerFormat, element value = %s", info->mTestPatternBayerFormat.c_str());
}

void CameraProfiles::dumpMediaCtlElementsSection(int cameraId)
{
    LOGD("@%s", __FUNCTION__);

    unsigned int numidx;

    IPU3CameraCapInfo * info = static_cast<IPU3CameraCapInfo*>(mCaps[cameraId]);
    const MediaCtlElement *currentElement;
    for (numidx = 0; numidx < info->mMediaCtlElements.size(); numidx++) {
        currentElement = &info->mMediaCtlElements[numidx];
        LOGD("MediaCtl element name=%s ,type=%s, isysNodeName=%d"
             ,currentElement->name.c_str(),
             currentElement->type.c_str(),
             currentElement->isysNodeName);
    }
}

void CameraProfiles::dumpCommonSection()
{
    LOGD("@%s", __FUNCTION__);
    LOGD("element name: boardName, element value = %s", mCameraCommon->mBoardName.c_str());
    LOGD("element name: productName, element value = %s", mCameraCommon->productName());
    LOGD("element name: manufacturerName, element value = %s", mCameraCommon->manufacturerName());
    LOGD("element name: mSupportDualVideo, element value = %d", mCameraCommon-> mSupportDualVideo);
    LOGD("element name: supportExtendedMakernote, element value = %d", mCameraCommon->mSupportExtendedMakernote);
}

// To be modified when new elements or sections are added
// Use LOGD for traces to be visible
void CameraProfiles::dump()
{
    LOGD("===========================@%s======================", __FUNCTION__);
    for (unsigned int i = 0; i <= mXmlSensorIndex; i++) {
        dumpStaticMetadataSection(i);
    }

    for (unsigned int j = 0; j <= mCaps.size(); j++) {
        dumpHalTuningSection(j);
        dumpSensorInfoSection(j);
        dumpMediaCtlElementsSection(j);
    }

    dumpCommonSection();
    LOGD("===========================end======================");
}
} /* namespace intel */
} /* namespace cros */
