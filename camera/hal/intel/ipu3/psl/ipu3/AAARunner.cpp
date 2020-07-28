/*
 * Copyright (C) 2018 Intel Corporation.
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

#define LOG_TAG "AAARunner"

#include "AAARunner.h"

#include <math.h>
#include "LogHelper.h"
#include "CameraMetadataHelper.h"
#include "Intel3aCore.h"
#include "IntelAEStateMachine.h"
#include "IntelAFStateMachine.h"
#include "IntelAWBStateMachine.h"
#include "LensHw.h"
#include "IPU3Types.h"
#include "SettingsProcessor.h"
#include "IntelAEStateMachine.h"
#include "IntelAWBStateMachine.h"


namespace cros {
namespace intel {

#define MIN3(a,b,c) MIN((a),MIN((b),(c)))

static const float EPSILON = 0.00001;
#define PRECAPTURE_ID_INVAL -1

AAARunner::AAARunner(int camerId, Intel3aPlus *aaaWrapper, SettingsProcessor *settingsProcessor, LensHw *lensController) :
        mCameraId(camerId),
        m3aWrapper(aaaWrapper),
        mAeState(nullptr),
        mAfState(nullptr),
        mAwbState(nullptr),
        mLensController(lensController),
        mLastSaGain(1.0),
        mSettingsProcessor(settingsProcessor),
        mDigiGainOnSensor(false),
        mPrecaptureResultRequestId(PRECAPTURE_ID_INVAL)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

    CLEAR(mResizeLscGridR);
    CLEAR(mResizeLscGridGr);
    CLEAR(mResizeLscGridGb);
    CLEAR(mResizeLscGridB);
    CLEAR(mLscGridRGGB);
    mLatestInputParams.init();

    // init LscOffGrid to 1.0f
    std::fill(std::begin(mLscOffGrid), std::end(mLscOffGrid), 1.0f);
}

status_t AAARunner::init(bool digiGainOnSensor)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

    mLatestInputParams.init();
    /*
     * Initialize the AE State Machine
     */
    mAeState = new IntelAEStateMachine(mCameraId);

    /*
     * Initialize the AF State Machine
     */
    mAfState = new IntelAFStateMachine(mCameraId, *m3aWrapper);

    /*
     * Initialize the AWB State Machine
     */
    mAwbState = new IntelAWBStateMachine(mCameraId);

    mDigiGainOnSensor = digiGainOnSensor;

    mLatestResults.init();

    return OK;
}

