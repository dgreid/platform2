/*
 * Copyright (C) 2017-2020 Intel Corporation
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

#define LOG_TAG "MetadataConvert"

#include "MetadataConvert.h"

#include <cmath>
#include <sstream>
#include <unordered_map>

#include "Errors.h"
#include "HALv3Utils.h"
#include "ICamera.h"
#include "Utils.h"

namespace camera3 {

#define NSEC_PER_SEC 1000000000LLU
#define DEFAULT_FPS_RANGE_MIN 15
#define DEFAULT_FPS_RANGE_MAX 30

template <typename T>
struct ValuePair {
    int halValue;
    T androidValue;
};

template <typename T>
static int getAndroidValue(int halValue, const ValuePair<T>* table, int tableCount,
                           T* androidValue) {
    CheckError(!table, icamera::BAD_VALUE, "null table!");
    CheckError(!androidValue, icamera::BAD_VALUE, "androidValue is nullptr!");

    for (int i = 0; i < tableCount; i++) {
        if (halValue == table[i].halValue) {
            *androidValue = table[i].androidValue;
            return icamera::OK;
        }
    }
    return icamera::BAD_VALUE;
}

template <typename T>
static int getHalValue(T androidValue, const ValuePair<T>* table, int tableCount, int* halValue) {
    CheckError(!table, icamera::BAD_VALUE, "null table!");
    CheckError(!halValue, icamera::BAD_VALUE, "halValue is nullptr!");

    for (int i = 0; i < tableCount; i++) {
        if (androidValue == table[i].androidValue) {
            *halValue = table[i].halValue;
            return icamera::OK;
        }
    }
    return icamera::BAD_VALUE;
}

static const ValuePair<int32_t> testPatternTable[] = {
    {icamera::TEST_PATTERN_OFF, ANDROID_SENSOR_TEST_PATTERN_MODE_OFF},
    {icamera::SOLID_COLOR, ANDROID_SENSOR_TEST_PATTERN_MODE_SOLID_COLOR},
    {icamera::COLOR_BARS, ANDROID_SENSOR_TEST_PATTERN_MODE_COLOR_BARS},
    {icamera::COLOR_BARS_FADE_TO_GRAY, ANDROID_SENSOR_TEST_PATTERN_MODE_COLOR_BARS_FADE_TO_GRAY},
    {icamera::PN9, ANDROID_SENSOR_TEST_PATTERN_MODE_PN9},
    {icamera::TEST_PATTERN_CUSTOM1, ANDROID_SENSOR_TEST_PATTERN_MODE_CUSTOM1},
};

static const ValuePair<uint8_t> antibandingModesTable[] = {
    {icamera::ANTIBANDING_MODE_AUTO, ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO},
    {icamera::ANTIBANDING_MODE_50HZ, ANDROID_CONTROL_AE_ANTIBANDING_MODE_50HZ},
    {icamera::ANTIBANDING_MODE_60HZ, ANDROID_CONTROL_AE_ANTIBANDING_MODE_60HZ},
    {icamera::ANTIBANDING_MODE_OFF, ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF},
};

static const ValuePair<uint8_t> aeModesTable[] = {
    {icamera::AE_MODE_AUTO, ANDROID_CONTROL_AE_MODE_ON},
    {icamera::AE_MODE_MANUAL, ANDROID_CONTROL_AE_MODE_OFF},
};

static const ValuePair<uint8_t> awbModesTable[] = {
    {icamera::AWB_MODE_AUTO, ANDROID_CONTROL_AWB_MODE_AUTO},
    {icamera::AWB_MODE_INCANDESCENT, ANDROID_CONTROL_AWB_MODE_INCANDESCENT},
    {icamera::AWB_MODE_FLUORESCENT, ANDROID_CONTROL_AWB_MODE_FLUORESCENT},
    {icamera::AWB_MODE_DAYLIGHT, ANDROID_CONTROL_AWB_MODE_DAYLIGHT},
    {icamera::AWB_MODE_FULL_OVERCAST, ANDROID_CONTROL_AWB_MODE_TWILIGHT},
    {icamera::AWB_MODE_PARTLY_OVERCAST, ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT},
    {icamera::AWB_MODE_MANUAL_COLOR_TRANSFORM, ANDROID_CONTROL_AWB_MODE_OFF},
};

static const ValuePair<uint8_t> afModesTable[] = {
    {icamera::AF_MODE_OFF, ANDROID_CONTROL_AF_MODE_OFF},
    {icamera::AF_MODE_AUTO, ANDROID_CONTROL_AF_MODE_AUTO},
    {icamera::AF_MODE_MACRO, ANDROID_CONTROL_AF_MODE_MACRO},
    {icamera::AF_MODE_CONTINUOUS_VIDEO, ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO},
    {icamera::AF_MODE_CONTINUOUS_PICTURE, ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE},
};

static const ValuePair<uint8_t> afTriggerTable[] = {
    {icamera::AF_TRIGGER_START, ANDROID_CONTROL_AF_TRIGGER_START},
    {icamera::AF_TRIGGER_CANCEL, ANDROID_CONTROL_AF_TRIGGER_CANCEL},
    {icamera::AF_TRIGGER_IDLE, ANDROID_CONTROL_AF_TRIGGER_IDLE},
};

static const ValuePair<uint8_t> dvsModesTable[] = {
    {icamera::VIDEO_STABILIZATION_MODE_OFF, ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF},
    {icamera::VIDEO_STABILIZATION_MODE_ON, ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_ON},
};

static const ValuePair<uint8_t> effectModesTable[] = {
    {icamera::CAM_EFFECT_NONE, ANDROID_CONTROL_EFFECT_MODE_OFF},
    {icamera::CAM_EFFECT_MONO, ANDROID_CONTROL_EFFECT_MODE_MONO},
    {icamera::CAM_EFFECT_SEPIA, ANDROID_CONTROL_EFFECT_MODE_SEPIA},
    {icamera::CAM_EFFECT_NEGATIVE, ANDROID_CONTROL_EFFECT_MODE_NEGATIVE},
};

static const ValuePair<uint8_t> shadingModeTable[] = {
    {icamera::SHADING_MODE_OFF, ANDROID_SHADING_MODE_OFF},
    {icamera::SHADING_MODE_FAST, ANDROID_SHADING_MODE_FAST},
    {icamera::SHADING_MODE_HIGH_QUALITY, ANDROID_SHADING_MODE_HIGH_QUALITY},
};

static const ValuePair<uint8_t> lensShadingMapModeTable[] = {
    {icamera::LENS_SHADING_MAP_MODE_OFF, ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF},
    {icamera::LENS_SHADING_MAP_MODE_ON, ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_ON},
};

static const ValuePair<uint8_t> tonemapModesTable[] = {
    {icamera::TONEMAP_MODE_CONTRAST_CURVE, ANDROID_TONEMAP_MODE_CONTRAST_CURVE},
    {icamera::TONEMAP_MODE_FAST, ANDROID_TONEMAP_MODE_FAST},
    {icamera::TONEMAP_MODE_HIGH_QUALITY, ANDROID_TONEMAP_MODE_HIGH_QUALITY},
    {icamera::TONEMAP_MODE_GAMMA_VALUE, ANDROID_TONEMAP_MODE_GAMMA_VALUE},
    {icamera::TONEMAP_MODE_PRESET_CURVE, ANDROID_TONEMAP_MODE_PRESET_CURVE},
};

static const ValuePair<uint8_t> tonemapPresetCurvesTable[] = {
    {icamera::TONEMAP_PRESET_CURVE_SRGB, ANDROID_TONEMAP_PRESET_CURVE_SRGB},
    {icamera::TONEMAP_PRESET_CURVE_REC709, ANDROID_TONEMAP_PRESET_CURVE_REC709},
};

static bool isValueSupported(uint8_t mode, const icamera::CameraMetadata* caps, uint32_t tag) {
    icamera_metadata_ro_entry entry = caps->find(tag);
    if (entry.count > 0) {
        for (size_t i = 0; i < entry.count; i++) {
            if (mode == entry.data.u8[i]) return true;
        }
    }
    return false;
}

MetadataConvert::MetadataConvert(int cameraId) : mCameraId(cameraId) {
    LOG1("@%s, mCameraId %d", __func__, mCameraId);
}

MetadataConvert::~MetadataConvert() {
    LOG1("@%s", __func__);
}

int MetadataConvert::constructDefaultMetadata(int cameraId, android::CameraMetadata* settings) {
    LOG1("@%s", __func__);
    const icamera::CameraMetadata* meta = StaticCapability::getInstance(cameraId)->getCapability();

    // CAMERA_CONTROL_MAX_REGIONS: [AE, AWB, AF]
    uint32_t tag = CAMERA_CONTROL_MAX_REGIONS;
    icamera_metadata_ro_entry roEntry = meta->find(tag);
    int32_t maxRegionAf = 0, maxRegionAe = 0;
    if (roEntry.count == 3) {
        maxRegionAe = roEntry.data.i32[0];
        maxRegionAf = roEntry.data.i32[2];
    }

    // AE, AF region (AWB region is not supported)
    int meteringRegion[5] = {0, 0, 0, 0, 0};
    if (maxRegionAe) {
        settings->update(ANDROID_CONTROL_AE_REGIONS, meteringRegion, 5);
    }
    if (maxRegionAf) {
        settings->update(ANDROID_CONTROL_AF_REGIONS, meteringRegion, 5);
    }

    // Control AE, AF, AWB
    uint8_t mode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    settings->update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &mode, 1);
    int32_t ev = 0;
    settings->update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &ev, 1);
    uint8_t lock = ANDROID_CONTROL_AE_LOCK_OFF;
    settings->update(ANDROID_CONTROL_AE_LOCK, &lock, 1);
    mode = ANDROID_CONTROL_AE_MODE_ON;
    settings->update(ANDROID_CONTROL_AE_MODE, &mode, 1);
    mode = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
    settings->update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &mode, 1);
    mode = ANDROID_CONTROL_AE_STATE_INACTIVE;
    settings->update(ANDROID_CONTROL_AE_STATE, &mode, 1);

    mode = ANDROID_CONTROL_AF_MODE_OFF;
    settings->update(ANDROID_CONTROL_AF_MODE, &mode, 1);
    mode = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    settings->update(ANDROID_CONTROL_AF_TRIGGER, &mode, 1);
    mode = ANDROID_CONTROL_AF_STATE_INACTIVE;
    settings->update(ANDROID_CONTROL_AF_STATE, &mode, 1);

    lock = ANDROID_CONTROL_AWB_LOCK_OFF;
    settings->update(ANDROID_CONTROL_AWB_LOCK, &lock, 1);
    mode = ANDROID_CONTROL_AWB_MODE_AUTO;
    settings->update(ANDROID_CONTROL_AWB_MODE, &mode, 1);
    mode = ANDROID_CONTROL_AWB_STATE_INACTIVE;
    settings->update(ANDROID_CONTROL_AWB_STATE, &mode, 1);

    // Control others
    mode = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
    settings->update(ANDROID_CONTROL_CAPTURE_INTENT, &mode, 1);
    mode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    settings->update(ANDROID_CONTROL_EFFECT_MODE, &mode, 1);
    mode = ANDROID_CONTROL_MODE_AUTO;
    settings->update(ANDROID_CONTROL_MODE, &mode, 1);
    mode = ANDROID_CONTROL_SCENE_MODE_DISABLED;
    settings->update(ANDROID_CONTROL_SCENE_MODE, &mode, 1);
    mode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
    settings->update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &mode, 1);

    // Flash
    mode = ANDROID_FLASH_MODE_OFF;
    settings->update(ANDROID_FLASH_MODE, &mode, 1);

    mode = ANDROID_FLASH_STATE_UNAVAILABLE;
    tag = CAMERA_FLASH_INFO_AVAILABLE;
    roEntry = meta->find(tag);
    if (roEntry.count == 1 && roEntry.data.u8[0] == CAMERA_FLASH_INFO_AVAILABLE_TRUE) {
        mode = ANDROID_FLASH_STATE_READY;
    }
    settings->update(ANDROID_FLASH_STATE, &mode, 1);

    // Black level
    lock = ANDROID_BLACK_LEVEL_LOCK_OFF;
    settings->update(ANDROID_BLACK_LEVEL_LOCK, &lock, 1);

    // Lens
    camera_metadata_entry entry = settings->find(ANDROID_LENS_INFO_AVAILABLE_APERTURES);
    if (entry.count >= 1) {
        settings->update(ANDROID_LENS_APERTURE, &entry.data.f[0], 1);
    }
    entry = settings->find(CAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS);
    if (entry.count >= 1) {
        settings->update(ANDROID_LENS_FOCAL_LENGTH, &entry.data.f[0], 1);
    }
    entry = settings->find(CAMERA_LENS_INFO_MINIMUM_FOCUS_DISTANCE);
    if (entry.count == 1) {
        settings->update(ANDROID_LENS_FOCUS_DISTANCE, &entry.data.f[0], 1);
    }

    float filterDensity = 0.0f;
    settings->update(ANDROID_LENS_FILTER_DENSITY, &filterDensity, 1);
    mode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    settings->update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &mode, 1);

    int64_t value_i64 = 0;
    settings->update(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW, &value_i64, 1);

    // Sync
    int64_t frameNumber = ANDROID_SYNC_FRAME_NUMBER_UNKNOWN;
    settings->update(ANDROID_SYNC_FRAME_NUMBER, &frameNumber, 1);

    // Request
    mode = ANDROID_REQUEST_TYPE_CAPTURE;
    settings->update(ANDROID_REQUEST_TYPE, &mode, 1);
    mode = ANDROID_REQUEST_METADATA_MODE_NONE;
    settings->update(ANDROID_REQUEST_METADATA_MODE, &mode, 1);

    // Scale
    int32_t region[] = {0, 0, 0, 0};
    settings->update(ANDROID_SCALER_CROP_REGION, region, 4);

    // Statistics
    mode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    settings->update(ANDROID_STATISTICS_FACE_DETECT_MODE, &mode, 1);
    mode = ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
    settings->update(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE, &mode, 1);
    mode = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
    settings->update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &mode, 1);
    mode = ANDROID_STATISTICS_SCENE_FLICKER_NONE;
    settings->update(ANDROID_STATISTICS_SCENE_FLICKER, &mode, 1);

    // Tonemap
    mode = ANDROID_TONEMAP_MODE_FAST;
    settings->update(ANDROID_TONEMAP_MODE, &mode, 1);

    // Sensor
    value_i64 = 0;
    settings->update(ANDROID_SENSOR_EXPOSURE_TIME, &value_i64, 1);
    int32_t sensitivity = 0;
    settings->update(ANDROID_SENSOR_SENSITIVITY, &sensitivity, 1);
    int64_t frameDuration = 33000000;
    settings->update(ANDROID_SENSOR_FRAME_DURATION, &frameDuration, 1);
    int32_t testPattern = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
    settings->update(ANDROID_SENSOR_TEST_PATTERN_MODE, &testPattern, 1);

    // Jpeg
    uint8_t quality = 95;
    settings->update(ANDROID_JPEG_QUALITY, &quality, 1);
    quality = 90;
    settings->update(ANDROID_JPEG_THUMBNAIL_QUALITY, &quality, 1);

    entry = settings->find(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES);
    int32_t thumbSize[] = {0, 0};
    if (entry.count >= 4) {
        thumbSize[0] = entry.data.i32[2];
        thumbSize[1] = entry.data.i32[3];
    } else {
        LOGE("Thumbnail size should have more than 2 resolutions");
    }
    settings->update(ANDROID_JPEG_THUMBNAIL_SIZE, thumbSize, 2);

    entry = settings->find(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES);
    if (entry.count > 0) {
        mode = entry.data.u8[0];
        for (uint32_t i = 0; i < entry.count; i++) {
            if (entry.data.u8[i] == ANDROID_TONEMAP_MODE_HIGH_QUALITY) {
                mode = ANDROID_TONEMAP_MODE_HIGH_QUALITY;
                break;
            }
        }
        settings->update(ANDROID_TONEMAP_MODE, &mode, 1);
    }

    // Color correction
    mode = ANDROID_COLOR_CORRECTION_MODE_FAST;
    settings->update(ANDROID_COLOR_CORRECTION_MODE, &mode, 1);

    float colorTransform[9] = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    camera_metadata_rational_t transformMatrix[9];
    for (int i = 0; i < 9; i++) {
        transformMatrix[i].numerator = colorTransform[i];
        transformMatrix[i].denominator = 1.0;
    }
    settings->update(ANDROID_COLOR_CORRECTION_TRANSFORM, transformMatrix, 9);

    float colorGains[4] = {1.0, 1.0, 1.0, 1.0};
    settings->update(ANDROID_COLOR_CORRECTION_GAINS, colorGains, 4);

    mode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
    settings->update(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &mode, 1);

    return icamera::OK;
}

int MetadataConvert::updateDefaultRequestSettings(int32_t cameraId, int type,
                                                  android::CameraMetadata* settings) {
    const icamera::CameraMetadata* caps = StaticCapability::getInstance(cameraId)->getCapability();

    uint8_t intent =
        (type == CAMERA3_TEMPLATE_PREVIEW)          ? ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW
        : (type == CAMERA3_TEMPLATE_STILL_CAPTURE)  ? ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE
        : (type == CAMERA3_TEMPLATE_VIDEO_RECORD)   ? ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD
        : (type == CAMERA3_TEMPLATE_VIDEO_SNAPSHOT) ? ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT
        : (type == CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG)
            ? ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG
        : (type == CAMERA3_TEMPLATE_MANUAL) ? ANDROID_CONTROL_CAPTURE_INTENT_MANUAL
                                            : ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
    settings->update(ANDROID_CONTROL_CAPTURE_INTENT, &intent, 1);

    uint8_t ctrlMode = ANDROID_CONTROL_MODE_AUTO;
    uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    uint8_t afMode = ANDROID_CONTROL_AF_MODE_OFF;
    uint8_t edgeMode = ANDROID_EDGE_MODE_FAST;
    uint8_t nrMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
    uint8_t sdMode = ANDROID_SHADING_MODE_FAST;
    uint8_t hpMode = ANDROID_HOT_PIXEL_MODE_FAST;

    switch (type) {
        case CAMERA3_TEMPLATE_MANUAL:
            ctrlMode = ANDROID_CONTROL_MODE_OFF;
            aeMode = ANDROID_CONTROL_AE_MODE_OFF;
            awbMode = ANDROID_CONTROL_AWB_MODE_OFF;
            afMode = ANDROID_CONTROL_AF_MODE_OFF;
            break;
        case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
            afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
            edgeMode = ANDROID_EDGE_MODE_ZERO_SHUTTER_LAG;
            nrMode = ANDROID_NOISE_REDUCTION_MODE_ZERO_SHUTTER_LAG;
            sdMode = ANDROID_SHADING_MODE_HIGH_QUALITY;
            hpMode = ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY;
            break;
        case CAMERA3_TEMPLATE_STILL_CAPTURE:
            afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
            edgeMode = ANDROID_EDGE_MODE_HIGH_QUALITY;
            nrMode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY;
            sdMode = ANDROID_SHADING_MODE_HIGH_QUALITY;
            hpMode = ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY;
            break;
        case CAMERA3_TEMPLATE_PREVIEW:
            afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
            break;
        case CAMERA3_TEMPLATE_VIDEO_RECORD:
        case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
            afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
            break;
        default:
            break;
    }

    // Check if modes are supported or not.
    if (!isValueSupported(afMode, caps, CAMERA_AF_AVAILABLE_MODES))
        afMode = ANDROID_CONTROL_AF_MODE_OFF;
    if (!isValueSupported(edgeMode, caps, CAMERA_EDGE_AVAILABLE_EDGE_MODES))
        edgeMode = ANDROID_EDGE_MODE_FAST;
    if (!isValueSupported(nrMode, caps, CAMERA_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES))
        nrMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
    if (!isValueSupported(sdMode, caps, CAMERA_SHADING_AVAILABLE_MODES))
        sdMode = ANDROID_SHADING_MODE_FAST;
    if (!isValueSupported(hpMode, caps, CAMERA_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES))
        hpMode = ANDROID_HOT_PIXEL_MODE_FAST;

    LOG2("%s, type %d, ctrlMode %d, aeMode %d, awbMode %d, afMode %d", __func__, type, ctrlMode,
         aeMode, awbMode, afMode);
    settings->update(ANDROID_CONTROL_MODE, &ctrlMode, 1);
    settings->update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
    settings->update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);
    settings->update(ANDROID_CONTROL_AF_MODE, &afMode, 1);
    settings->update(ANDROID_EDGE_MODE, &edgeMode, 1);
    settings->update(ANDROID_NOISE_REDUCTION_MODE, &nrMode, 1);
    settings->update(ANDROID_SHADING_MODE, &sdMode, 1);
    settings->update(ANDROID_HOT_PIXEL_MODE, &hpMode, 1);

    uint32_t tag = ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES;
    camera_metadata_entry fpsRangesEntry = settings->find(tag);
    if ((fpsRangesEntry.count >= 2) && (fpsRangesEntry.count % 2 == 0)) {
        int32_t delta = INT32_MAX;
        int fpsRange[] = {DEFAULT_FPS_RANGE_MIN, DEFAULT_FPS_RANGE_MAX};

        // choose closest (DEFAULT_FPS_RANGE_MIN, DEFAULT_FPS_RANGE_MAX) range
        for (size_t i = 0; i < fpsRangesEntry.count; i += 2) {
            int32_t diff = abs(fpsRangesEntry.data.i32[i] - DEFAULT_FPS_RANGE_MIN) +
                           abs(fpsRangesEntry.data.i32[i + 1] - DEFAULT_FPS_RANGE_MAX);

            if (delta > diff) {
                fpsRange[0] = fpsRangesEntry.data.i32[i];
                fpsRange[1] = fpsRangesEntry.data.i32[i + 1];
                delta = diff;
            }
        }

        if (type == CAMERA3_TEMPLATE_VIDEO_RECORD) {
            // Stable range requried for video recording
            fpsRange[0] = fpsRange[1];
        }
        settings->update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, &fpsRange[0], 2);
    } else {
        LOGW("The fpsRanges isn't correct, please check the profiles file");
    }

    return icamera::OK;
}

int MetadataConvert::requestMetadataToHALMetadata(const android::CameraMetadata& settings,
                                                  icamera::Parameters* parameter,
                                                  bool forceConvert) {
    LOG1("@%s: settings entry count %d", __func__, settings.entryCount());
    CheckError(parameter == nullptr, icamera::BAD_VALUE, "%s, parameter is nullptr", __func__);

    uint8_t intent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
    camera_metadata_ro_entry entry = settings.find(ANDROID_CONTROL_CAPTURE_INTENT);
    if (entry.count == 1) {
        intent = entry.data.u8[0];
    }

    // ANDROID_COLOR_CORRECTION
    convertColorCorrectionMetadata(settings, parameter);

    // ANDROID_CONTROL
    convertControlMetadata(settings, parameter);

    // ANDROID_DEMOSAIC
    // ANDROID_EDGE
    convertEdgeMetadata(settings, parameter, intent);

    // ANDROID_HOT_PIXEL
    // ANDROID_NOISE_REDUCTION
    convertNoiseReductionMetadata(settings, parameter, intent);

    // ANDROID_SHADING
    // ANDROID_TONEMAP
    convertTonemapMetadata(settings, parameter);
    // ANDROID_BLACK_LEVEL
    convertAdvancedFeatureMetadata(settings, parameter);
    // ANDROID_FLASH

    // ANDROID_JPEG
    convertJpegMetadata(settings, parameter);

    // ANDROID_LENS
    convertLensMetadata(settings, parameter);

    // ANDROID_SCALER

    // ANDROID_SENSOR
    convertSensorMetadata(settings, parameter, forceConvert);

    // ANDROID_STATISTICS

    // ANDROID_LED

    // ANDROID_REPROCESS

    return icamera::OK;
}

int MetadataConvert::HALMetadataToRequestMetadata(const icamera::Parameters& parameter,
                                                  android::CameraMetadata* settings, int cameraId) {
    LOG1("@%s", __func__);

    CheckError(settings == nullptr, icamera::BAD_VALUE, "%s, settings is nullptr", __func__);

    // ANDROID_COLOR_CORRECTION
    convertColorCorrectionParameter(parameter, settings);

    // ANDROID_CONTROL
    convertControlParameter(parameter, settings);

    // ANDROID_FLASH
    // ANDROID_FLASH_INFO
    convertFlashParameter(parameter, settings);

    // ANDROID_JPEG

    // ANDROID_LENS
    // ANDROID_LENS_INFO
    convertLensParameter(parameter, settings);

    // ANDROID_QUIRKS

    // ANDROID_REQUEST
    convertRequestParameter(parameter, settings, cameraId);

    // ANDROID_SCALER

    // ANDROID_SENSOR
    // ANDROID_SENSOR_INFO
    convertSensorParameter(parameter, settings);

    // ANDROID_STATISTICS
    // ANDROID_STATISTICS_INFO
    convertStatisticsParameter(parameter, settings);

    // ANDROID_TONEMAP
    convertTonemapParameter(parameter, settings);

    // ANDROID_DEMOSAIC, ANDROID_EDGE, ANDROID_HOT_PIXEL, ANDROID_NOISE_REDUCTION
    // ANDROID_SHADING, ANDROID_INFO, ANDROID_BLACK_LEVEL, ANDROID_SYNC
    convertAdvancedFeatureParameter(parameter, settings);

    // ANDROID_LED

    // ANDROID_REPROCESS

    // ANDROID_DEPTH

    LOG1("@%s: convert entry count %d", __func__, settings->entryCount());
    return icamera::OK;
}

int MetadataConvert::HALCapabilityToStaticMetadata(const icamera::Parameters& parameter,
                                                   android::CameraMetadata* settings) {
    LOG1("@%s", __func__);

    CheckError(settings == nullptr, icamera::BAD_VALUE, "%s, settings is nullptr", __func__);

    // ANDROID_COLOR_CORRECTION
    uint8_t aberrationAvailable = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
    settings->update(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES, &aberrationAvailable, 1);

    // ANDROID_CONTROL
    fillControlStaticMetadata(parameter, settings);

    // ANDROID_FLASH
    // ANDROID_FLASH_INFO
    uint8_t flashInfoAvailable = ANDROID_FLASH_INFO_AVAILABLE_FALSE;
    settings->update(ANDROID_FLASH_INFO_AVAILABLE, &flashInfoAvailable, 1);

    // ANDROID_JPEG
    fillJpegStaticMetadata(parameter, settings);

    // ANDROID_LENS
    // ANDROID_LENS_INFO
    fillLensStaticMetadata(parameter, settings);

    // ANDROID_QUIRKS

    // ANDROID_REQUEST
    fillRequestStaticMetadata(parameter, settings);

    // ANDROID_SCALER
    fillScalerStaticMetadata(parameter, settings);

    // ANDROID_SENSOR
    // ANDROID_SENSOR_INFO
    fillSensorStaticMetadata(parameter, settings);

    // ANDROID_STATISTICS
    // ANDROID_STATISTICS_INFO
    fillStatisticsStaticMetadata(parameter, settings);

    // ANDROID_TONEMAP
    fillTonemapStaticMetadata(parameter, settings);

    // ANDROID_LED
    uint8_t availLeds = ANDROID_LED_AVAILABLE_LEDS_TRANSMIT;
    settings->update(ANDROID_LED_AVAILABLE_LEDS, &availLeds, 1);

    // ANDROID_REPROCESS

    // ANDROID_DEPTH

    fillAdvancedFeatureStaticMetadata(parameter, settings);

    return icamera::OK;
}

void MetadataConvert::convertFaceDetectionMetadata(
    const icamera::CVFaceDetectionAbstractResult& fdResult, android::CameraMetadata* settings) {
    CheckError(settings == nullptr, VOID_VALUE, "@%s, settings is nullptr", __func__);

    camera_metadata_entry entry = settings->find(ANDROID_STATISTICS_FACE_DETECT_MODE);
    CheckError(entry.count == 0, VOID_VALUE, "@%s: No face detection mode setting", __func__);

    const uint8_t mode = entry.data.u8[0];
    if (mode == ANDROID_STATISTICS_FACE_DETECT_MODE_OFF) {
        LOG2("%s: Face mode is off", __func__);
        int faceIds[1] = {0};
        settings->update(ANDROID_STATISTICS_FACE_IDS, faceIds, 1);
        return;
    } else if (mode == ANDROID_STATISTICS_FACE_DETECT_MODE_SIMPLE) {
        LOG2("%s: Face mode is simple", __func__);
        // Face id is expected to be -1 for SIMPLE mode
        if (fdResult.faceNum > 0) {
            int faceIds[MAX_FACES_DETECTABLE];
            for (int i = 0; i < fdResult.faceNum; i++) {
                faceIds[i] = -1;
            }
            settings->update(ANDROID_STATISTICS_FACE_IDS, faceIds, fdResult.faceNum);
        } else {
            int faceIds[1] = {-1};
            settings->update(ANDROID_STATISTICS_FACE_IDS, faceIds, 1);
        }
    } else if (mode == ANDROID_STATISTICS_FACE_DETECT_MODE_FULL) {
        LOG2("%s: Face mode is full", __func__);
        /*
         * from the spec:
         * SIMPLE mode must fill in android.statistics.faceRectangles and
         * android.statistics.faceScores. FULL mode must also fill in
         * android.statistics.faceIds, and android.statistics.faceLandmarks.
         */
        settings->update(ANDROID_STATISTICS_FACE_IDS, fdResult.faceIds, fdResult.faceNum);
        settings->update(ANDROID_STATISTICS_FACE_LANDMARKS, fdResult.faceLandmarks,
                         LM_SIZE * fdResult.faceNum);
    }

    settings->update(ANDROID_STATISTICS_FACE_RECTANGLES, fdResult.faceRect,
                     RECT_SIZE * fdResult.faceNum);
    settings->update(ANDROID_STATISTICS_FACE_SCORES, fdResult.faceScores, fdResult.faceNum);
}

