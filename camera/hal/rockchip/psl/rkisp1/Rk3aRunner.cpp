/*
 * Copyright (C) 2017 Intel Corporation.
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

#define LOG_TAG "Rk3aRunner"

#include "Rk3aRunner.h"

#include <math.h>
#include "LogHelper.h"
#include "CameraMetadataHelper.h"
#include "Rk3aPlus.h"
#include "RkAEStateMachine.h"
#include "RkAWBStateMachine.h"
#include "LensHw.h"
#include "RKISP1Common.h"
#include "SettingsProcessor.h"


namespace android {
namespace camera2 {


Rk3aRunner::Rk3aRunner(int camerId, Rk3aPlus *aaaWrapper, SettingsProcessor *settingsProcessor, LensHw *lensController) :
        mCameraId(camerId),
        m3aWrapper(aaaWrapper),
        mAeState(nullptr),
        mLensController(lensController)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
}

status_t Rk3aRunner::init()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    /*
     * Initialize the AE State Machine
     */
    mAeState = new RkAEStateMachine(mCameraId);

    /*
     * Initialize the AWB State Machine
     */
    mAwbState = new RkAWBStateMachine(mCameraId);

    CLEAR(mLatestResults);

    return OK;
}

Rk3aRunner::~Rk3aRunner()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    m3aWrapper = nullptr;

    delete mAeState;
    mAeState = nullptr;

    delete mAwbState;
    mAwbState = nullptr;
}

/**
 * run2A
 *
 * Runs AE and AWB for a request and submits the request for capture
 * together with the capture settings obtained after running these 2A algorithms
 *
 *\param [IN] reqState: Pointer to the request control structure to process
 *\return NO_ERROR
 */
status_t Rk3aRunner::run2A(RequestCtrlState &reqState, bool forceUpdated)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);

    status_t status = NO_ERROR;

    int reqId = reqState.request->getId();
    /*
     * Auto Exposure Compensation
     * certain settings changes require running the AE algorithm during AE
     * locked state. These at least:
     *  1) ev_shift changes
     *  2) FPS rate changes (TODO)
     */
    bool forceAeRun = mLatestInputParams.aeParams.ev_shift !=
                      reqState.aiqInputParams.aeParams.ev_shift;

    // process state when the request is actually processed
    mAeState->processState(reqState.aaaControls.controlMode,
                           reqState.aaaControls.ae);

    // copy control mode for capture unit to use
    reqState.captureSettings->controlMode = reqState.aaaControls.controlMode;
    reqState.captureSettings->controlAeMode = reqState.aaaControls.ae.aeMode;

    if (forceAeRun || mAeState->getState() != ANDROID_CONTROL_AE_STATE_LOCKED) {
        status = m3aWrapper->runAe(nullptr,
                                   &reqState.aiqInputParams.aeParams,
                                   &reqState.captureSettings->aiqResults.aeResults);

        if (CC_LIKELY(status == OK)) {
            reqState.aeState = ALGORITHM_RUN;
        } else {
            LOGE("Run AE failed for request Id %d", reqId);
            return UNKNOWN_ERROR;
        }
    } else {
        reqState.captureSettings->aiqResults.aeResults = mLatestResults.aeResults;
    }

    status = mAwbState->processState(reqState.aaaControls.controlMode,
                                     reqState.aaaControls.awb);

    /*
     * Client may enable AWB lock right from the start, so force AWB
     * to run at least once.
     */
    bool forceAwbRun = (reqId == 0);
    bool awbLocked = (mAwbState->getState() == ANDROID_CONTROL_AWB_STATE_LOCKED);

    /*
     * Auto White Balance
     */
    if (forceAwbRun || !awbLocked) {
        status = m3aWrapper->runAwb(nullptr,
                                    &reqState.aiqInputParams.awbParams,
                                    &reqState.captureSettings->aiqResults.awbResults);
        if (CC_LIKELY(status == OK)) {
            reqState.awbState = ALGORITHM_RUN;
        } else {
            LOGE("Run AWB failed for request Id %d", reqId);
            return UNKNOWN_ERROR;
        }
    } else {
        reqState.captureSettings->aiqResults.awbResults = mLatestResults.awbResults;
    }

    status = m3aWrapper->runMisc(nullptr,
                                 &reqState.aiqInputParams.miscParams,
                                 &reqState.captureSettings->aiqResults.miscIspResults);
    if (!CC_LIKELY(status == OK)) {
        LOGE("Run misc failed for request Id %d", reqId);
        return UNKNOWN_ERROR;
    }

    status = applyTonemaps(reqState);
    if (CC_UNLIKELY(status != OK)) {
        LOGE("Failed to apply tonemaps for request id %d", reqId);
    }

    /*
     * Result processing before we send them to HW
     */

    processAeResults(reqState);

    processAwbResults(reqState);

    return status;
}

/**
 * Generic results handler which runs after 3A has run. At this point of time
 * the state transitions for AE and AWB should be handled and those results
 * can be written to request metadata
 *
 * \param[in,out] reqState Request state structure
 */