AAARunner::~AAARunner()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

    m3aWrapper = nullptr;

    delete mAeState;
    mAeState = nullptr;

    delete mAwbState;
    mAwbState = nullptr;

    delete mAfState;
    mAfState = nullptr;
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
status_t AAARunner::run2A(RequestCtrlState &reqState, bool forceUpdated)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);

    status_t status = NO_ERROR;
    int prevExposure = 0;
    int prevIso = 0;
    int currentExposure = 0;
    int currentIso = 0;
    uint8_t controlMode = reqState.aaaControls.controlMode;

    int reqId = reqState.request->getId();
    /*
     * Auto Exposure Compensation
     * certain settings changes require running the AE algorithm during AE
     * locked state. These at least:
     *  1) ev_shift changes
     *  2) FPS rate changes (TODO)
     */
    bool forceAeRun = mLatestInputParams.aeInputParams.ev_shift !=
                      reqState.aiqInputParams.aeInputParams.ev_shift;

    // process state when the request is actually processed
    mAeState->processState(reqState.aaaControls.controlMode,
                           reqState.aaaControls.ae);

    // copy control mode for capture unit to use
    reqState.captureSettings->controlMode = reqState.aaaControls.controlMode;
    reqState.captureSettings->controlAeMode = reqState.aaaControls.ae.aeMode;

    if (forceAeRun || mAeState->getState() != ANDROID_CONTROL_AE_STATE_LOCKED) {
        status = m3aWrapper->runAe(nullptr,
                                   &reqState.aiqInputParams.aeInputParams,
                                   &reqState.captureSettings->aiqResults.aeResults);

        if (CC_LIKELY(status == OK)) {
            reqState.aeState = ALGORITHM_RUN;
            Intel3aHelper::dumpAeResult(&reqState.captureSettings->aiqResults.aeResults);
        } else {
            LOGE("Run AE failed for request Id %d", reqId);
            return UNKNOWN_ERROR;
        }

        /*
          * Global Brightness and Contrast Enhancement
          */
        ia_aiq_gbce_input_params &gbceInput = reqState.aiqInputParams.gbceParams;

        // in case of OFFMODE, bypass gbce (using standard gbce)
        if (IS_CONTROL_MODE_OFF(controlMode))
            gbceInput.gbce_level = ia_aiq_gbce_level_bypass;
        else
            gbceInput.gbce_level = ia_aiq_gbce_level_use_tuning;

        gbceInput.frame_use = reqState.aiqInputParams.aeInputParams.frame_use;
        gbceInput.ev_shift = reqState.aiqInputParams.aeInputParams.ev_shift;

        status = m3aWrapper->runGbce(nullptr,
                                     &reqState.aiqInputParams.gbceParams,
                                     &reqState.captureSettings->aiqResults.gbceResults);

        if (CC_UNLIKELY(status != OK)) {
            LOGE("Run GBCE failed for request Id %d", reqId);
            return UNKNOWN_ERROR;
        }

    } else {
        m3aWrapper->deepCopyAEResults(&reqState.captureSettings->aiqResults.aeResults, &mLatestResults.aeResults);
        m3aWrapper->deepCopyGBCEResults(&reqState.captureSettings->aiqResults.gbceResults, &mLatestResults.gbceResults);
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
    Intel3aHelper::dumpAwbResult(&reqState.captureSettings->aiqResults.awbResults);
    /*
     * Parameter Adaptor RUN
     */
    // Prepare the ia_aiq_pa_input_params
    ia_aiq_pa_input_params* paInput = &reqState.aiqInputParams.paParams;
    paInput->awb_results = &reqState.captureSettings->aiqResults.awbResults;
    ia_aiq_ae_results &aeResult = reqState.captureSettings->aiqResults.aeResults;
    paInput->exposure_params = aeResult.exposures[0].exposure;
    /*
     * Do not apply digital gain through PA, due to one channel in HW being
     * stuck at gain 1.0
     */
    paInput->color_gains = nullptr;
    status = m3aWrapper->runPa(nullptr,
                               paInput,
                               &reqState.captureSettings->aiqResults.paResults);
    if (CC_UNLIKELY(status != NO_ERROR)) {
       LOGE("Failed to run PA for request of id %d", reqId);
    }

    if (mLatestResults.aeResults.num_exposures > 0) {
        prevExposure = mLatestResults.aeResults.exposures[0].exposure->exposure_time_us;
        prevIso = mLatestResults.aeResults.exposures[0].exposure->iso;
        currentExposure = aeResult.exposures[0].exposure->exposure_time_us;
        currentIso = aeResult.exposures[0].exposure->iso;
    }

    if (reqState.aiqInputParams.blackLevelLock) {
        if (prevExposure == currentExposure && prevIso == currentIso) {
            // overwrite black level from previous (== "latest") results
            reqState.captureSettings->aiqResults.paResults.black_level =
                    mLatestResults.paResults.black_level;
        } else {
            // Exposure or iso value changed. Set lock to off for this request
            LOG2("Set black level lock off");
            reqState.blackLevelOff = true;
        }
    }

    /*
     * Shading Adaptor RUN
     */
    bool oldSAResultsCopied = false;
    if (reqState.captureSettings->shadingMode != ANDROID_SHADING_MODE_OFF) {
        ia_aiq_sa_input_params *saInput = &reqState.aiqInputParams.saParams;
        saInput->awb_results = &reqState.captureSettings->aiqResults.awbResults;
        saInput->frame_use = reqState.aiqInputParams.aeInputParams.frame_use;
        saInput->sensor_frame_params = mSettingsProcessor->getCurrentFrameParams();
        status = m3aWrapper->runSa(nullptr,
                                   saInput,
                                   &reqState.captureSettings->aiqResults.saResults,
                                   forceUpdated);
        if (CC_UNLIKELY(status != NO_ERROR)) {
            LOGE("Failed to run SA for request of id %d", reqId);
        }

        if (!reqState.captureSettings->aiqResults.saResults.lsc_update &&
            mLatestResults.saResults.lsc_update == true) {
            // copy the old lsc table, if there was no update and we have an old
            m3aWrapper->deepCopySAResults(&reqState.captureSettings->aiqResults.saResults,
                                          &mLatestResults.saResults);
            // but don't claim that it is updated
            reqState.captureSettings->aiqResults.saResults.lsc_update = false;
            oldSAResultsCopied = true;
        }
    }

    if (mDigiGainOnSensor == false) {
        /*
         * Apply the digital gain. It has to be injected to SA results, and we need
         * to consider the fact that we might use old LSC which already has digital
         * gain applied to it.
         */
        float digitalGain = aeResult.exposures[0].exposure->digital_gain;

        if (oldSAResultsCopied && mLastSaGain > EPSILON) {
            // do not apply dg twice, so remove the last applied
            digitalGain /= mLastSaGain;
        } else if (!oldSAResultsCopied) {
            // save gain applied on next mLatest saResult
            mLastSaGain = digitalGain;
        }

        applyDigitalGain(reqState, digitalGain);
    }

    status = applyTonemaps(reqState);
    if (CC_UNLIKELY(status != OK)) {
        LOGE("Failed to apply tonemaps for request id %d", reqId);
    }

    /*
     * Result processing before we send them to HW
     */

    processSAResults(reqState);

    processAeResults(reqState);

    processAwbResults(reqState);

    updateNeutralColorPoint(reqState);

    return status;
}