int MetadataConvert::convertColorCorrectionMetadata(const android::CameraMetadata& settings,
                                                    icamera::Parameters* parameter) {
    uint32_t tag = ANDROID_COLOR_CORRECTION_TRANSFORM;
    camera_metadata_ro_entry entry = settings.find(tag);
    if (entry.count == 9) {
        icamera::camera_color_transform_t transform;
        for (size_t i = 0; i < entry.count; i++) {
            transform.color_transform[i / 3][i % 3] =
                static_cast<float>(entry.data.r[i].numerator) / entry.data.r[i].denominator;
        }
        parameter->setColorTransform(transform);
    }

    tag = ANDROID_COLOR_CORRECTION_GAINS;
    entry = settings.find(tag);
    if (entry.count == 4) {
        icamera::camera_color_gains_t gains;
        for (size_t i = 0; i < entry.count; i++) {
            gains.color_gains_rggb[i] = entry.data.f[i];
        }
        parameter->setColorGains(gains);
    }

    return icamera::OK;
}

int MetadataConvert::convertColorCorrectionParameter(const icamera::Parameters& parameter,
                                                     android::CameraMetadata* settings) {
    icamera::camera_color_transform_t transform;
    if (parameter.getColorTransform(transform) == 0) {
        camera_metadata_rational_t matrix[9];
        for (int i = 0; i < 9; i++) {
            matrix[i].numerator = round(transform.color_transform[i / 3][i % 3] * 1000);
            matrix[i].denominator = 1000;
        }
        settings->update(ANDROID_COLOR_CORRECTION_TRANSFORM, &matrix[0], 9);
    }

    icamera::camera_color_gains_t colorGains;
    if (parameter.getColorGains(colorGains) == 0) {
        settings->update(ANDROID_COLOR_CORRECTION_GAINS, &colorGains.color_gains_rggb[0], 4);
    }

    uint8_t aberrationMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
    settings->update(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &aberrationMode, 1);

    return icamera::OK;
}

