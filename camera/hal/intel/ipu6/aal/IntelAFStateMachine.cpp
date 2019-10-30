/*
 * Copyright (C) 2015-2020 Intel Corporation
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

#define LOG_TAG "IntelAFStateMachine"

#include "IntelAFStateMachine.h"

#include "Errors.h"
#include "HALv3Utils.h"
#include "Utils.h"

namespace camera3 {

/**
 * AF timeouts. Together these will make:
 * timeout if: [MIN_AF_TIMEOUT - MAX_AF_FRAME_COUNT_TIMEOUT - MAX_AF_TIMEOUT]
 * which translates to 2-4 seconds with the current values. Actual timeout value
 * will depend on the FPS. E.g. >30FPS = 2s, 20FPS = 3s, <15FPS = 4s.
 */

/**
 * MAX_AF_TIMEOUT
 * Maximum time we allow the AF to iterate without a result.
 * This timeout is the last resort, for very low FPS operation.
 * Units are in microseconds.
 * 4 seconds is a compromise between CTS & ITS. ITS allows for 10 seconds for
 * 3A convergence. CTS1 allows only 5, but it doesn't require convergence, just
 * a conclusion. We reserve one second for latencies to be safe. This makes the
 * timeout 5 (cts1) - 1 (latency safety) = 4 seconds = 4000000us.
 */
static const long int MAX_AF_TIMEOUT = 4000000;  // 4 seconds

/**
 * MIN_AF_TIMEOUT
 * For very high FPS use cases, we want to anyway allow some time for moving the
 * lens.
 */
static const long int MIN_AF_TIMEOUT = 2000000;  // 2 seconds

/**
 * MAX_AF_FRAME_COUNT_TIMEOUT
 * Maximum time we allow the AF to iterate without a result.
 * Based on frames, as the AF algorithm itself needs frames for its operation,
 * not just time, and the FPS varies.
 * This is the timeout for normal operation, and translates to 2 seconds
 * if FPS is 30.
 */
static const int MAX_AF_FRAME_COUNT_TIMEOUT = 60;  // 2 seconds if 30fps

IntelAFStateMachine::IntelAFStateMachine(int cameraId) : mCameraId(cameraId) {
    LOG1("%s mCameraId %d", __func__, mCameraId);
    mCurrentAfMode = &mAutoMode;
    mLastAfControls = {ANDROID_CONTROL_AF_MODE_AUTO, ANDROID_CONTROL_AF_TRIGGER_IDLE};
}

IntelAFStateMachine::~IntelAFStateMachine() {
    LOG1("%s mCameraId %d", __func__, mCameraId);
}

int IntelAFStateMachine::processTriggers(uint8_t afTrigger, uint8_t afMode) {
    if (afMode != mLastAfControls.afMode) {
        LOG1("Change of AF mode from %d to %d", mLastAfControls.afMode, afMode);

        switch (afMode) {
            case ANDROID_CONTROL_AF_MODE_AUTO:
            case ANDROID_CONTROL_AF_MODE_MACRO:
                mCurrentAfMode = &mAutoMode;
                break;
            case ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO:
            case ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE:
                mCurrentAfMode = &mContinuousPictureMode;
                break;
            case ANDROID_CONTROL_AF_MODE_OFF:
                mCurrentAfMode = &mOffMode;
                break;
            default:
                LOGE("INVALID AF mode requested defaulting to AUTO");
                mCurrentAfMode = &mAutoMode;
                break;
        }
        mCurrentAfMode->resetState();
    }
    mLastAfControls.afTrigger = afTrigger;
    mLastAfControls.afMode = afMode;

    LOG2("%s: afMode %d", __func__, mLastAfControls.afMode);
    return mCurrentAfMode->processTriggers(afTrigger, afMode);
}

int IntelAFStateMachine::processResult(int afState, bool lensMoving,
                                       android::CameraMetadata* result) {
    CheckError(!mCurrentAfMode, icamera::UNKNOWN_ERROR, "Invalid AF mode");
    CheckError(!result, icamera::UNKNOWN_ERROR, "%s, result is nullptr", __func__);

    return mCurrentAfMode->processResult(afState, lensMoving, result);
}

/**
 * updateDefaults
 *
 * Used in case of error in the algorithm or fixed focus sensor
 * In case of fixed focus sensor we always report locked
 */