/**
 * Runs the auto focus algorithm
 * Runs the state machine
 * Updates the metadata results
 * TODO: send the partial metadata results ahead of time
 *
 * \param[in,out] reqState The AF input parameters are input to this routine
 * the AF results also stored in this struct are output from this routine.
 *
 * The AF algorithm state is used to determine whether we need to run or not.
 */
void AAARunner::runAf(RequestCtrlState &reqState)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);

    status_t status = OK;
    bool fixedFocus = (m3aWrapper->getMinFocusDistance() == 0.0f);
    ia_aiq_af_input_params *afInputParams;
    ia_aiq_af_results *afResults;
    afInputParams = &reqState.aiqInputParams.afParams;
    afResults = &reqState.captureSettings->aiqResults.afResults;

    status = processAfTriggers(reqState);
    if (status != OK) {
        LOGE("Af triggers processing failed");
        goto exit;
    }

    if ((reqState.afState != ALGORITHM_READY &&
         reqState.aaaControls.af.afMode != ANDROID_CONTROL_AF_MODE_OFF) ||
         mLensController == nullptr) {
        /*
         * Not ready to run AF for several reasons:
         * - No stats (algo not ready) and not in manual (this should not happen)
         * - No lens controller (because this sensor is fixed focus)
         * update the state and leave
         */
        LOG2("AF state not ready or fixed focus sensor");
        mAfState->updateDefaults(*afResults,
                                 *afInputParams,
                                 *reqState.ctrlUnitResult,
                                 fixedFocus);
        goto exit;
    }

    /*
     * Get the lens position and time now, just before running AF
     */
    if (mLensController != nullptr) {
        mLensController->getLatestPosition(
                &afInputParams->lens_position,
                &afInputParams->lens_movement_start_timestamp);
    }

    // Uncomment for debugging
    Intel3aHelper::dumpAfInputParams(&reqState.aiqInputParams.afParams);

    status = m3aWrapper->runAf(nullptr, afInputParams, afResults);
    if (status == OK) {
        reqState.afState = ALGORITHM_RUN;
        // Uncomment for debugging
        Intel3aHelper::dumpAfResult(afResults);
        status = processAfResults(reqState);
    } else {
        LOGW("AF Failed, update default");
        mAfState->updateDefaults(*afResults,
                                 *afInputParams,
                                 *reqState.ctrlUnitResult,
                                 fixedFocus);
    }

exit:
    CameraWindow &reportedAfregion = reqState.captureSettings->afRegion;
    if (reqState.captureSettings->afRegion.isValid()) {
        //# ANDROID_METADATA_Dynamic android.control.afRegions done
        reqState.ctrlUnitResult->update(ANDROID_CONTROL_AF_REGIONS,
                        reportedAfregion.meteringRectangle(), METERING_RECT_SIZE);
    }

    return;
}

