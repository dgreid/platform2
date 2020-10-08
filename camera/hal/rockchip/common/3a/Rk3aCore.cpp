/*
 * Copyright (C) 2014-2017 Intel Corporation
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
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

#define LOG_TAG "Rk3aCore"

#include "Rk3aCore.h"

#include <utils/Errors.h>
#include <math.h>

#include "PlatformData.h"
#include "LogHelper.h"
#include "PerformanceTraces.h"
#include "Utils.h"

NAMESPACE_DECLARATION {
void AiqInputParams::init()
{
    CLEAR(aeParams);
    CLEAR(awbParams);
    CLEAR(miscParams);
    CLEAR(sensorDescriptor);
    CLEAR(exposureWindow);
    CLEAR(awbWindow);
    CLEAR(aeManualLimits);
    CLEAR(manualCctRange);
    CLEAR(manualColorGains);
    aeLock = false;
    awbLock = false;
    restorePointers();
}

void AiqInputParams::restorePointers()
{
    aeParams.sensor_descriptor = &sensorDescriptor;
    aeParams.window = &exposureWindow;
    aeParams.manual_limits = &aeManualLimits;
    aeParams.manual_exposure_time_us = &manual_exposure_time_us[0];
    aeParams.manual_analog_gain = &manual_analog_gain[0];
    aeParams.manual_iso = &manual_iso[0];

    awbParams.window = &awbWindow;
    awbParams.manual_cct_range = &manualCctRange;
}

AiqInputParams &AiqInputParams::operator=(const AiqInputParams &other)
{
    LOG2("@%s", __FUNCTION__);
    if (this == &other)
        return *this;

    MEMCPY_S(this,
             sizeof(AiqInputParams),
             &other,
             sizeof(AiqInputParams));
    restorePointers();

    return *this;
}


Rk3aCore::Rk3aCore(int camId):
        mCameraId(camId)
{
    LOG1("@%s, mCameraId:%d", __FUNCTION__, mCameraId);
}

status_t Rk3aCore::init(const char* xmlFilePath)
{
    LOG1("@%s", __FUNCTION__);

    bool ret = mAiq.init(xmlFilePath);
    CheckAndLogError(ret == false, UNKNOWN_ERROR, "@%s, Error in IA AIQ init",
                     __FUNCTION__);

    LOG1("@%s: AIQ version: %s.", __FUNCTION__, mAiq.getVersion());

    return NO_ERROR;
}

void Rk3aCore::deinit()
{
    LOG1("@%s", __FUNCTION__);

    mAiq.deinit();
}

status_t Rk3aCore::setStatistics(rk_aiq_statistics_input_params *ispStatistics,
                                 rk_aiq_exposure_sensor_descriptor *sensor_desc)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    status = mAiq.statisticsSet(ispStatistics, sensor_desc);
    if (CC_UNLIKELY(status != NO_ERROR)) {
        LOGE("Error setting statistics before 3A");
    }
    return status;
}

status_t Rk3aCore::runAe(rk_aiq_statistics_input_params *ispStatistics,
                         rk_aiq_ae_input_params *aeInputParams,
                         rk_aiq_ae_results* aeResults)
{
    LOG2("@%s", __FUNCTION__);

    status_t status = NO_ERROR;
    rk_aiq_ae_results results;
    rk_aiq_ae_results* new_ae_results = &results;

    if (mAiq.isInitialized() == false) {
        LOGE("@%s, aiq doesn't initialized", __FUNCTION__);
        return NO_INIT;
    }

    /**
     * First set statistics if provided
     */
    if (ispStatistics != nullptr) {
        status = mAiq.statisticsSet(ispStatistics);
        if (CC_UNLIKELY(status != NO_ERROR)) {
            LOGE("Error setting statistics before 3A");
        }
    }
    // ToDo: cases to be considered in 3ACU
    //    - invalidated (empty ae results)
    //    - AE locked
    //    - AF assist light case (set the statistics from before assist light)


    if (aeInputParams != nullptr &&
        aeInputParams->manual_exposure_time_us &&
        aeInputParams->manual_analog_gain &&
        aeInputParams->manual_iso) {
        LOG2("AEC manual_exposure_time_us: %ld manual_analog_gain: %f manual_iso: %d",
             aeInputParams->manual_exposure_time_us[0],
             aeInputParams->manual_analog_gain[0],
             aeInputParams->manual_iso[0]);
        LOG2("AEC frame_use: %d", aeInputParams->frame_use);
        if (aeInputParams->sensor_descriptor != nullptr) {
            LOG2("AEC line_periods_per_field: %d",
                 aeInputParams->sensor_descriptor->line_periods_per_field);
        }
    }
    {
        PERFORMANCE_HAL_ATRACE_PARAM1("mAiq.aeRun", 1);
        status = mAiq.aeRun(aeInputParams, &new_ae_results);
    }
    if (status != NO_ERROR) {
        LOGE("Error running AE");
    } else {
        //storing results;
        if (CC_LIKELY(new_ae_results != nullptr))
            *aeResults = *new_ae_results;
    }

    return status;
}