int MetadataConvert::convertControlMetadata(const android::CameraMetadata& settings,
                                            icamera::Parameters* parameter) {
    int ret = icamera::OK;
    int mode = 0;
    uint32_t tag = ANDROID_CONTROL_AE_MODE;
    camera_metadata_ro_entry entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], aeModesTable, ARRAY_SIZE(aeModesTable), &mode);
        if (ret == icamera::OK) {
            parameter->setAeMode((icamera::camera_ae_mode_t)mode);
        }
    }

    tag = ANDROID_STATISTICS_FACE_DETECT_MODE;
    entry = settings.find(tag);
    uint8_t fdValue = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    if ((entry.count == 1) && (entry.data.u8[0] == ANDROID_STATISTICS_FACE_DETECT_MODE_OFF)) {
        int faceIds[1] = {0};
        parameter->setFaceIds(faceIds, 1);
    } else {
        fdValue = entry.data.u8[0];
    }
    parameter->setFaceDetectMode(fdValue);

    tag = ANDROID_CONTROL_AE_LOCK;
    entry = settings.find(tag);
    if (entry.count == 1) {
        bool aeLock = (entry.data.u8[0] == ANDROID_CONTROL_AE_LOCK_ON);
        parameter->setAeLock(aeLock);
    }

    tag = ANDROID_CONTROL_AE_REGIONS;
    entry = settings.find(tag);
    icamera::camera_window_list_t windows;
    if (entry.count > 0) {
        if (convertToHalWindow(entry.data.i32, entry.count, &windows) == 0) {
            parameter->setAeRegions(windows);
        }
    }

    tag = ANDROID_CONTROL_AE_TARGET_FPS_RANGE;
    entry = settings.find(tag);
    if (entry.count == 2) {
        icamera::camera_range_t range;
        range.min = entry.data.i32[0];
        range.max = entry.data.i32[1];
        parameter->setFpsRange(range);
    }

    tag = ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION;
    entry = settings.find(tag);
    if (entry.count == 1) {
        parameter->setAeCompensation(entry.data.i32[0]);
    }

    tag = ANDROID_CONTROL_AE_ANTIBANDING_MODE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], antibandingModesTable,
                          ARRAY_SIZE(antibandingModesTable), &mode);
        if (ret == icamera::OK) {
            parameter->setAntiBandingMode((icamera::camera_antibanding_mode_t)mode);
        }
    }

    tag = ANDROID_CONTROL_AF_MODE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], afModesTable, ARRAY_SIZE(afModesTable), &mode);
        if (ret == icamera::OK) {
            parameter->setAfMode((icamera::camera_af_mode_t)mode);
        }
    }

    tag = ANDROID_CONTROL_AF_TRIGGER;
    entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], afTriggerTable, ARRAY_SIZE(afTriggerTable), &mode);
        if (ret == icamera::OK) {
            parameter->setAfTrigger((icamera::camera_af_trigger_t)mode);
        }
    }

    tag = ANDROID_CONTROL_AF_REGIONS;
    entry = settings.find(tag);
    windows.clear();
    if (entry.count > 0) {
        if (convertToHalWindow(entry.data.i32, entry.count, &windows) == 0) {
            parameter->setAfRegions(windows);
        }
    }

    tag = ANDROID_CONTROL_AWB_MODE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], awbModesTable, ARRAY_SIZE(awbModesTable), &mode);
        if (ret == icamera::OK) {
            parameter->setAwbMode((icamera::camera_awb_mode_t)mode);
        }
    }

    tag = ANDROID_CONTROL_AWB_LOCK;
    entry = settings.find(tag);
    if (entry.count == 1) {
        bool awbLock = (entry.data.u8[0] == ANDROID_CONTROL_AWB_LOCK_ON);
        parameter->setAwbLock(awbLock);
    }

    tag = ANDROID_CONTROL_AWB_REGIONS;
    entry = settings.find(tag);
    windows.clear();
    if (entry.count > 0) {
        if (convertToHalWindow(entry.data.i32, entry.count, &windows) == 0) {
            parameter->setAwbRegions(windows);
        }
    }

    tag = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], dvsModesTable, ARRAY_SIZE(dvsModesTable), &mode);
        if (ret == icamera::OK) {
            parameter->setVideoStabilizationMode((icamera::camera_video_stabilization_mode_t)mode);
        }
    }

    tag = ANDROID_CONTROL_EFFECT_MODE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], effectModesTable, ARRAY_SIZE(effectModesTable), &mode);
        if (ret == icamera::OK) {
            parameter->setImageEffect((icamera::camera_effect_mode_t)mode);
        }
    }

    tag = ANDROID_CONTROL_CAPTURE_INTENT;
    entry = settings.find(tag);
    if (entry.count == 1) {
        parameter->setCaptureIntent(entry.data.u8[0]);
    }

    return icamera::OK;
}