/**
 * Generic results handler which runs after 3A has run. At this point of time
 * the state transitions for AE, AWB and possibly AF should be handled and
 * those results can be written to request metadata
 *
 * \param[in,out] reqState Request state structure
 */
status_t AAARunner::processAeResults(RequestCtrlState &reqState)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);

    if (reqState.request == nullptr) {
        LOGE("Request is nullptr");
        return BAD_VALUE;
    }

    ia_aiq_ae_input_params &inParams = reqState.aiqInputParams.aeInputParams;
    uint8_t sceneFlickerMode = ANDROID_STATISTICS_SCENE_FLICKER_NONE;
    switch (inParams.flicker_reduction_mode) {
    case ia_aiq_ae_flicker_reduction_50hz:
        sceneFlickerMode = ANDROID_STATISTICS_SCENE_FLICKER_50HZ;
        break;
    case ia_aiq_ae_flicker_reduction_60hz:
        sceneFlickerMode = ANDROID_STATISTICS_SCENE_FLICKER_60HZ;
        break;
    default:
        sceneFlickerMode = ANDROID_STATISTICS_SCENE_FLICKER_NONE;
    }
    //# ANDROID_METADATA_Dynamic android.statistics.sceneFlicker done
    reqState.ctrlUnitResult->update(ANDROID_STATISTICS_SCENE_FLICKER,
                                    &sceneFlickerMode, 1);

    ///////////// AE precapture handling starts
    ia_aiq_ae_results &aeResult =
        reqState.captureSettings->aiqResults.aeResults;

    LOG2("%s exp_time=%d gain=%f", __FUNCTION__,
            aeResult.exposures->exposure->exposure_time_us,
            aeResult.exposures->exposure->analog_gain);

    mAeState->processResult(aeResult, *reqState.ctrlUnitResult,
                            reqState.request->getId());

    uint8_t intent = reqState.intent;
    // use the precapture settings, if they are available, and if they are recent
    if (mPrecaptureResults.aeResults.exposures->sensor_exposure->coarse_integration_time != 0 &&
        reqState.request->getId() <= mPrecaptureResultRequestId + PRECAP_TIME_ALIVE) {

        if (intent == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE ||
                intent == ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT) {
            LOG2("@%s: copy precapture settings", __FUNCTION__);
            m3aWrapper->deepCopyAiqResults(reqState.captureSettings->aiqResults, mPrecaptureResults);
            mPrecaptureResults.init();
        }
    }

    //# ANDROID_METADATA_Dynamic android.control.aeRegions done
    reqState.ctrlUnitResult->update(ANDROID_CONTROL_AE_REGIONS,
                        reqState.captureSettings->aeRegion.meteringRectangle(),
                        5);

    //# ANDROID_METADATA_Dynamic android.control.aeExposureCompensation done
    // TODO get step size (currently 1/3) from static metadata
    int32_t exposureCompensation =
            round((reqState.aiqInputParams.aeInputParams.ev_shift) * 3);

    reqState.ctrlUnitResult->update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
                                    &exposureCompensation,
                                    1);
    return OK;
}

/**
 *
 * Analyze the results of the AF algorithm and runs the state machine to update
 * the capture results
 *
 * It also relays to the lens driver the Optical Image Stabilization control
 *
 * This method is called just after AF algorithm has run
 *
 * \param[in,out] reqState from which input parameters are:
 *        .aiqInputParams: AF input params for processing AF result
 *        .captureSettings: settings for OIS
 *        .CameraMetadata: control values from framework
 *        and output parameters:
 *        .CameraMetadata: dynamic values to framework
 */