status_t Rk3aRunner::processAeResults(RequestCtrlState &reqState)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);

    if (reqState.request == nullptr) {
        LOGE("Request is nullptr");
        return BAD_VALUE;
    }

    rk_aiq_ae_input_params &inParams = reqState.aiqInputParams.aeParams;
    uint8_t sceneFlickerMode = ANDROID_STATISTICS_SCENE_FLICKER_NONE;
    switch (inParams.flicker_reduction_mode) {
    case rk_aiq_ae_flicker_reduction_50hz:
        sceneFlickerMode = ANDROID_STATISTICS_SCENE_FLICKER_50HZ;
        break;
    case rk_aiq_ae_flicker_reduction_60hz:
        sceneFlickerMode = ANDROID_STATISTICS_SCENE_FLICKER_60HZ;
        break;
    default:
        sceneFlickerMode = ANDROID_STATISTICS_SCENE_FLICKER_NONE;
    }
    //# ANDROID_METADATA_Dynamic android.statistics.sceneFlicker done
    reqState.ctrlUnitResult->update(ANDROID_STATISTICS_SCENE_FLICKER,
                                    &sceneFlickerMode, 1);

    ///////////// AE precapture handling starts
    rk_aiq_ae_results &aeResult =
        reqState.captureSettings->aiqResults.aeResults;

    LOG2("%s exp_time=%d gain=%f", __FUNCTION__,
            aeResult.exposure.exposure_time_us,
            aeResult.exposure.analog_gain);

    mAeState->processResult(aeResult, *reqState.ctrlUnitResult,
                            reqState.request->getId());

    /* not support aeRegions now */
    //# ANDROID_METADATA_Dynamic android.control.aeRegions done

    //# ANDROID_METADATA_Dynamic android.control.aeExposureCompensation done
    // TODO get step size (currently 1/3) from static metadata
    int32_t exposureCompensation =
            round((reqState.aiqInputParams.aeParams.ev_shift) * 3);

    reqState.ctrlUnitResult->update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
                                    &exposureCompensation,
                                    1);
    return OK;

}

/**
 * Generic results handler which runs after AWB has run. At this point of time
 * we can modify the results before we send them towards the HW (sensor/ISP)
 *
 * \param reqState[in,out]: Request state structure
 * \return OK
 * \return UNKNOWN_ERROR
 */
status_t Rk3aRunner::processAwbResults(RequestCtrlState &reqState)
{
    if (CC_UNLIKELY(reqState.captureSettings.get() == nullptr)) {
        LOGE("Null capture settings when processing AWB results- BUG");
        return UNKNOWN_ERROR;
    }
    status_t status = OK;

    status = mAwbState->processResult(reqState.captureSettings->aiqResults.awbResults,
                                      *reqState.ctrlUnitResult);

    return status;

}

/*
 * Tonemap conversions or overwrites for CONTRAST_CURVE, GAMMA_VALUE, and
 * PRESET_CURVE modes
 */
status_t Rk3aRunner::applyTonemaps(RequestCtrlState &reqState)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    /*
     * Normal use-case is the automatic modes, and we need not do anything here
     */
    if (reqState.captureSettings->tonemapMode == ANDROID_TONEMAP_MODE_FAST ||
        reqState.captureSettings->tonemapMode == ANDROID_TONEMAP_MODE_HIGH_QUALITY) {
        // automatic modes, gbce output is used as-is
        return OK;
    }

    rk_aiq_goc_config &results = reqState.captureSettings->aiqResults.miscIspResults.gbce_config.goc_config;
    int lutSize = results.gamma_y.gamma_y_cnt;

    /*
     * Basic consistency check. If gbce isn't producing a lut, we can't
     * overwrite it.
     */
    if (lutSize <= 0) {
        LOGE("Bad gamma lut size (%d) in gbce results", lutSize);
        return UNKNOWN_ERROR;
    }

    /*
     * Contrast curve mode. Since IPU3 can't really support separate color
     * channel tonemaps, we can't fully support contrast curve. This hacky
     * implementation is just to satisfy one ITS test, other ITS tests are smart
     * enough to figure out that they should test against MODE_GAMMA_VALUE, when
     * CONTRAST_CURVE is not reported as supported. Alternatively CTS2 should
     * check that CONTRAST_CURVE is supported - which it does not do for FULL
     * capability devices. CTS2 is fine with just GAMMA_VALUE.
     */
    if (reqState.captureSettings->tonemapMode == ANDROID_TONEMAP_MODE_CONTRAST_CURVE) {
        /* TODO */
    }

    /*
     * Gamma value and preset curve modes. Generated on the fly based on the
     * current lut size.
     */
    if (reqState.captureSettings->tonemapMode == ANDROID_TONEMAP_MODE_GAMMA_VALUE) {
        /* TODO */
    }

    if (reqState.captureSettings->tonemapMode == ANDROID_TONEMAP_MODE_PRESET_CURVE) {
        if (reqState.captureSettings->presetCurve == ANDROID_TONEMAP_PRESET_CURVE_SRGB) {
            /* TODO */
        }
        if (reqState.captureSettings->presetCurve == ANDROID_TONEMAP_PRESET_CURVE_REC709) {
            /* TODO */
        }
    }

    return OK;
}

inline float Rk3aRunner::interpolate(float pos, const float *src, int srcSize) const
{
    if (pos <= 0)
        return src[0];

    if (pos >= srcSize - 1)
        return src[srcSize - 1];

    int i = int(pos);

    return src[i] + (pos - i) * (src[i + 1] - src[i]);
}

void Rk3aRunner::interpolateArray(const float *src, int srcSize, float *dst, int dstSize) const
{
    if (src == nullptr || dst == nullptr || srcSize < 2 || dstSize < 2) {
        LOGE("Bad input for array interpolation");
        return;
    }

    float step = float(srcSize - 1) / (dstSize - 1);
    for (int i = 0; i < dstSize; i++){
        dst[i] = interpolate(i * step, src, srcSize);
    }
}


} /* namespace camera2 */
} /* namespace android */