int IntelAFStateMachine::updateDefaults(android::CameraMetadata* result, bool fixedFocus) const {
    CheckError(!result, icamera::UNKNOWN_ERROR, "%s, result is nullptr", __func__);
    mCurrentAfMode->updateResult(result);
    uint8_t defaultState = ANDROID_CONTROL_AF_STATE_INACTIVE;
    if (fixedFocus) defaultState = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;

    result->update(ANDROID_CONTROL_AF_STATE, &defaultState, 1);

    return icamera::OK;
}

/******************************************************************************
 * AF MODE   -  BASE
 ******************************************************************************/
IntelAfModeBase::IntelAfModeBase()
        : mCurrentAfState(ANDROID_CONTROL_AF_STATE_INACTIVE),
          mLensState(ANDROID_LENS_STATE_STATIONARY),
          mLastActiveTriggerTime(0),
          mFramesSinceTrigger(0) {
    LOG1("%s", __func__);
    mLastAfControls = {ANDROID_CONTROL_AF_MODE_AUTO, ANDROID_CONTROL_AF_TRIGGER_IDLE};
}

/**
 * processTriggers
 *
 * This method is called BEFORE auto focus algorithm has RUN
 * Input parameters are pre-filled by the Intel3APlus::fillAfInputParams()
 * by parsing the request settings.
 * Other parameters from the capture request settings not filled in the input
 * params structure is passed as argument
 */
int IntelAfModeBase::processTriggers(uint8_t afTrigger, uint8_t afMode) {
    LOG2("%s", __func__);

    if (afTrigger == ANDROID_CONTROL_AF_TRIGGER_START) {
        resetTrigger(icamera::CameraUtils::systemTime() / 1000);
        LOG1("AF TRIGGER START");
    } else if (afTrigger == ANDROID_CONTROL_AF_TRIGGER_CANCEL) {
        LOG1("AF TRIGGER CANCEL");
        resetTrigger(0);
    }
    mLastAfControls.afTrigger = afTrigger;
    mLastAfControls.afMode = afMode;
    return icamera::OK;
}

void IntelAfModeBase::updateResult(android::CameraMetadata* results) {
    CheckError(!results, VOID_VALUE, "%s, result is nullptr", __func__);
    LOG2("%s", __func__);

    LOG2("%s afMode = %d state = %d", __func__, mLastAfControls.afMode, mCurrentAfState);

    results->update(ANDROID_CONTROL_AF_MODE, &mLastAfControls.afMode, 1);
    //# METADATA_Dynamic control.afTrigger done
    results->update(ANDROID_CONTROL_AF_TRIGGER, &mLastAfControls.afTrigger, 1);
    //# METADATA_Dynamic control.afState done
    results->update(ANDROID_CONTROL_AF_STATE, &mCurrentAfState, 1);
    /**
     * LENS STATE update
     */
    //# METADATA_Dynamic lens.state Done
    results->update(ANDROID_LENS_STATE, &mLensState, 1);
}

void IntelAfModeBase::resetTrigger(usecs_t triggerTime) {
    mLastActiveTriggerTime = triggerTime;
    mFramesSinceTrigger = 0;
}

void IntelAfModeBase::resetState() {
    mCurrentAfState = ANDROID_CONTROL_AF_STATE_INACTIVE;
}

void IntelAfModeBase::checkIfFocusTimeout() {
    // give up if AF was iterating for too long
    if (mLastActiveTriggerTime != 0) {
        mFramesSinceTrigger++;
        usecs_t now = icamera::CameraUtils::systemTime() / 1000;
        usecs_t timeSinceTriggered = now - mLastActiveTriggerTime;
        if (mCurrentAfState != ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED) {
            /**
             * Timeout IF either time has passed beyond MAX_AF_TIMEOUT
             *                         OR
             * Enough frames have been processed and time has passed beyond
             * MIN_AF_TIMEOUT
             */
            if (timeSinceTriggered > MAX_AF_TIMEOUT ||
                (mFramesSinceTrigger > MAX_AF_FRAME_COUNT_TIMEOUT &&
                 timeSinceTriggered > MIN_AF_TIMEOUT)) {
                resetTrigger(0);
                mCurrentAfState = ANDROID_CONTROL_AF_STATE_NOT_FOCUSED_LOCKED;
            }
        }
    }
}

/******************************************************************************
 * AF MODE   -  OFF
 ******************************************************************************/

IntelAFModeOff::IntelAFModeOff() : IntelAfModeBase() {
    LOG1("%s", __func__);
}