status_t AAARunner::processAfResults(RequestCtrlState &reqState)
{
    if (CC_UNLIKELY(reqState.captureSettings.get() == nullptr)) {
        LOGE("Null capture settings when processing AF results - BUG");
        return UNKNOWN_ERROR;
    }

    status_t status = OK;
    AiqResults &aiqResults = reqState.captureSettings->aiqResults;
    int32_t focusDistanceBoundLow = 0;
    int32_t focusDistanceBoundHigh = 0;
    float afDistanceControl = 0;
    float afDistanceDynamic = 0;
    ia_aiq_af_input_params *tempAfInputParams = &reqState.aiqInputParams.afParams;
    ia_aiq_af_results tempAfResults;
    camera_metadata_entry_t entry;
    bool resultParsed = false;

    CLEAR(tempAfResults);
    CLEAR(entry);

    /**
     * Process state machine transitions
     */
    status = mAfState->processResult(aiqResults.afResults,
                                     reqState.aiqInputParams.afParams,
                                     *reqState.ctrlUnitResult);

    /*
     * When setting the focus distance manually, the focus distance value is
     * converted from diopters (float) to millimeters (int), quantized to VCM
     * (Voice Coil Motor) units and eventually converted back to diopters.
     * This can cause a small offset between incoming and outgoing
     * (= control and dynamic) value of ANDROID_LENS_FOCUS_DISTANCE.
     * By calculating allowed bounds (that vary according to the current focus
     * distance), we are able to know when the difference between incoming and
     * outgoing focus distance is caused by this quantization. When the lens is
     * moving, the focus distance is outside the bounds. In that case we don't
     * tweak the outgoing (=dynamic) distance.
     */
    if (tempAfInputParams && tempAfInputParams->manual_focus_parameters != nullptr) {
        const android::CameraMetadata *settings = reqState.request->getSettings();
        if (CC_UNLIKELY(settings == nullptr)) {
            LOGE("Failed reading metadata settings - BUG");
            return UNKNOWN_ERROR;
        }

        resultParsed = MetadataHelper::getMetadataValue(*settings,
                                            ANDROID_LENS_FOCUS_DISTANCE,
                                            afDistanceControl,
                                            1);
        if (!resultParsed) {
            LOGE("Failed reading ANDROID_LENS_FOCUS_DISTANCE from metadata - BUG");
            return UNKNOWN_ERROR;
        }
        LOG2("ANDROID_LENS_FOCUS_DISTANCE control: %f", afDistanceControl);


        // Read and print out the dynamic focus distance for debugging purposes
        entry = reqState.ctrlUnitResult->find(ANDROID_LENS_FOCUS_DISTANCE);
        if (entry.count == 1) {
            afDistanceDynamic = (float)entry.data.f[0];
            LOG2("ANDROID_LENS_FOCUS_DISTANCE dynamic: %f", afDistanceDynamic);
        }

        tempAfInputParams = &reqState.aiqInputParams.afParams;
        tempAfInputParams->lens_position =
                            aiqResults.afResults.next_lens_position + 1;
        status = m3aWrapper->runAf(nullptr, tempAfInputParams, &tempAfResults);
        focusDistanceBoundLow = tempAfResults.current_focus_distance;

        tempAfInputParams->lens_position =
                            aiqResults.afResults.next_lens_position - 1;
        status = m3aWrapper->runAf(nullptr, tempAfInputParams, &tempAfResults);
        focusDistanceBoundHigh = tempAfResults.current_focus_distance;

        LOG2("current_focus_distance in mm: %d, bounds: [%d, %d]",
                                aiqResults.afResults.current_focus_distance,
                                focusDistanceBoundLow,
                                focusDistanceBoundHigh);

        if (aiqResults.afResults.current_focus_distance >= focusDistanceBoundLow &&
            aiqResults.afResults.current_focus_distance <= focusDistanceBoundHigh) {
            reqState.ctrlUnitResult->update(ANDROID_LENS_FOCUS_DISTANCE,
                                            &afDistanceControl,
                                            1);
        }
    }

    /**
     *  Send the results to the Hardware(Lens controller)
     */
    if (aiqResults.afResults.lens_driver_action == ia_aiq_lens_driver_action_move_to_unit) {
        if (CC_UNLIKELY(mLensController != nullptr)) {
           status = mLensController->moveFocusToPosition(aiqResults.afResults.next_lens_position);
           if (CC_UNLIKELY(status != OK)) {
               LOGE("AF Failed to move the lens to position %d",
                       aiqResults.afResults.next_lens_position);
           }
        }
    }
    // TODO: remove this once the request flow is fixed
    mLatestResults.afResults = aiqResults.afResults;

    /*
     * Act on the OIS control
     */
    if (mLensController != nullptr)
        mLensController->enableOis(reqState.captureSettings->opticalStabilizationMode);

    return status;
}