int MetadataConvert::convertControlParameter(const icamera::Parameters& parameter,
                                             android::CameraMetadata* settings) {
    int ret = icamera::OK;
    uint8_t mode = 0;
    icamera::camera_ae_mode_t aeMode;
    if (parameter.getAeMode(aeMode) == 0) {
        ret = getAndroidValue(aeMode, aeModesTable, ARRAY_SIZE(aeModesTable), &mode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_CONTROL_AE_MODE, &mode, 1);
        }
    }

    bool aeLock;
    if (parameter.getAeLock(aeLock) == 0) {
        uint8_t mode = aeLock ? ANDROID_CONTROL_AE_LOCK_ON : ANDROID_CONTROL_AE_LOCK_OFF;
        settings->update(ANDROID_CONTROL_AE_LOCK, &mode, 1);
    }

    icamera::camera_window_list_t windows;
    parameter.getAeRegions(windows);
    int count = windows.size() * 5;
    if (count > 0) {
        int regions[count];
        count = convertToMetadataRegion(windows, windows.size() * 5, regions);
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AE_REGIONS, &regions[0], count);
        }
    }

    icamera::camera_range_t range;
    if (parameter.getFpsRange(range) == 0) {
        int fps[2] = {(int)range.min, (int)range.max};
        settings->update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, &fps[0], 2);
    }

    int ev;
    if (parameter.getAeCompensation(ev) == 0) {
        settings->update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &ev, 1);
    }

    icamera::camera_antibanding_mode_t antiMode;
    if (parameter.getAntiBandingMode(antiMode) == 0) {
        ret = getAndroidValue(antiMode, antibandingModesTable, ARRAY_SIZE(antibandingModesTable),
                              &mode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &mode, 1);
        }
    }

    icamera::camera_af_mode_t afMode;
    if (parameter.getAfMode(afMode) == 0) {
        ret = getAndroidValue(afMode, afModesTable, ARRAY_SIZE(afModesTable), &mode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_CONTROL_AF_MODE, &mode, 1);
        }
    }

    windows.clear();
    parameter.getAfRegions(windows);
    count = windows.size() * 5;
    if (count > 0) {
        int regions[count];
        count = convertToMetadataRegion(windows, windows.size() * 5, regions);
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AF_REGIONS, &regions[0], count);
        }
    }

    icamera::camera_awb_mode_t awbMode;
    if (parameter.getAwbMode(awbMode) == 0) {
        ret = getAndroidValue(awbMode, awbModesTable, ARRAY_SIZE(awbModesTable), &mode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_CONTROL_AWB_MODE, &mode, 1);
        }
    }

    bool awbLock;
    if (parameter.getAwbLock(awbLock) == 0) {
        uint8_t mode = awbLock ? ANDROID_CONTROL_AWB_LOCK_ON : ANDROID_CONTROL_AWB_LOCK_OFF;
        settings->update(ANDROID_CONTROL_AWB_LOCK, &mode, 1);
    }

    windows.clear();
    parameter.getAwbRegions(windows);
    count = windows.size() * 5;
    if (count > 0) {
        int regions[count];
        count = convertToMetadataRegion(windows, windows.size() * 5, regions);
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AWB_REGIONS, &regions[0], count);
        }
    }

    icamera::camera_video_stabilization_mode_t dvsMode;
    if (parameter.getVideoStabilizationMode(dvsMode) == 0) {
        ret = getAndroidValue(dvsMode, dvsModesTable, ARRAY_SIZE(dvsModesTable), &mode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &mode, 1);
        }
    }

    icamera::camera_effect_mode_t effectMode;
    if (parameter.getImageEffect(effectMode) == 0) {
        ret = getAndroidValue(effectMode, effectModesTable, ARRAY_SIZE(effectModesTable), &mode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_CONTROL_EFFECT_MODE, &mode, 1);
        }
    }

    return icamera::OK;
}

int MetadataConvert::convertJpegMetadata(const android::CameraMetadata& settings,
                                         icamera::Parameters* parameter) {
    uint32_t tag = ANDROID_JPEG_GPS_COORDINATES;
    camera_metadata_ro_entry entry = settings.find(tag);
    if (entry.count == 3) {
        parameter->setJpegGpsCoordinates(entry.data.d);
    }

    tag = ANDROID_JPEG_GPS_PROCESSING_METHOD;
    entry = settings.find(tag);
    if (entry.count >= 1) {
        char data[entry.count + 1];
        MEMCPY_S(data, sizeof(data), entry.data.u8, entry.count);
        data[entry.count] = 0;
        parameter->setJpegGpsProcessingMethod(data);
    }

    tag = ANDROID_JPEG_GPS_TIMESTAMP;
    entry = settings.find(tag);
    if (entry.count == 1) {
        parameter->setJpegGpsTimeStamp(entry.data.i64[0]);
    }

    tag = ANDROID_JPEG_ORIENTATION;
    entry = settings.find(tag);
    if (entry.count == 1) {
        parameter->setJpegRotation(entry.data.i32[0]);
    }

    tag = ANDROID_JPEG_QUALITY;
    entry = settings.find(tag);
    if (entry.count == 1) {
        int quality = entry.data.u8[0];
        parameter->setJpegQuality(quality);
    }

    tag = ANDROID_JPEG_THUMBNAIL_QUALITY;
    entry = settings.find(tag);
    if (entry.count == 1) {
        int quality = entry.data.u8[0];
        parameter->setJpegThumbnailQuality(quality);
    }

    tag = ANDROID_JPEG_THUMBNAIL_SIZE;
    entry = settings.find(tag);
    if (entry.count == 2) {
        icamera::camera_resolution_t size;
        size.width = entry.data.i32[0];
        size.height = entry.data.i32[1];
        parameter->setJpegThumbnailSize(size);
    }

    return icamera::OK;
}

int MetadataConvert::convertEdgeMetadata(const android::CameraMetadata& settings,
                                         icamera::Parameters* parameter, int intent) {
    camera_metadata_ro_entry entry = settings.find(ANDROID_EDGE_MODE);
    if (entry.count != 1) return icamera::OK;

    int32_t mode = entry.data.u8[0];
    /* When intent is still capture, the edgeMode default value should be HQ. In other case,
       the edgeMode default value should be FAST. The default value corresponds to
       EDGE_MODE_LEVEL_2.
       In addition, we use the same level for OFF and ZSL.
    */
    icamera::camera_edge_mode_t edgeMode = icamera::EDGE_MODE_LEVEL_2;

    if ((mode == ANDROID_EDGE_MODE_OFF) ||
        (mode == ANDROID_EDGE_MODE_ZERO_SHUTTER_LAG)) {
        edgeMode = icamera::EDGE_MODE_LEVEL_4;
    } else if ((intent == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE) &&
        (mode == ANDROID_EDGE_MODE_FAST)) {
        edgeMode = icamera::EDGE_MODE_LEVEL_3;
    } else if ((intent != ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE) &&
       (mode == ANDROID_EDGE_MODE_HIGH_QUALITY)) {
        edgeMode = icamera::EDGE_MODE_LEVEL_1;
    }

    parameter->setEdgeMode(edgeMode);

    return icamera::OK;
}

int MetadataConvert::convertNoiseReductionMetadata(const android::CameraMetadata& settings,
                                                   icamera::Parameters* parameter, int intent) {
    camera_metadata_ro_entry entry = settings.find(ANDROID_NOISE_REDUCTION_MODE);
    if (entry.count != 1) return icamera::OK;

    uint8_t mode = entry.data.u8[0];
    /* When intent is still capture, the nrMode default value should be HQ. In other case,
       the nrMode default value should be FAST. The default value corresponds to
       NR_MODE_LEVEL_2.
       In addition, we use the same level for OFF and ZSL.
    */
    icamera::camera_nr_mode_t nrMode = icamera::NR_MODE_LEVEL_2;

    if ((mode == ANDROID_NOISE_REDUCTION_MODE_OFF) ||
        (mode == ANDROID_NOISE_REDUCTION_MODE_ZERO_SHUTTER_LAG)) {
        nrMode = icamera::NR_MODE_LEVEL_4;
    } else if ((intent == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE) &&
        (mode == ANDROID_NOISE_REDUCTION_MODE_FAST)) {
        nrMode = icamera::NR_MODE_LEVEL_3;
    } else if ((intent != ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE) &&
       (mode == ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY)) {
        nrMode = icamera::NR_MODE_LEVEL_1;
    }

    parameter->setNrMode(nrMode);

    return icamera::OK;
}