int IntelAFModeOff::processTriggers(uint8_t afTrigger, uint8_t afMode) {
    LOG2("%s", __func__);

    mLastAfControls.afTrigger = afTrigger;
    mLastAfControls.afMode = afMode;
    return icamera::OK;
}

int IntelAFModeOff::processResult(int afState, bool lensMoving, android::CameraMetadata* result) {
    CheckError(!result, icamera::UNKNOWN_ERROR, "%s, result is nullptr", __func__);
    /**
     * IN MANUAL and EDOF AF state never changes
     */
    LOG2("%s", __func__);

    mCurrentAfState = ANDROID_CONTROL_AF_STATE_INACTIVE;
    mLensState = lensMoving ? ANDROID_LENS_STATE_MOVING : ANDROID_LENS_STATE_STATIONARY;
    updateResult(result);

    return icamera::OK;
}

/******************************************************************************
 * AF MODE   -  AUTO
 ******************************************************************************/

IntelAFModeAuto::IntelAFModeAuto() : IntelAfModeBase() {
    LOG1("%s", __func__);
}

int IntelAFModeAuto::processTriggers(uint8_t afTrigger, uint8_t afMode) {
    LOG2("%s", __func__);

    IntelAfModeBase::processTriggers(afTrigger, afMode);

    // Override AF state if we just got an AF TRIGGER Start
    // This is only valid for the AUTO/MACRO state machine
    if (mLastAfControls.afTrigger == ANDROID_CONTROL_AF_TRIGGER_START) {
        mCurrentAfState = ANDROID_CONTROL_AF_STATE_ACTIVE_SCAN;
        LOG2("@%s AF state ACTIVE_SCAN (trigger start)", __PRETTY_FUNCTION__);
    } else if (mLastAfControls.afTrigger == ANDROID_CONTROL_AF_TRIGGER_CANCEL) {
        mCurrentAfState = ANDROID_CONTROL_AF_STATE_INACTIVE;
        LOG2("@%s AF state INACTIVE (trigger cancel)", __PRETTY_FUNCTION__);
    }

    return icamera::OK;
}

int IntelAFModeAuto::processResult(int afState, bool lensMoving, android::CameraMetadata* result) {
    CheckError(!result, icamera::UNKNOWN_ERROR, "%s, result is nullptr", __func__);
    LOG2("%s", __func__);
    mLensState = lensMoving ? ANDROID_LENS_STATE_MOVING : ANDROID_LENS_STATE_STATIONARY;

    if (mLastActiveTriggerTime != 0) {
        switch (afState) {
            case icamera::AF_STATE_LOCAL_SEARCH:
            case icamera::AF_STATE_EXTENDED_SEARCH:
                LOG2("@%s AF state SCANNING", __PRETTY_FUNCTION__);
                break;
            case icamera::AF_STATE_SUCCESS:
                mCurrentAfState = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
                resetTrigger(0);
                LOG2("@%s AF state FOCUSED_LOCKED", __PRETTY_FUNCTION__);
                break;
            case icamera::AF_STATE_FAIL:
                mCurrentAfState = ANDROID_CONTROL_AF_STATE_NOT_FOCUSED_LOCKED;
                resetTrigger(0);
                LOG2("@%s AF state NOT_FOCUSED_LOCKED", __PRETTY_FUNCTION__);
                break;
            default:
            case icamera::AF_STATE_IDLE:
                LOG2("@%s AF state INACTIVE", __PRETTY_FUNCTION__);
                break;
        }
    }

    checkIfFocusTimeout();

    updateResult(result);

    return icamera::OK;
}

/******************************************************************************
 * AF MODE   -  CONTINUOUS PICTURE
 ******************************************************************************/

IntelAFModeContinuousPicture::IntelAFModeContinuousPicture() : IntelAfModeBase() {
    LOG1("%s", __func__);
}