status_t AAARunner::processAfTriggers(RequestCtrlState &reqAiqCfg)
{

    /**
     * Update state machine if needed and modify the AF Input params
     * Keep in mind that even in fixedFocus case, we still are in AF AUTO!
     * we still have an AF object for the state machine, but it is not processed
     * only the results are updated. (bypass)
     */
    ia_aiq_af_input_params &afInputParams = reqAiqCfg.aiqInputParams.afParams;
    return mAfState->processTriggers(
        reqAiqCfg.aaaControls.af.afTrigger,
        reqAiqCfg.aaaControls.af.afMode,
        0,
        afInputParams);
}

status_t AAARunner::processSAResults(RequestCtrlState &reqState)
{
    status_t status = OK;
    if (reqState.captureSettings->shadingMapMode ==
        ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_ON) {

        Intel3aPlus::LSCGrid resizeGrid;
        resizeGrid.width = mSettingsProcessor->getLSCMapWidth();
        resizeGrid.height = mSettingsProcessor->getLSCMapHeight();
        if (reqState.captureSettings->aiqResults.saResults.lsc_update) {
            Intel3aPlus::LSCGrid inputGrid;
            ia_aiq_sa_results &sar = reqState.captureSettings->aiqResults.saResults;
            inputGrid.gridB = sar.channel_b;
            inputGrid.gridR = sar.channel_r;
            inputGrid.gridGr = sar.channel_gr;
            inputGrid.gridGb = sar.channel_gb;
            inputGrid.width = sar.width;
            inputGrid.height = sar.height;

            Intel3aPlus::LSCGrid resizeGrid;
            resizeGrid.gridB = mResizeLscGridB;
            resizeGrid.gridR = mResizeLscGridR;
            resizeGrid.gridGr = mResizeLscGridGr;
            resizeGrid.gridGb = mResizeLscGridGb;

            Intel3aPlus::storeLensShadingMap(inputGrid, resizeGrid, mLscGridRGGB);
        }

        // todo remove fix too small values from algorithm
        size_t size = resizeGrid.width * resizeGrid.height * 4;
        size_t errCount = 0;
        for (size_t i = 0; i < size; i++) {
            if (mLscGridRGGB[i] < 1.0f) {
                mLscGridRGGB[i] = 1.0f;
                errCount++;
            }
        }
        if (errCount) {
            LOGE("Error - SA produced too small values (%zu/%zu)!", errCount, size);
            status = BAD_VALUE;
        }

        bool lscOn = (reqState.captureSettings->shadingMode != ANDROID_SHADING_MODE_OFF);
        const float *lscMap = lscOn ? mLscGridRGGB : mLscOffGrid;
        reqState.ctrlUnitResult->update(ANDROID_STATISTICS_LENS_SHADING_MAP,
                                        lscMap,
                                        size);
    }

    return status;
}

/**
 * Generic results handler which runs after AWB has run. At this point of time
 * we can modify the results before we send them towards the HW (sensor/ISP)
 *
 * \param reqState[in,out]: Request state structure
 * \return OK
 * \return UNKNOWN_ERROR
 */
status_t AAARunner::processAwbResults(RequestCtrlState &reqState)
{
    if (CC_UNLIKELY(reqState.captureSettings.get() == nullptr)) {
        LOGE("Null capture settings when processing AWB results- BUG");
        return UNKNOWN_ERROR;
    }
    status_t status = OK;
    AAAControls &controls = reqState.aaaControls;
    ia_aiq_pa_results &paResults = reqState.captureSettings->aiqResults.paResults;

    if (controls.awb.awbMode == ANDROID_CONTROL_AWB_MODE_OFF &&
        controls.awb.colorCorrectionMode == ANDROID_COLOR_CORRECTION_MODE_TRANSFORM_MATRIX) {
        /*
         * Overwrite the PA results if client provided its own color gains
         * and color conversion matrix
         */
        paResults.color_gains = reqState.aiqInputParams.manualColorGains;
        MEMCPY_S(paResults.color_conversion_matrix,
            sizeof(paResults.color_conversion_matrix),
            reqState.aiqInputParams.manualColorTransform,
            sizeof(reqState.aiqInputParams.manualColorTransform));
        paResults.preferred_acm = nullptr;

    }

    status = mAwbState->processResult(reqState.captureSettings->aiqResults.awbResults,
                                      *reqState.ctrlUnitResult);

    return status;
}