status_t Rk3aCore::runAwb(rk_aiq_statistics_input_params *ispStatistics,
                          rk_aiq_awb_input_params *awbInputParams,
                          rk_aiq_awb_results* awbResults)
{
    LOG2("@%s", __FUNCTION__);

    status_t status = NO_ERROR;
    rk_aiq_awb_results results;
    rk_aiq_awb_results* new_awb_results = &results;

    if (mAiq.isInitialized() == false) {
        LOGE("@%s, aiq doesn't initialized", __FUNCTION__);
        return NO_INIT;
    }

    /**
     * First set statistics if provided
     */
    if (ispStatistics != nullptr) {
        status = mAiq.statisticsSet(ispStatistics);
        if (CC_UNLIKELY(status != NO_ERROR)) {
            LOGE("Error setting statistics before 3A");
        }
    }

    if (awbInputParams != nullptr) {
        // Todo: manual AWB
    }
    {
        PERFORMANCE_HAL_ATRACE_PARAM1("mAiq.awbRun", 1);
        status = mAiq.awbRun(awbInputParams, &new_awb_results);
    }
    if (CC_UNLIKELY(status != NO_ERROR)) {
        LOGE("Error running AWB");
    } else {
        //storing results;
        if (CC_LIKELY(new_awb_results != nullptr))
            *awbResults = *new_awb_results;
    }

    return status;
}

status_t Rk3aCore::runMisc(rk_aiq_statistics_input_params *ispStatistics,
                           rk_aiq_misc_isp_input_params *miscInputParams,
                           rk_aiq_misc_isp_results *miscResults)
{
    LOG2("@%s", __FUNCTION__);

    status_t status = NO_ERROR;
    rk_aiq_misc_isp_results results;
    rk_aiq_misc_isp_results* new_misc_results = &results;

    if (mAiq.isInitialized() == false) {
        LOGE("@%s, aiq doesn't initialized", __FUNCTION__);
        return NO_INIT;
    }

    /**
     * First set statistics if provided
     */
    if (ispStatistics != nullptr) {
        status = mAiq.statisticsSet(ispStatistics);
        if (CC_UNLIKELY(status != NO_ERROR)) {
            LOGE("Error setting statistics before run GBCE");
        }
    }
    {
        PERFORMANCE_HAL_ATRACE_PARAM1("mAiq.miscRun", 1);
        status = mAiq.miscRun(miscInputParams, &new_misc_results);
    }
    if (CC_UNLIKELY(status != NO_ERROR)) {
        LOGE("Error running MISC");
    } else {
        //storing results;
        if (CC_LIKELY(new_misc_results != nullptr))
            *miscResults = *new_misc_results;
    }

    return status;
}
} NAMESPACE_DECLARATION_END