int IntelAFModeContinuousPicture::processTriggers(uint8_t afTrigger, uint8_t afMode) {
    LOG2("%s", __func__);

    IntelAfModeBase::processTriggers(afTrigger, afMode);

    // Override AF state if we just got an AF TRIGGER CANCEL
    if (mLastAfControls.afTrigger == ANDROID_CONTROL_AF_TRIGGER_CANCEL) {
        /* Scan is supposed to be restarted, which we try by triggering a new
         * scan. (see IntelAFStateMachine::processTriggers)
         * This however, doesn't do anything at all, because AIQ does not
         * want to play ball, at least yet.
         *
         * We can skip state transitions when allowed by the state
         * machine documentation, so skip INACTIVE, also skip PASSIVE_SCAN if
         * possible and go directly to either PASSIVE_FOCUSED or UNFOCUSED
         *
         * TODO: Remove this switch-statement, once triggering a scan starts to
         * work. We could go directly to PASSIVE_SCAN always then, because a
         * scan is really happening. Now it is not.
         */
        switch (mCurrentAfState) {
            case ANDROID_CONTROL_AF_STATE_PASSIVE_SCAN:
            case ANDROID_CONTROL_AF_STATE_NOT_FOCUSED_LOCKED:
                mCurrentAfState = ANDROID_CONTROL_AF_STATE_PASSIVE_UNFOCUSED;
                break;
            case ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED:
                mCurrentAfState = ANDROID_CONTROL_AF_STATE_PASSIVE_FOCUSED;
                break;
            default:
                mCurrentAfState = ANDROID_CONTROL_AF_STATE_PASSIVE_SCAN;
                break;
        }
    }
    /* Override AF state if we just got an AF TRIGGER START, this will stop
     * the scan as intended in the state machine documentation (see
     * IntelAFStateMachine::processTriggers)
     */
    if (mLastAfControls.afTrigger == ANDROID_CONTROL_AF_TRIGGER_START) {
        if (mCurrentAfState == ANDROID_CONTROL_AF_STATE_PASSIVE_FOCUSED)
            mCurrentAfState = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
        else if (mCurrentAfState == ANDROID_CONTROL_AF_STATE_PASSIVE_UNFOCUSED ||
                 mCurrentAfState == ANDROID_CONTROL_AF_STATE_PASSIVE_SCAN)
            mCurrentAfState = ANDROID_CONTROL_AF_STATE_NOT_FOCUSED_LOCKED;
    }

    return icamera::OK;
}

int IntelAFModeContinuousPicture::processResult(int afState, bool lensMoving,
                                                android::CameraMetadata* result) {
    CheckError(!result, icamera::UNKNOWN_ERROR, "%s, result is nullptr", __func__);
    LOG2("%s", __func__);
    mLensState = lensMoving ? ANDROID_LENS_STATE_MOVING : ANDROID_LENS_STATE_STATIONARY;

    // state transition from locked state are only allowed via triggers, which
    // are handled in the currentAFMode processTriggers() and below in this
    // function.
    if (mCurrentAfState != ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED &&
        mCurrentAfState != ANDROID_CONTROL_AF_STATE_NOT_FOCUSED_LOCKED) {
        switch (afState) {
            case icamera::AF_STATE_LOCAL_SEARCH:
            case icamera::AF_STATE_EXTENDED_SEARCH:
                LOG2("@%s AF state SCANNING", __PRETTY_FUNCTION__);
                mCurrentAfState = ANDROID_CONTROL_AF_STATE_PASSIVE_SCAN;
                break;
            case icamera::AF_STATE_SUCCESS:
                if (mLastActiveTriggerTime == 0) {
                    mCurrentAfState = ANDROID_CONTROL_AF_STATE_PASSIVE_FOCUSED;
                    LOG2("@%s AF state PASSIVE_FOCUSED", __PRETTY_FUNCTION__);
                } else {
                    resetTrigger(0);
                    mCurrentAfState = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
                    LOG2("@%s AF state FOCUSED_LOCKED", __PRETTY_FUNCTION__);
                }
                break;
            case icamera::AF_STATE_FAIL:
                if (mLastActiveTriggerTime == 0) {
                    mCurrentAfState = ANDROID_CONTROL_AF_STATE_PASSIVE_UNFOCUSED;
                    LOG2("@%s AF state PASSIVE_UNFOCUSED", __PRETTY_FUNCTION__);
                } else {
                    resetTrigger(0);
                    mCurrentAfState = ANDROID_CONTROL_AF_STATE_NOT_FOCUSED_LOCKED;
                    LOG2("@%s AF state NOT_FOCUSED_LOCKED", __PRETTY_FUNCTION__);
                }
                break;
            default:
            case icamera::AF_STATE_IDLE:
                if (mCurrentAfState == ANDROID_CONTROL_AF_STATE_INACTIVE) {
                    mCurrentAfState = ANDROID_CONTROL_AF_STATE_PASSIVE_UNFOCUSED;
                    LOG2("@%s AF state PASSIVE_UNFOCUSED (idle)", __PRETTY_FUNCTION__);
                }
                break;
        }
    }

    checkIfFocusTimeout();

    updateResult(result);

    return icamera::OK;
}

}  // namespace camera3