/*
 * Tonemap conversions or overwrites for CONTRAST_CURVE, GAMMA_VALUE, and
 * PRESET_CURVE modes
 */
status_t AAARunner::applyTonemaps(RequestCtrlState &reqState)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);
    status_t status = OK;

    /*
     * Normal use-case is the automatic modes, and we need not do anything here
     */
    if (reqState.captureSettings->tonemapMode == ANDROID_TONEMAP_MODE_FAST ||
        reqState.captureSettings->tonemapMode == ANDROID_TONEMAP_MODE_HIGH_QUALITY) {
        // automatic modes, gbce output is used as-is
        return OK;
    }

    ia_aiq_gbce_results &results = reqState.captureSettings->aiqResults.gbceResults;
    int lutSize = results.gamma_lut_size;

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
        float *srcR = reqState.rGammaLut;
        float *srcG = reqState.gGammaLut;
        float *srcB = reqState.bGammaLut;
        size_t srcLenR = reqState.rGammaLutSize;
        size_t srcLenG = reqState.gGammaLutSize;
        size_t srcLenB = reqState.bGammaLutSize;

        if (srcLenR >= 4 && (srcLenR == srcLenG) && (srcLenR == srcLenB)) {
            // The Android tonemap is 2d, but here we assume the input values to
            // be linearly spaced. If they are not, well, our broken
            // implementation is just a bit more broken.

            // allocate a linear spaced lut for half the input size, discarding
            // in-values
            size_t srcLutSize = srcLenG / 2;
            float srcLut[srcLutSize];
            // pick the out-values in srcLut, skip the in-values, calculate sums
            float sumR = 0;
            float sumG = 0;
            float sumB = 0;
            for (size_t i = 0; i < srcLutSize; i++) {
                int srcIndex = i * 2 + 1;
                srcLut[i] = srcG[srcIndex];
                sumR += srcR[srcIndex];
                sumG += srcG[srcIndex];
                sumB += srcB[srcIndex];
            }

            // calculate averages
            float averageR = sumR / srcLutSize;
            float averageG = sumG / srcLutSize;
            float averageB = sumB / srcLutSize;

            float minAverage = MIN3(averageR, averageG, averageB);

            if (minAverage > EPSILON) {
                // adjust gains to try to mimic per-channel controls a bit
                reqState.aiqInputParams.manualColorGains.r  *= (averageR / minAverage);
                reqState.aiqInputParams.manualColorGains.gr *= (averageG / minAverage);
                reqState.aiqInputParams.manualColorGains.gb *= (averageG / minAverage);
                reqState.aiqInputParams.manualColorGains.b  *= (averageB / minAverage);
            }

            // interpolate to result lut, using the G channel srcLut
            interpolateArray(srcLut, srcLutSize, results.g_gamma_lut, lutSize);
        }
    }

    /*
     * Gamma value and preset curve modes. Generated on the fly based on the
     * current lut size.
     */
    if (reqState.captureSettings->tonemapMode == ANDROID_TONEMAP_MODE_GAMMA_VALUE) {
        float gamma = reqState.captureSettings->gammaValue;
        if (fabs(gamma) >= EPSILON) {
            for (int i = 0; i < lutSize; i++) {
                results.g_gamma_lut[i] = pow(i / float(lutSize), 1 / gamma);
            }
        } else {
            LOGE("Bad gamma");
            status = BAD_VALUE;
        }
    }

    if (reqState.captureSettings->tonemapMode == ANDROID_TONEMAP_MODE_PRESET_CURVE) {
        if (reqState.captureSettings->presetCurve == ANDROID_TONEMAP_PRESET_CURVE_SRGB) {
            for (int i = 0; i < lutSize; i++) {
                if (i / (lutSize -1)  < 0.0031308)
                    results.g_gamma_lut[i] = 12.92 * (i / (lutSize -1));
                else
                    results.g_gamma_lut[i] = 1.055 * pow(i / float(lutSize - 1), 1 / 2.4) - 0.055;
            }
        }
        if (reqState.captureSettings->presetCurve == ANDROID_TONEMAP_PRESET_CURVE_REC709) {
            for (int i = 0; i < lutSize; i++) {
                if (i / (lutSize - 1) < 0.018)
                    results.g_gamma_lut[i] = 4.5 * (i / (lutSize -1));
                else
                    results.g_gamma_lut[i] = 1.099 * pow(i / float(lutSize - 1), 0.45) - 0.099;
            }
        }
    }

    // make all luts the same with memcpy, hw only really supports one
    MEMCPY_S(results.b_gamma_lut, lutSize * sizeof(float),
        results.g_gamma_lut, lutSize * sizeof(float));
    MEMCPY_S(results.r_gamma_lut, lutSize * sizeof(float),
        results.g_gamma_lut, lutSize * sizeof(float));

    return status;
}