int MetadataConvert::convertTonemapMetadata(const android::CameraMetadata& settings,
                                            icamera::Parameters* parameter) {
    int ret = icamera::OK;

    camera_metadata_ro_entry entry = settings.find(ANDROID_TONEMAP_MODE);
    if (entry.count == 1) {
        int32_t mode = 0;
        ret =
            getHalValue(entry.data.u8[0], tonemapModesTable, ARRAY_SIZE(tonemapModesTable), &mode);
        if (ret == icamera::OK) {
            parameter->setTonemapMode((icamera::camera_tonemap_mode_t)mode);
        }
    }

    entry = settings.find(ANDROID_TONEMAP_PRESET_CURVE);
    if (entry.count == 1) {
        int32_t curve = 0;
        ret = getHalValue(entry.data.u8[0], tonemapPresetCurvesTable,
                          ARRAY_SIZE(tonemapPresetCurvesTable), &curve);
        if (ret == icamera::OK) {
            parameter->setTonemapPresetCurve((icamera::camera_tonemap_preset_curve_t)curve);
        }
    }

    entry = settings.find(ANDROID_TONEMAP_GAMMA);
    if (entry.count == 1) {
        parameter->setTonemapGamma(entry.data.f[0]);
    }

    icamera::camera_tonemap_curves_t curves;
    entry = settings.find(ANDROID_TONEMAP_CURVE_RED);
    curves.rSize = entry.count;
    curves.rCurve = entry.data.f;
    entry = settings.find(ANDROID_TONEMAP_CURVE_GREEN);
    curves.gSize = entry.count;
    curves.gCurve = entry.data.f;
    entry = settings.find(ANDROID_TONEMAP_CURVE_BLUE);
    curves.bSize = entry.count;
    curves.bCurve = entry.data.f;
    if (curves.rSize > 0 && curves.gSize > 0 && curves.bSize > 0) {
         parameter->setTonemapCurves(curves);
    }

    return icamera::OK;
}

int MetadataConvert::convertTonemapParameter(const icamera::Parameters& parameter,
                                             android::CameraMetadata* settings) {
    icamera::camera_tonemap_curves_t curves;
    if (parameter.getTonemapCurves(curves) == 0) {
        settings->update(ANDROID_TONEMAP_CURVE_RED, curves.rCurve, curves.rSize);
        settings->update(ANDROID_TONEMAP_CURVE_BLUE, curves.bCurve, curves.bSize);
        settings->update(ANDROID_TONEMAP_CURVE_GREEN, curves.gCurve, curves.gSize);
    }

    return icamera::OK;
}

int MetadataConvert::convertSensorMetadata(const android::CameraMetadata& settings,
                                           icamera::Parameters* parameter, bool forceConvert) {
    // get control ae mode
    uint8_t manualAeMode = ANDROID_CONTROL_AE_MODE_ON;
    uint32_t tag = ANDROID_CONTROL_AE_MODE;
    camera_metadata_ro_entry entry = settings.find(tag);
    if (entry.count == 1) {
        manualAeMode = entry.data.u8[0];
    }

    // get control mode
    uint8_t manualMode = ANDROID_CONTROL_MODE_AUTO;
    tag = ANDROID_CONTROL_MODE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        manualMode = entry.data.u8[0];
    }

    if (manualAeMode == ANDROID_CONTROL_AE_MODE_OFF || manualMode == ANDROID_CONTROL_MODE_OFF ||
        forceConvert) {
        // manual exposure control
        tag = ANDROID_SENSOR_EXPOSURE_TIME;
        entry = settings.find(tag);
        if (entry.count == 1) {
            parameter->setExposureTime(entry.data.i64[0] / 1000);  // ns -> us
        }

        // manual sensitivity control
        tag = ANDROID_SENSOR_SENSITIVITY;
        entry = settings.find(tag);
        if (entry.count == 1) {
            parameter->setSensitivityIso(entry.data.i32[0]);
        }

        // manual frame duration control
        int64_t maxFrameDuration = 0;
        entry = settings.find(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION);
        if (entry.count == 1) {
            maxFrameDuration = entry.data.i64[0];
            LOG2("@%s, maxFrameDuration:%ld ns", __func__, maxFrameDuration);
        }

        tag = ANDROID_SENSOR_FRAME_DURATION;
        entry = settings.find(tag);
        if (entry.count == 1) {
            int64_t frameDuration = entry.data.i64[0];
            LOG2("@%s, frameDuration:%ld ns", __func__, frameDuration);
            if (maxFrameDuration > 0 && frameDuration > maxFrameDuration) {
                frameDuration = maxFrameDuration;
            }

            if (frameDuration != 0) {
                float fps = NSEC_PER_SEC / frameDuration;
                parameter->setFrameRate(fps);
            }
        }
    } else {
        // Clear manual settings then AE algorithm works
        int64_t exposureTime = 0;
        parameter->setExposureTime(exposureTime);
        int32_t iso = 0;
        parameter->setSensitivityIso(iso);
        float fps = 0.0;
        parameter->setFrameRate(fps);
    }

    // Test Pattern Mode
    tag = ANDROID_SENSOR_TEST_PATTERN_MODE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        int halTestPatternMode = icamera::TEST_PATTERN_OFF;
        int ret = getHalValue(entry.data.i32[0], testPatternTable, ARRAY_SIZE(testPatternTable),
                              &halTestPatternMode);
        if (ret == icamera::OK) {
            parameter->setTestPatternMode(
                static_cast<icamera::camera_test_pattern_mode_t>(halTestPatternMode));
        }
    }

    return icamera::OK;
}

int MetadataConvert::convertRequestParameter(const icamera::Parameters& parameter,
                                             android::CameraMetadata* settings, int cameraId) {
    const icamera::CameraMetadata* meta = StaticCapability::getInstance(cameraId)->getCapability();

    uint32_t tag = CAMERA_REQUEST_PIPELINE_MAX_DEPTH;
    icamera_metadata_ro_entry entry = meta->find(tag);
    uint8_t depth = (entry.count == 1) ? *entry.data.u8 : 6;

    settings->update(ANDROID_REQUEST_PIPELINE_DEPTH, &depth, 1);

    return icamera::OK;
}

int MetadataConvert::convertSensorParameter(const icamera::Parameters& parameter,
                                            android::CameraMetadata* settings) {
    int64_t exposure;
    if (parameter.getExposureTime(exposure) == 0) {
        int64_t time = exposure * 1000;  // us -> ns
        settings->update(ANDROID_SENSOR_EXPOSURE_TIME, &time, 1);
    }

    int32_t iso;
    if (parameter.getSensitivityIso(iso) == 0) {
        settings->update(ANDROID_SENSOR_SENSITIVITY, &iso, 1);
    }

    icamera::camera_test_pattern_mode_t halTestPatternMode = icamera::TEST_PATTERN_OFF;
    if (parameter.getTestPatternMode(halTestPatternMode) == icamera::OK) {
        int32_t androidPatternMode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
        int ret = getAndroidValue(halTestPatternMode, testPatternTable,
                                  ARRAY_SIZE(testPatternTable), &androidPatternMode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_SENSOR_TEST_PATTERN_MODE, &androidPatternMode, 1);
        }
    }

    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    uint32_t tag = CAMERA_SENSOR_ROLLING_SHUTTER_SKEW;
    icamera_metadata_entry entry = meta.find(tag);
    if (entry.count == 1) {
        int64_t rollingShutter = entry.data.i64[0] * 1000;  // us -> ns
        settings->update(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW, &rollingShutter, entry.count);
    }

    tag = CAMERA_SENSOR_FRAME_DURATION;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_SENSOR_FRAME_DURATION, entry.data.i64, entry.count);
    }

    return icamera::OK;
}

int MetadataConvert::convertLensMetadata(const android::CameraMetadata& settings,
                                         icamera::Parameters* parameter) {
    uint32_t tag = ANDROID_LENS_FOCAL_LENGTH;
    camera_metadata_ro_entry entry = settings.find(tag);
    if (entry.count == 1) {
        parameter->setFocalLength(entry.data.f[0]);
    }

    tag = ANDROID_LENS_APERTURE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        parameter->setAperture(entry.data.f[0]);
    }

    tag = ANDROID_LENS_FOCUS_DISTANCE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        parameter->setFocusDistance(entry.data.f[0]);
    }

    return icamera::OK;
}

int MetadataConvert::convertLensParameter(const icamera::Parameters& parameter,
                                          android::CameraMetadata* settings) {
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    uint32_t tag = CAMERA_LENS_INFO_AVAILABLE_APERTURES;
    icamera_metadata_entry entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_LENS_APERTURE, entry.data.f, 1);
    }

    float focal = 0.0f;
    parameter.getFocalLength(focal);
    if (focal < EPSILON) {
        icamera_metadata_entry entry = meta.find(CAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS);
        if (entry.count >= 1) {
            focal = entry.data.f[0];
        }
    }
    settings->update(ANDROID_LENS_FOCAL_LENGTH, &focal, 1);

    float focusDistanceDiopters = 0.0;
    if (parameter.getFocusDistance(focusDistanceDiopters) == 0) {
        settings->update(ANDROID_LENS_FOCUS_DISTANCE, &focusDistanceDiopters, 1);
    }

    icamera::camera_range_t focusRange = {};
    if (parameter.getFocusRange(focusRange) == 0) {
        float range[] = {focusRange.min, focusRange.max};
        settings->update(ANDROID_LENS_FOCUS_RANGE, range, 2);
    }

    uint8_t mode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    settings->update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &mode, 1);
    float filterDensity = 0.0;
    settings->update(ANDROID_LENS_FILTER_DENSITY, &filterDensity, 1);

    return icamera::OK;
}

int MetadataConvert::convertStatisticsParameter(const icamera::Parameters& /*parameter*/,
                                                android::CameraMetadata* settings) {
    camera_metadata_entry entry = settings->find(ANDROID_STATISTICS_FACE_DETECT_MODE);
    if (entry.count == 1 && entry.data.u8[0] == ANDROID_STATISTICS_FACE_DETECT_MODE_OFF) {
        LOG2("%s: Face mode is off", __func__);
        int faceIds[1] = {0};
        settings->update(ANDROID_STATISTICS_FACE_IDS, faceIds, 1);
    }
    return icamera::OK;
}

int MetadataConvert::convertFlashParameter(const icamera::Parameters& /*parameter*/,
                                           android::CameraMetadata* settings) {
    uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    settings->update(ANDROID_FLASH_MODE, &flashMode, 1);

    return icamera::OK;
}

int MetadataConvert::convertAdvancedFeatureMetadata(const android::CameraMetadata& settings,
                                                    icamera::Parameters* parameter) {
    int ret = icamera::OK;
    // ANDROID_DEMOSAIC
    // ANDROID_EDGE
    // ANDROID_HOT_PIXEL
    // ANDROID_NOISE_REDUCTION

    // ANDROID_SHADING
    int mode;
    camera_metadata_ro_entry entry = settings.find(ANDROID_SHADING_MODE);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], shadingModeTable, ARRAY_SIZE(shadingModeTable), &mode);
        if (ret == icamera::OK) {
            parameter->setShadingMode((icamera::camera_shading_mode_t)mode);
        }
    }

    entry = settings.find(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], lensShadingMapModeTable,
                          ARRAY_SIZE(lensShadingMapModeTable), &mode);
        if (ret == icamera::OK) {
            parameter->setLensShadingMapMode((icamera::camera_lens_shading_map_mode_type_t)mode);
        }
    }

    // ANDROID_TONEMAP
    // ANDROID_INFO
    // ANDROID_BLACK_LEVEL

    return icamera::OK;
}