inline float AAARunner::interpolate(float pos, const float *src, int srcSize) const
{
    if (pos <= 0)
        return src[0];

    if (pos >= srcSize - 1)
        return src[srcSize - 1];

    int i = int(pos);

    return src[i] + (pos - i) * (src[i + 1] - src[i]);
}

void AAARunner::interpolateArray(const float *src, int srcSize, float *dst, int dstSize) const
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


/**
 * This function calculates the Neutral Color Point based on previously
 * calculated AWB results. The Neutral Color Point is returned as a dynamic
 * tag to the framework.
 *
 * \param[in,out] reqCfg AWB Results (in), Neutral Color Point (out)
 */
status_t AAARunner::updateNeutralColorPoint(RequestCtrlState &reqAiqCfg)
{
    LOG2("@%s", __FUNCTION__);
    float whitePoint[] = { 1.0, 1.0, 1.0 };
    int tableSize = sizeof(whitePoint) / sizeof(float);

    ia_aiq_awb_results &awbResults =
                reqAiqCfg.captureSettings->aiqResults.awbResults;

    if (fabs(awbResults.final_r_per_g) > EPSILON &&
        fabs(awbResults.final_b_per_g) > EPSILON) {
        // color order defined by Android: R,G,B
        float max_chroma = MAX(MAX(awbResults.final_r_per_g, 1.0),
                                   awbResults.final_b_per_g);
        whitePoint[0] = max_chroma / awbResults.final_r_per_g;
        whitePoint[1] = max_chroma;
        whitePoint[2] = max_chroma / awbResults.final_b_per_g;
        LOG2("white point RGB(%f, %f, %f)",
             whitePoint[0], whitePoint[1], whitePoint[2]);
    }

    camera_metadata_rational_t neutralColorPoint[tableSize];
    for (int i = 0; i < tableSize; i++) {
        neutralColorPoint[i].numerator = whitePoint[i];
        neutralColorPoint[i].denominator = 1;
    }

    reqAiqCfg.ctrlUnitResult->update(ANDROID_SENSOR_NEUTRAL_COLOR_POINT,
                                     neutralColorPoint, tableSize);
    //# ANDROID_METADATA_Dynamic android.sensor.neutralColorPoint done

    return NO_ERROR;
}

void AAARunner::applyDigitalGain(RequestCtrlState &reqState, float digitalGain) const
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);
    ia_aiq_sa_results &saResults = reqState.captureSettings->aiqResults.saResults;
    uint32_t lscSize = saResults.width * saResults.height;
    for (uint32_t i = 0; i < lscSize; i++) {
        saResults.channel_b[i]  *= digitalGain;
        saResults.channel_r[i]  *= digitalGain;
        saResults.channel_gb[i] *= digitalGain;
        saResults.channel_gr[i] *= digitalGain;
    }
}

status_t AAARunner::allocateLscTable(int tableSize)
{
    if (tableSize <= 0) {
        LOGE("Allocate LSC table failed");
        return BAD_VALUE;
    }

    status_t status = mLatestResults.allocateLsc(tableSize);
    status |= mPrecaptureResults.allocateLsc(tableSize);
    mPrecaptureResults.init();
    mLatestResults.init();
    initLsc(mPrecaptureResults, tableSize);
    initLsc(mLatestResults, tableSize);

    return status;
}

void AAARunner::initLsc(AiqResults &results, uint32_t lscSize) const
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);
    ia_aiq_sa_results &saResults = results.saResults;
    for (uint32_t i = 0; i < lscSize; i++) {
        saResults.channel_b[i]  = 1.0f;
        saResults.channel_r[i]  = 1.0f;
        saResults.channel_gb[i] = 1.0f;
        saResults.channel_gr[i] = 1.0f;
    }
}


} /* namespace intel */
} /* namespace cros */