int MetadataConvert::convertAdvancedFeatureParameter(const icamera::Parameters& parameter,
                                                     android::CameraMetadata* settings) {
    int ret = icamera::OK;
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    // ANDROID_DEMOSAIC

    // ANDROID_EDGE

    // ANDROID_HOT_PIXEL

    // ANDROID_NOISE_REDUCTION

    // ANDROID_SHADING
    icamera::camera_shading_mode_t shadingMode;
    uint8_t saMode = ANDROID_SHADING_MODE_OFF;
    if (parameter.getShadingMode(shadingMode) == icamera::OK) {
        ret = getAndroidValue(shadingMode, shadingModeTable, ARRAY_SIZE(shadingModeTable), &saMode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_SHADING_MODE, &saMode, 1);
        }
    }

    icamera::camera_lens_shading_map_mode_type_t lensShadingMapMode;
    ret = parameter.getLensShadingMapMode(lensShadingMapMode);
    if (ret == icamera::OK) {
        uint8_t lensSMMode;
        ret = getAndroidValue(lensShadingMapMode, lensShadingMapModeTable,
                              ARRAY_SIZE(lensShadingMapModeTable), &lensSMMode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &lensSMMode, 1);
        }
    }

    if (lensShadingMapMode == icamera::LENS_SHADING_MAP_MODE_ON) {
        size_t lensShadingMapSize;
        float* lensShadingMap = nullptr;
        ret = parameter.getLensShadingMap(&lensShadingMap, lensShadingMapSize);
        if (ret == icamera::OK) {
            settings->update(ANDROID_STATISTICS_LENS_SHADING_MAP, lensShadingMap,
                             lensShadingMapSize);
            if (saMode == ANDROID_SHADING_MODE_OFF) {
                saMode = ANDROID_SHADING_MODE_FAST;
                settings->update(ANDROID_SHADING_MODE, &saMode, 1);
            }
        }
    }

    // ANDROID_TONEMAP
    // ANDROID_INFO
    // ANDROID_BLACK_LEVEL
    // ANDROID_SYNC

    return icamera::OK;
}

void MetadataConvert::fillControlStaticMetadata(const icamera::Parameters& parameter,
                                                android::CameraMetadata* settings) {
    int ret = icamera::OK;
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    icamera_metadata_entry entry = meta.find(CAMERA_CONTROL_AVAILABLE_MODES);
    if (entry.count != 0) {
        settings->update(ANDROID_CONTROL_AVAILABLE_MODES, entry.data.u8, entry.count);
    }

    std::vector<icamera::camera_antibanding_mode_t> antibandingModes;
    parameter.getSupportedAntibandingMode(antibandingModes);
    if (antibandingModes.size() > 0) {
        int size = antibandingModes.size();
        uint8_t data[size];
        int count = 0;
        for (int i = 0; i < size; i++) {
            ret = getAndroidValue(antibandingModes[i], antibandingModesTable,
                                  ARRAY_SIZE(antibandingModesTable), &data[count]);
            if (ret == icamera::OK) {
                count++;
            }
        }
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, data, count);
        }
    } else {
        LOGW("No antibanding modes provided!");
    }

    std::vector<icamera::camera_ae_mode_t> availAeModes;
    parameter.getSupportedAeMode(availAeModes);
    if (availAeModes.size() > 0) {
        int size = availAeModes.size();
        uint8_t data[size];
        int count = 0;
        for (int i = 0; i < size; i++) {
            ret = getAndroidValue(availAeModes[i], aeModesTable, ARRAY_SIZE(aeModesTable),
                                  &data[count]);
            if (ret == icamera::OK) {
                count++;
            }
        }
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AE_AVAILABLE_MODES, data, count);
        }
    } else {
        LOGW("No ae modes provided!");
    }

    uint8_t aeLockAvailable = parameter.getAeLockAvailable()
                                  ? ANDROID_CONTROL_AE_LOCK_AVAILABLE_TRUE
                                  : ANDROID_CONTROL_AE_LOCK_AVAILABLE_FALSE;
    settings->update(ANDROID_CONTROL_AE_LOCK_AVAILABLE, &aeLockAvailable, 1);

    icamera::camera_range_array_t fpsRanges;
    if (parameter.getSupportedFpsRange(fpsRanges) == 0) {
        int count = fpsRanges.size() * 2;
        int32_t data[count];
        for (size_t i = 0; i < fpsRanges.size(); i++) {
            data[i * 2] = (int32_t)fpsRanges[i].min;
            data[i * 2 + 1] = (int32_t)fpsRanges[i].max;
        }
        settings->update(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, data, count);
    } else {
        LOGW("No fps ranges provided!");
    }

    icamera::camera_range_t aeCompensationRange;
    if (parameter.getAeCompensationRange(aeCompensationRange) == 0) {
        int32_t data[2];
        data[0] = (int32_t)aeCompensationRange.min;
        data[1] = (int32_t)aeCompensationRange.max;
        settings->update(ANDROID_CONTROL_AE_COMPENSATION_RANGE, data, 2);
    } else {
        LOGW("No ae compensation range provided!");
    }

    icamera::camera_rational_t aeCompensationStep;
    if (parameter.getAeCompensationStep(aeCompensationStep) == 0) {
        camera_metadata_rational rational;
        rational.numerator = aeCompensationStep.numerator;
        rational.denominator = aeCompensationStep.denominator;
        settings->update(ANDROID_CONTROL_AE_COMPENSATION_STEP, &rational, 1);
    } else {
        LOGW("No ae compensation step provided!");
    }

    std::vector<icamera::camera_af_mode_t> availAfModes;
    parameter.getSupportedAfMode(availAfModes);
    if (availAfModes.size() > 0) {
        int size = availAfModes.size();
        uint8_t data[size];
        int count = 0;
        for (int i = 0; i < size; i++) {
            ret = getAndroidValue(availAfModes[i], afModesTable, ARRAY_SIZE(afModesTable),
                                  &data[count]);
            if (ret == icamera::OK) {
                count++;
            }
        }
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AF_AVAILABLE_MODES, data, count);
        }
    } else {
        LOGW("No af modes provided!");
    }

    uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    settings->update(ANDROID_CONTROL_AVAILABLE_EFFECTS, &effectMode, 1);

    entry = meta.find(CAMERA_CONTROL_AVAILABLE_SCENE_MODES);
    if (entry.count != 0) {
        settings->update(ANDROID_CONTROL_AVAILABLE_SCENE_MODES, entry.data.u8, entry.count);
    } else {
        LOGW("No available scene modes");
    }

    icamera::camera_video_stabilization_list_t availDvsModes;
    parameter.getSupportedVideoStabilizationMode(availDvsModes);
    if (availDvsModes.size() > 0) {
        int size = availDvsModes.size();
        uint8_t data[size];
        int count = 0;
        for (int i = 0; i < size; i++) {
            ret = getAndroidValue(availDvsModes[i], dvsModesTable, ARRAY_SIZE(dvsModesTable),
                                  &data[count]);
            if (ret == icamera::OK) {
                count++;
            }
        }
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, data, count);
        }
    } else {
        LOGW("No video stablization modes provided!");
    }

    std::vector<icamera::camera_awb_mode_t> availAwbModes;
    parameter.getSupportedAwbMode(availAwbModes);
    if (availAwbModes.size() > 0) {
        int size = availAwbModes.size();
        uint8_t data[size];
        int count = 0;
        for (int i = 0; i < size; i++) {
            ret = getAndroidValue(availAwbModes[i], awbModesTable, ARRAY_SIZE(awbModesTable),
                                  &data[count]);
            if (ret == icamera::OK) {
                count++;
            }
        }
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AWB_AVAILABLE_MODES, data, count);
        }
    } else {
        LOGW("No awb modes provided!");
    }

    uint8_t awbLockAvailable = parameter.getAwbLockAvailable()
                                   ? ANDROID_CONTROL_AWB_LOCK_AVAILABLE_TRUE
                                   : ANDROID_CONTROL_AWB_LOCK_AVAILABLE_FALSE;
    settings->update(ANDROID_CONTROL_AWB_LOCK_AVAILABLE, &awbLockAvailable, 1);

    int32_t rawSensitivity = 100;
    settings->update(ANDROID_CONTROL_POST_RAW_SENSITIVITY_BOOST, &rawSensitivity, 1);

    int32_t rawSensitivityRange[2] = {100, 100};
    settings->update(ANDROID_CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE, rawSensitivityRange, 2);

    entry = meta.find(CAMERA_CONTROL_MAX_REGIONS);
    if (entry.count >= 1) {
        settings->update(ANDROID_CONTROL_MAX_REGIONS, entry.data.i32, entry.count);
    }
}

void MetadataConvert::fillScalerStaticMetadata(const icamera::Parameters& parameter,
                                               android::CameraMetadata* settings) {
// stream configuration: fmt, w, h, type
#define STREAM_CFG_SIZE 4
// duration: fmt, w, h, ns
#define DURATION_SIZE 4

    float maxDigitalZoom = 1.0;
    settings->update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, &maxDigitalZoom, 1);

    uint8_t type = ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY;
    settings->update(ANDROID_SCALER_CROPPING_TYPE, &type, 1);

    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    uint32_t tag = CAMERA_SCALER_AVAILABLE_INPUT_OUTPUT_FORMATS_MAP;
    icamera_metadata_entry entry = meta.find(tag);
    if (entry.count > 0) {
        settings->update(ANDROID_SCALER_AVAILABLE_INPUT_OUTPUT_FORMATS_MAP, entry.data.i32,
                         entry.count);
    }

    tag = CAMERA_REPROCESS_MAX_CAPTURE_STALL;
    entry = meta.find(tag);
    if (entry.count > 0) {
        settings->update(ANDROID_REPROCESS_MAX_CAPTURE_STALL, entry.data.i32, entry.count);
    }

    tag = CAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS;
    entry = meta.find(tag);
    if (entry.count > 0) {
        settings->update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, entry.data.i32,
                         entry.count);
    }

    tag = CAMERA_SCALER_AVAILABLE_MIN_FRAME_DURATIONS;
    entry = meta.find(tag);
    if (entry.count > 0) {
        settings->update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, entry.data.i64, entry.count);
    }

    tag = CAMERA_SCALER_AVAILABLE_STALL_DURATIONS;
    entry = meta.find(tag);
    if (entry.count > 0) {
        settings->update(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS, entry.data.i64, entry.count);
    }
}

void MetadataConvert::fillTonemapStaticMetadata(const icamera::Parameters& parameter,
                                                android::CameraMetadata* settings) {
    int ret = icamera::OK;

    int32_t maxPoint = 0;
    if (parameter.getTonemapMaxCurvePoints(maxPoint) == 0) {
        settings->update(ANDROID_TONEMAP_MAX_CURVE_POINTS, &maxPoint, 1);
    }

    std::vector<icamera::camera_tonemap_mode_t> tonemapModes;
    parameter.getSupportedTonemapMode(tonemapModes);
    if (tonemapModes.size() > 0) {
        int size = tonemapModes.size();
        uint8_t data[size];
        int count = 0;
        for (int i = 0; i < size; i++) {
            ret = getAndroidValue(tonemapModes[i], tonemapModesTable, ARRAY_SIZE(tonemapModesTable),
                                  &data[count]);
            if (ret == icamera::OK) {
                count++;
            }
        }
        if (count > 0) {
            settings->update(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES, data, count);
        }
    }
}

void MetadataConvert::fillSensorStaticMetadata(const icamera::Parameters& parameter,
                                               android::CameraMetadata* settings) {
    icamera::camera_range_t timeRange;
    // Fill it if it is supported
    if (parameter.getSupportedSensorExposureTimeRange(timeRange) == 0) {
        int64_t range[2];
        range[0] = timeRange.min * 1000LLU;  // us -> ns
        range[1] = timeRange.max * 1000LLU;  // us -> ns
        settings->update(ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE, range, 2);
        settings->update(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION, &(range[1]), 1);
    } else {
        LOGW("No SensorExposureTimeRange provided!");
    }

    icamera::camera_range_t sensitivityRange;
    if (parameter.getSupportedSensorSensitivityRange(sensitivityRange) == 0) {
        int32_t range[2];
        range[0] = (int32_t)sensitivityRange.min;
        range[1] = (int32_t)sensitivityRange.max;
        settings->update(ANDROID_SENSOR_INFO_SENSITIVITY_RANGE, range, 2);
        settings->update(ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY, &range[1], 1);
    } else {
        LOGW("No SensorSensitivityRange provided!");
    }

    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    uint32_t tag = CAMERA_SENSOR_INFO_ACTIVE_ARRAY_SIZE;
    icamera_metadata_entry entry = meta.find(tag);
    // Check if the count is correct
    if (entry.count == 4) {
        settings->update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, entry.data.i32, entry.count);
    }

    tag = CAMERA_SENSOR_OPAQUE_RAW_SIZE;
    entry = meta.find(tag);
    if (entry.count > 0) {
        settings->update(ANDROID_SENSOR_OPAQUE_RAW_SIZE, entry.data.i32, entry.count);
    }

    tag = CAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE;
    entry = meta.find(tag);
    if (entry.count == 2) {
        settings->update(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, entry.data.i32, entry.count);
    }

    tag = CAMERA_SENSOR_INFO_PHYSICAL_SIZE;
    entry = meta.find(tag);
    if (entry.count == 2) {
        settings->update(ANDROID_SENSOR_INFO_PHYSICAL_SIZE, entry.data.f, entry.count);
    }

    tag = CAMERA_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT, entry.data.u8, entry.count);
    }

    tag = CAMERA_SENSOR_AVAILABLE_TEST_PATTERN_MODES;
    entry = meta.find(tag);
    if (entry.count != 0) {
        settings->update(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, entry.data.i32, entry.count);
    }

    int32_t whiteLevel = 0;
    settings->update(ANDROID_SENSOR_INFO_WHITE_LEVEL, &whiteLevel, 1);

    int32_t blackLevelPattern[4] = {0, 0, 0, 0};
    settings->update(ANDROID_SENSOR_BLACK_LEVEL_PATTERN, blackLevelPattern, 4);

    uint8_t timestampSource = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN;
    settings->update(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &timestampSource, 1);

    camera_metadata_rational_t baseGainFactor = {0, 1};
    settings->update(ANDROID_SENSOR_BASE_GAIN_FACTOR, &baseGainFactor, 1);

    int32_t orientation = 0;
    tag = CAMERA_SENSOR_ORIENTATION;
    entry = meta.find(tag);
    if (entry.count == 1) {
        orientation = entry.data.u8[0];
    }
    settings->update(ANDROID_SENSOR_ORIENTATION, &orientation, 1);

    int32_t profileHueSatMapDimensions[3] = {0, 0, 0};
    settings->update(ANDROID_SENSOR_PROFILE_HUE_SAT_MAP_DIMENSIONS, profileHueSatMapDimensions, 3);
}

void MetadataConvert::fillLensStaticMetadata(const icamera::Parameters& parameter,
                                             android::CameraMetadata* settings) {
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    float aperture = 0.0;
    if (icamera::OK == parameter.getLensAperture(aperture)) {
        settings->update(ANDROID_LENS_INFO_AVAILABLE_APERTURES, &aperture, 1);
    }

    float filterDensity = 0.0;
    if (icamera::OK == parameter.getLensFilterDensity(filterDensity)) {
        settings->update(ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES, &filterDensity, 1);
    }

    uint32_t tag = CAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS;
    icamera_metadata_entry entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, entry.data.f, entry.count);
    }

    float hyperfocalDistance = 0.0;
    if (icamera::OK == parameter.getLensHyperfocalDistance(hyperfocalDistance)) {
        settings->update(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, &hyperfocalDistance, 1);
    }

    float minFocusDistance = 0.0;
    if (icamera::OK == parameter.getLensMinFocusDistance(minFocusDistance)) {
        settings->update(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE, &minFocusDistance, 1);
    }

    tag = CAMERA_LENS_INFO_SHADING_MAP_SIZE;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_LENS_INFO_SHADING_MAP_SIZE, entry.data.i32, entry.count);
    }

    tag = CAMERA_LENS_INFO_FOCUS_DISTANCE_CALIBRATION;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION, entry.data.u8, entry.count);
    }

    tag = CAMERA_LENS_FACING;
    entry = meta.find(tag);
    uint8_t lensFacing = ANDROID_LENS_FACING_BACK;
    if (entry.count == 1) {
        lensFacing = entry.data.u8[0];
    }
    settings->update(ANDROID_LENS_FACING, &lensFacing, 1);

    uint8_t availableOpticalStabilization = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    settings->update(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
                     &availableOpticalStabilization, 1);
}

void MetadataConvert::fillRequestStaticMetadata(const icamera::Parameters& parameter,
                                                android::CameraMetadata* settings) {
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    uint32_t tag = CAMERA_REQUEST_MAX_NUM_OUTPUT_STREAMS;
    icamera_metadata_entry entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, entry.data.i32, entry.count);
    }

    tag = CAMERA_REQUEST_PIPELINE_MAX_DEPTH;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, entry.data.u8, entry.count);
    }

    tag = CAMERA_REQUEST_AVAILABLE_CAPABILITIES;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_REQUEST_AVAILABLE_CAPABILITIES, entry.data.u8, entry.count);
    }

    tag = CAMERA_REQUEST_MAX_NUM_INPUT_STREAMS;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, entry.data.i32, entry.count);
    }

    int32_t partialResultCount = 1;
    settings->update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &partialResultCount, 1);

    int32_t requestKeysBasic[] = {ANDROID_BLACK_LEVEL_LOCK,
                                  ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
                                  ANDROID_COLOR_CORRECTION_GAINS,
                                  ANDROID_COLOR_CORRECTION_TRANSFORM,
                                  ANDROID_CONTROL_AE_ANTIBANDING_MODE,
                                  ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
                                  ANDROID_CONTROL_AE_LOCK,
                                  ANDROID_CONTROL_AE_MODE,
                                  ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
                                  ANDROID_CONTROL_AF_MODE,
                                  ANDROID_CONTROL_AE_REGIONS,
                                  ANDROID_CONTROL_AF_TRIGGER,
                                  ANDROID_CONTROL_AWB_LOCK,
                                  ANDROID_CONTROL_AWB_MODE,
                                  ANDROID_CONTROL_CAPTURE_INTENT,
                                  ANDROID_CONTROL_EFFECT_MODE,
                                  ANDROID_CONTROL_MODE,
                                  ANDROID_CONTROL_SCENE_MODE,
                                  ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
                                  ANDROID_EDGE_MODE,
                                  ANDROID_FLASH_MODE,
                                  ANDROID_JPEG_ORIENTATION,
                                  ANDROID_JPEG_QUALITY,
                                  ANDROID_JPEG_THUMBNAIL_QUALITY,
                                  ANDROID_JPEG_THUMBNAIL_SIZE,
                                  ANDROID_SCALER_CROP_REGION,
                                  ANDROID_STATISTICS_FACE_DETECT_MODE,
                                  ANDROID_SENSOR_FRAME_DURATION,
                                  ANDROID_SENSOR_EXPOSURE_TIME,
                                  ANDROID_SENSOR_SENSITIVITY,
                                  ANDROID_HOT_PIXEL_MODE,
                                  ANDROID_LENS_APERTURE,
                                  ANDROID_LENS_FOCAL_LENGTH,
                                  ANDROID_LENS_FOCUS_DISTANCE,
                                  ANDROID_LENS_FILTER_DENSITY,
                                  ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
                                  ANDROID_NOISE_REDUCTION_MODE,
                                  ANDROID_REQUEST_ID,
                                  ANDROID_REQUEST_TYPE,
                                  ANDROID_TONEMAP_MODE,
                                  ANDROID_TONEMAP_PRESET_CURVE,
                                  ANDROID_TONEMAP_GAMMA,
                                  ANDROID_SHADING_MODE,
                                  ANDROID_STATISTICS_LENS_SHADING_MAP_MODE};
    // depends on CAMERA_CONTROL_MAX_REGIONS
    int32_t requestKeysExtra[] = {ANDROID_CONTROL_AF_REGIONS};

    size_t basicKeysCnt = sizeof(requestKeysBasic) / sizeof(requestKeysBasic[0]);
    size_t extraKeysCnt = sizeof(requestKeysExtra) / sizeof(requestKeysExtra[0]);
    int32_t* totalRequestKeys = new int32_t[basicKeysCnt + extraKeysCnt];
    MEMCPY_S(totalRequestKeys, sizeof(requestKeysBasic), requestKeysBasic,
             sizeof(requestKeysBasic));
    entry = meta.find(CAMERA_CONTROL_MAX_REGIONS);
    if (entry.count == 3 && entry.data.i32[2] > 0) {
        totalRequestKeys[basicKeysCnt] = ANDROID_CONTROL_AF_REGIONS;
        basicKeysCnt++;
    }
    settings->update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, totalRequestKeys, basicKeysCnt);
    delete []totalRequestKeys;

    int32_t resultKeysBasic[] = {ANDROID_REQUEST_ID,
                                 ANDROID_REQUEST_TYPE,
                                 ANDROID_COLOR_CORRECTION_MODE,
                                 ANDROID_COLOR_CORRECTION_GAINS,
                                 ANDROID_COLOR_CORRECTION_TRANSFORM,
                                 ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
                                 ANDROID_CONTROL_AE_ANTIBANDING_MODE,
                                 ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
                                 ANDROID_CONTROL_AE_LOCK,
                                 ANDROID_CONTROL_AE_MODE,
                                 ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
                                 ANDROID_CONTROL_AF_MODE,
                                 ANDROID_CONTROL_AE_REGIONS,
                                 ANDROID_CONTROL_AF_REGIONS,
                                 ANDROID_CONTROL_AF_TRIGGER,
                                 ANDROID_CONTROL_AWB_LOCK,
                                 ANDROID_CONTROL_AWB_MODE,
                                 ANDROID_CONTROL_CAPTURE_INTENT,
                                 ANDROID_CONTROL_EFFECT_MODE,
                                 ANDROID_CONTROL_MODE,
                                 ANDROID_CONTROL_SCENE_MODE,
                                 ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
                                 ANDROID_CONTROL_AE_STATE,
                                 ANDROID_CONTROL_AF_STATE,
                                 ANDROID_CONTROL_AWB_STATE,
                                 ANDROID_SYNC_FRAME_NUMBER,
                                 ANDROID_EDGE_MODE,
                                 ANDROID_FLASH_MODE,
                                 ANDROID_JPEG_ORIENTATION,
                                 ANDROID_JPEG_QUALITY,
                                 ANDROID_JPEG_THUMBNAIL_QUALITY,
                                 ANDROID_JPEG_THUMBNAIL_SIZE,
                                 ANDROID_LENS_APERTURE,
                                 ANDROID_LENS_FOCAL_LENGTH,
                                 ANDROID_LENS_FOCUS_DISTANCE,
                                 ANDROID_LENS_FILTER_DENSITY,
                                 ANDROID_LENS_FOCUS_RANGE,
                                 ANDROID_LENS_STATE,
                                 ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
                                 ANDROID_SCALER_CROP_REGION,
                                 ANDROID_SENSOR_FRAME_DURATION,
                                 ANDROID_SENSOR_EXPOSURE_TIME,
                                 ANDROID_SENSOR_SENSITIVITY,
                                 ANDROID_HOT_PIXEL_MODE,
                                 ANDROID_REQUEST_PIPELINE_DEPTH,
                                 ANDROID_SHADING_MODE,
                                 ANDROID_STATISTICS_FACE_DETECT_MODE,
                                 ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE,
                                 ANDROID_STATISTICS_LENS_SHADING_MAP_MODE,
                                 ANDROID_STATISTICS_SCENE_FLICKER,
                                 ANDROID_NOISE_REDUCTION_MODE,
                                 ANDROID_TONEMAP_CURVE_RED,
                                 ANDROID_TONEMAP_CURVE_BLUE,
                                 ANDROID_TONEMAP_CURVE_GREEN};
    size_t resultKeysCnt = sizeof(resultKeysBasic) / sizeof(resultKeysBasic[0]);
    settings->update(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, resultKeysBasic, resultKeysCnt);

    int32_t characteristicsKeysBasic[] = {ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
                                          ANDROID_CONTROL_AVAILABLE_MODES,
                                          ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
                                          ANDROID_CONTROL_AE_AVAILABLE_MODES,
                                          ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
                                          ANDROID_CONTROL_AE_COMPENSATION_RANGE,
                                          ANDROID_CONTROL_AE_COMPENSATION_STEP,
                                          ANDROID_CONTROL_AE_LOCK_AVAILABLE,
                                          ANDROID_CONTROL_AF_AVAILABLE_MODES,
                                          ANDROID_CONTROL_AVAILABLE_EFFECTS,
                                          ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
                                          ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
                                          ANDROID_CONTROL_AWB_AVAILABLE_MODES,
                                          ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
                                          ANDROID_EDGE_AVAILABLE_EDGE_MODES,
                                          ANDROID_FLASH_INFO_AVAILABLE,
                                          ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES,
                                          ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
                                          ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
                                          ANDROID_LENS_FACING,
                                          ANDROID_LENS_INFO_AVAILABLE_APERTURES,
                                          ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
                                          ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
                                          ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
                                          ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,
                                          ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
                                          ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
                                          ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
                                          ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                          ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,
                                          ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
                                          ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
                                          ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
                                          ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
                                          ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
                                          ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
                                          ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
                                          ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                                          ANDROID_SCALER_CROPPING_TYPE,
                                          ANDROID_SENSOR_BLACK_LEVEL_PATTERN,
                                          ANDROID_SENSOR_ORIENTATION,
                                          ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
                                          ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT,
                                          ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE,
                                          ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
                                          ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
                                          ANDROID_SENSOR_INFO_SENSITIVITY_RANGE,
                                          ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
                                          ANDROID_SENSOR_INFO_WHITE_LEVEL,
                                          ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
                                          ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
                                          ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY,
                                          ANDROID_SHADING_AVAILABLE_MODES,
                                          ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
                                          ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
                                          ANDROID_SYNC_MAX_LATENCY,
                                          ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES,
                                          ANDROID_TONEMAP_MAX_CURVE_POINTS};
    settings->update(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, characteristicsKeysBasic,
                     sizeof(characteristicsKeysBasic) / sizeof(int32_t));
}

void MetadataConvert::fillStatisticsStaticMetadata(const icamera::Parameters& parameter,
                                                   android::CameraMetadata* settings) {
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    icamera_metadata_entry entry;
    entry = meta.find(CAMERA_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES);
    if (entry.count != 0) {
        settings->update(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES, entry.data.u8,
                         entry.count);
    } else {
        uint8_t availFaceDetectMode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
        settings->update(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES, &availFaceDetectMode,
                         1);
    }

    entry = meta.find(CAMERA_STATISTICS_INFO_MAX_FACE_COUNT);
    if (entry.count != 0) {
        settings->update(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, entry.data.i32, entry.count);
    } else {
        int32_t maxFaceCount = 0;
        settings->update(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, &maxFaceCount, 1);
    }

    int32_t histogramBucketCount = 0;
    settings->update(ANDROID_STATISTICS_INFO_HISTOGRAM_BUCKET_COUNT, &histogramBucketCount, 1);

    int32_t maxHistogramCount = 0;
    settings->update(ANDROID_STATISTICS_INFO_MAX_HISTOGRAM_COUNT, &maxHistogramCount, 1);

    int32_t maxSharpnessMapValue = 0;
    settings->update(ANDROID_STATISTICS_INFO_MAX_SHARPNESS_MAP_VALUE, &maxSharpnessMapValue, 1);

    int32_t sharpnessMapSize[2] = {0, 0};
    settings->update(ANDROID_STATISTICS_INFO_SHARPNESS_MAP_SIZE, sharpnessMapSize, 2);

    uint8_t availableHotPixelMapModes = ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
    settings->update(ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
                     &availableHotPixelMapModes, 1);

    uint8_t availableLensShadingMapModes = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
    settings->update(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
                     &availableLensShadingMapModes, 1);
}

void MetadataConvert::fillJpegStaticMetadata(const icamera::Parameters& parameter,
                                             android::CameraMetadata* settings) {
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    uint32_t tag = CAMERA_JPEG_MAX_SIZE;
    icamera_metadata_entry entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_JPEG_MAX_SIZE, entry.data.i32, entry.count);
    }

    tag = CAMERA_JPEG_AVAILABLE_THUMBNAIL_SIZES;
    entry = meta.find(tag);
    if (entry.count >= 2) {
        settings->update(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, entry.data.i32, entry.count);
    }
}

void MetadataConvert::fillAdvancedFeatureStaticMetadata(const icamera::Parameters& parameter,
                                                        android::CameraMetadata* settings) {
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    // ANDROID_DEMOSAIC

    // ANDROID_EDGE
    uint32_t tag = CAMERA_EDGE_AVAILABLE_EDGE_MODES;
    icamera_metadata_entry entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_EDGE_AVAILABLE_EDGE_MODES, entry.data.u8, entry.count);
    }

    // ANDROID_HOT_PIXEL
    tag = CAMERA_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES, entry.data.u8, entry.count);
    }

    // ANDROID_NOISE_REDUCTION
    tag = CAMERA_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES, entry.data.u8,
                         entry.count);
    }

    // ANDROID_SHADING
    tag = CAMERA_SHADING_AVAILABLE_MODES;
    entry = meta.find(tag);
    if (entry.count != 0) {
        settings->update(ANDROID_SHADING_AVAILABLE_MODES, entry.data.u8, entry.count);
    }

    // ANDROID_TONEMAP
    tag = CAMERA_TONEMAP_MAX_CURVE_POINTS;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_TONEMAP_MAX_CURVE_POINTS, entry.data.i32, entry.count);
    }

    tag = CAMERA_TONEMAP_AVAILABLE_TONE_MAP_MODES;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES, entry.data.u8, entry.count);
    }

    // ANDROID_INFO
    tag = CAMERA_INFO_SUPPORTED_HARDWARE_LEVEL;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, entry.data.u8, entry.count);
    }

    // ANDROID_BLACK_LEVEL

    // ANDROID_SYNC
    tag = CAMERA_SYNC_MAX_LATENCY;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_SYNC_MAX_LATENCY, entry.data.i32, entry.count);
    }
}

int MetadataConvert::convertToHalWindow(const int32_t* data, int dataCount,
                                        icamera::camera_window_list_t* windows) {
    windows->clear();
    CheckError((!data), icamera::BAD_VALUE, "null data to convert hal window!");
    CheckError((dataCount % 5 != 0), icamera::BAD_VALUE, "wrong data count %d!", dataCount);

    icamera::camera_window_t window;
    for (int i = 0; i < dataCount / 5; i += 5) {
        window.left = data[i];
        window.top = data[i + 1];
        window.right = data[i + 2];
        window.bottom = data[i + 3];
        window.weight = data[i + 4];
        windows->push_back(window);
    }
    return icamera::OK;
}

int MetadataConvert::convertToMetadataRegion(const icamera::camera_window_list_t& windows,
                                             int dataCount, int32_t* data) {
    size_t num = windows.size();
    CheckError((!data), 0, "null data to convert Metadata region!");
    CheckError(((unsigned int)dataCount < num * 5), 0, "small dataCount!");

    for (size_t i = 0; i < windows.size(); i++) {
        data[i * 5] = windows[i].left;
        data[i * 5 + 1] = windows[i].top;
        data[i * 5 + 2] = windows[i].right;
        data[i * 5 + 3] = windows[i].bottom;
        data[i * 5 + 4] = windows[i].weight;
    }

    return num * 5;
}

void MetadataConvert::dumpMetadata(const camera_metadata_t* meta) {
    if (!meta || !icamera::Log::isDebugLevelEnable(icamera::CAMERA_DEBUG_LOG_LEVEL2)) {
        return;
    }

    LOG2("%s", __func__);
    int entryCount = get_camera_metadata_entry_count(meta);

    for (int i = 0; i < entryCount; i++) {
        camera_metadata_entry_t entry;
        if (get_camera_metadata_entry(const_cast<camera_metadata_t*>(meta), i, &entry)) {
            continue;
        }

        // Print tag & type
        const char *tagName, *tagSection;
        tagSection = get_camera_metadata_section_name(entry.tag);
        if (tagSection == nullptr) {
            tagSection = "unknownSection";
        }
        tagName = get_camera_metadata_tag_name(entry.tag);
        if (tagName == nullptr) {
            tagName = "unknownTag";
        }
        const char* typeName;
        if (entry.type >= NUM_TYPES) {
            typeName = "unknown";
        } else {
            typeName = camera_metadata_type_names[entry.type];
        }
        LOG2("(%d)%s.%s (%05x): %s[%zu], type: %d", i, tagSection, tagName, entry.tag, typeName,
             entry.count, entry.type);

        // Print data
        size_t j;
        const uint8_t* u8;
        const int32_t* i32;
        const float* f;
        const int64_t* i64;
        const double* d;
        const camera_metadata_rational_t* r;
        std::ostringstream stringStream;
        stringStream << "[";

        switch (entry.type) {
            case TYPE_BYTE:
                u8 = entry.data.u8;
                for (j = 0; j < entry.count; j++) stringStream << (int32_t)u8[j] << " ";
                break;
            case TYPE_INT32:
                i32 = entry.data.i32;
                for (j = 0; j < entry.count; j++) stringStream << " " << i32[j] << " ";
                break;
            case TYPE_FLOAT:
                f = entry.data.f;
                for (j = 0; j < entry.count; j++) stringStream << " " << f[j] << " ";
                break;
            case TYPE_INT64:
                i64 = entry.data.i64;
                for (j = 0; j < entry.count; j++) stringStream << " " << i64[j] << " ";
                break;
            case TYPE_DOUBLE:
                d = entry.data.d;
                for (j = 0; j < entry.count; j++) stringStream << " " << d[j] << " ";
                break;
            case TYPE_RATIONAL:
                r = entry.data.r;
                for (j = 0; j < entry.count; j++)
                    stringStream << " (" << r[j].numerator << ", " << r[j].denominator << ") ";
                break;
        }
        stringStream << "]";
        std::string str = stringStream.str();
        LOG2("%s", str.c_str());
    }
}

StaticCapability::StaticCapability(int cameraId) : mCameraId(cameraId) {
    LOG2("@%s, mCameraId %d", __func__, mCameraId);

    icamera::camera_info_t cameraInfo = {};
    icamera::get_camera_info(mCameraId, cameraInfo);
    icamera::ParameterHelper::copyMetadata(*cameraInfo.capability, &mMetadata);
}

StaticCapability::~StaticCapability() {
    LOG2("@%s, mCameraId: %d", __func__, mCameraId);
}

std::mutex StaticCapability::sLock;
std::unordered_map<int, StaticCapability*> StaticCapability::sInstances;

StaticCapability* StaticCapability::getInstance(int cameraId) {
    std::lock_guard<std::mutex> lock(sLock);
    if (sInstances.find(cameraId) == sInstances.end()) {
        sInstances[cameraId] = new StaticCapability(cameraId);
    }

    return sInstances[cameraId];
}

void StaticCapability::releaseInstance(int cameraId) {
    std::lock_guard<std::mutex> lock(sLock);
    if (sInstances.find(cameraId) != sInstances.end()) {
        StaticCapability* capability = sInstances[cameraId];
        sInstances.erase(cameraId);
        delete capability;
    }
}
}  // namespace camera3
