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

#define LOG_TAG "IntelAEStateMachine"

#include "IntelAEStateMachine.h"

#include "Errors.h"
#include "HALv3Utils.h"
#include "Utils.h"

namespace camera3 {

IntelAEStateMachine::IntelAEStateMachine(int cameraId)
        : mCameraId(cameraId),
          mLastControlMode(0),
          mLastSceneMode(0),
          mCurrentAeMode(NULL) {
    LOG1("%s mCameraId %d", __func__, mCameraId);
    mCurrentAeMode = &mAutoMode;
    CLEAR(mLastAeControls);
    mLastAeControls.aeMode = ANDROID_CONTROL_AE_MODE_ON;
}

IntelAEStateMachine::~IntelAEStateMachine() {
    LOG1("%s mCameraId %d", __func__, mCameraId);
}

/**
 * Process states in input stage before the AE is run.
 * It is initializing the current state if input
 * parameters have an influence.
 *
 * \param[IN] controlMode: control.controlMode
 * \param[IN] sceneMode: control.sceneMode
 * \param[IN] aeControls: set of control.<ae>
 */
int IntelAEStateMachine::processState(uint8_t controlMode, uint8_t sceneMode,
                                      const AeControls& aeControls) {
    if (controlMode == ANDROID_CONTROL_MODE_OFF) {
        LOG2("%s: Set AE offMode: controlMode = %d, aeMode = %d", __func__, controlMode,
             aeControls.aeMode);
        mCurrentAeMode = &mOffMode;
    } else {
        if (aeControls.aeMode == ANDROID_CONTROL_AE_MODE_OFF) {
            mCurrentAeMode = &mOffMode;
            LOG2("%s: Set AE offMode: controlMode = %d, aeMode = %d", __func__, controlMode,
                 aeControls.aeMode);
        } else {
            LOG2("%s: Set AE AutoMode: controlMode = %d, aeMode = %d", __func__, controlMode,
                 aeControls.aeMode);
            mCurrentAeMode = &mAutoMode;
        }
    }

    mLastAeControls = aeControls;
    mLastSceneMode = sceneMode;
    mLastControlMode = controlMode;

    return mCurrentAeMode->processState(controlMode, sceneMode, aeControls);
}

/**
 * Process results and define output state after the AE is run
 *
 * \param[IN] aeConverged: from the ae result
 * \param[IN] results: cameraMetadata to write dynamic tags.
 */
int IntelAEStateMachine::processResult(bool aeConverged, android::CameraMetadata* result) {
    CheckError(!mCurrentAeMode, icamera::UNKNOWN_ERROR, "Invalid AE mode");
    CheckError(!result, icamera::UNKNOWN_ERROR, "%s, result is nullptr", __func__);

    return mCurrentAeMode->processResult(aeConverged, result);
}

/******************************************************************************
 * AE MODE   -  BASE
 ******************************************************************************/
IntelAEModeBase::IntelAEModeBase()
        : mLastControlMode(0),
          mLastSceneMode(0),
          mEvChanged(false),
          mLastAeConvergedFlag(false),
          mAeRunCount(0),
          mAeConvergedCount(0),
          mCurrentAeState(ANDROID_CONTROL_AE_STATE_INACTIVE) {
    LOG1("%s", __func__);
    CLEAR(mLastAeControls);
}

void IntelAEModeBase::updateResult(android::CameraMetadata* results) {
    CheckError(!results, VOID_VALUE, "%s, result is nullptr", __func__);
    LOG2("%s: current AE state is: %d", __func__, mCurrentAeState);

    //# METADATA_Dynamic control.aeMode done
    results->update(ANDROID_CONTROL_AE_MODE, &mLastAeControls.aeMode, 1);
    //# METADATA_Dynamic control.aeLock done
    results->update(ANDROID_CONTROL_AE_LOCK, &mLastAeControls.aeLock, 1);
    //# METADATA_Dynamic control.aePrecaptureTrigger done
    results->update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &mLastAeControls.aePreCaptureTrigger, 1);
    //# METADATA_Dynamic control.aeState done
    results->update(ANDROID_CONTROL_AE_STATE, &mCurrentAeState, 1);
}

void IntelAEModeBase::resetState() {
    LOG2("%s", __func__);

    mCurrentAeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
    mLastAeConvergedFlag = false;
    mAeRunCount = 0;
    mAeConvergedCount = 0;
}

/******************************************************************************
 * AE MODE   -  OFF
 ******************************************************************************/

IntelAEModeOff::IntelAEModeOff() : IntelAEModeBase() {
    LOG1("%s", __func__);
}

int IntelAEModeOff::processState(uint8_t controlMode, uint8_t sceneMode,
                                 const AeControls& aeControls) {
    LOG2("%s", __func__);

    mLastAeControls = aeControls;
    mLastSceneMode = sceneMode;
    mLastControlMode = controlMode;

    if (controlMode == ANDROID_CONTROL_MODE_OFF ||
        aeControls.aeMode == ANDROID_CONTROL_AE_MODE_OFF) {
        resetState();
    } else {
        LOGE("AE State machine should not be OFF! - Fix bug");
        return icamera::UNKNOWN_ERROR;
    }

    return icamera::OK;
}

int IntelAEModeOff::processResult(bool aeConverged, android::CameraMetadata* result) {
    UNUSED(aeConverged);
    CheckError(!result, icamera::UNKNOWN_ERROR, "%s, result is nullptr", __func__);
    LOG2("%s", __func__);

    mCurrentAeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
    updateResult(result);

    return icamera::OK;
}

/******************************************************************************
 * AE MODE   -  AUTO
 ******************************************************************************/

IntelAEModeAuto::IntelAEModeAuto() : IntelAEModeBase() {
    LOG1("%s", __func__);
}

int IntelAEModeAuto::processState(uint8_t controlMode, uint8_t sceneMode,
                                  const AeControls& aeControls) {
    if (controlMode != mLastControlMode) {
        LOG1("%s: control mode has changed %d -> %d, reset AE State", __func__, controlMode,
             mLastControlMode);
        resetState();
    }

    if (aeControls.aeLock == ANDROID_CONTROL_AE_LOCK_ON) {
        // If ev compensation changes, we have to let the AE run until
        // convergence. Thus we need to figure out changes in compensation and
        // only change the state immediately to locked,
        // IF the EV did not change.
        if (mLastAeControls.evCompensation != aeControls.evCompensation) mEvChanged = true;

        if (!mEvChanged) mCurrentAeState = ANDROID_CONTROL_AE_STATE_LOCKED;
    } else if (aeControls.aeMode != mLastAeControls.aeMode ||
               (controlMode == ANDROID_CONTROL_MODE_USE_SCENE_MODE &&
                sceneMode != mLastSceneMode)) {
        resetState();
    } else {
        switch (mCurrentAeState) {
            case ANDROID_CONTROL_AE_STATE_LOCKED:
                mCurrentAeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
                break;
            case ANDROID_CONTROL_AE_STATE_SEARCHING:
            case ANDROID_CONTROL_AE_STATE_INACTIVE:
            case ANDROID_CONTROL_AE_STATE_CONVERGED:
            case ANDROID_CONTROL_AE_STATE_FLASH_REQUIRED:
            case ANDROID_CONTROL_AE_STATE_PRECAPTURE:
                if (aeControls.aePreCaptureTrigger == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START)
                    mCurrentAeState = ANDROID_CONTROL_AE_STATE_PRECAPTURE;

                if (aeControls.aePreCaptureTrigger == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_CANCEL)
                    mCurrentAeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
                break;
            default:
                LOGE("Invalid AE state!, State set to INACTIVE");
                mCurrentAeState = ANDROID_CONTROL_AE_STATE_INACTIVE;

                break;
        }
    }
    mLastAeControls = aeControls;
    mLastSceneMode = sceneMode;
    mLastControlMode = controlMode;
    return icamera::OK;
}

int IntelAEModeAuto::processResult(bool aeConverged, android::CameraMetadata* result) {
    CheckError(!result, icamera::UNKNOWN_ERROR, "%s, result is nullptr", __func__);
    switch (mCurrentAeState) {
        case ANDROID_CONTROL_AE_STATE_LOCKED:
            // do nothing
            break;
        case ANDROID_CONTROL_AE_STATE_INACTIVE:
        case ANDROID_CONTROL_AE_STATE_SEARCHING:
        case ANDROID_CONTROL_AE_STATE_CONVERGED:
        case ANDROID_CONTROL_AE_STATE_FLASH_REQUIRED:
            if (aeConverged) {
                mEvChanged = false;  // converged -> reset
                if (mLastAeControls.aeLock) {
                    mCurrentAeState = ANDROID_CONTROL_AE_STATE_LOCKED;
                } else {
                    mCurrentAeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
                }
            } else {
                mCurrentAeState = ANDROID_CONTROL_AE_STATE_SEARCHING;
            }
            break;
        case ANDROID_CONTROL_AE_STATE_PRECAPTURE:
            if (aeConverged) {
                mEvChanged = false;  // converged -> reset
                if (mLastAeControls.aeLock) {
                    mCurrentAeState = ANDROID_CONTROL_AE_STATE_LOCKED;
                } else {
                    mCurrentAeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
                }
            }  // here the else is staying at the same state.
            break;
        default:
            LOGE("Invalid AE state!, State set to INACTIVE");
            mCurrentAeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
            break;
    }

    if (aeConverged) {
        if (mLastAeConvergedFlag == true) {
            mAeConvergedCount++;
            LOG2("%s: AE converged for %d frames", __func__, mAeConvergedCount);
        } else {
            mAeConvergedCount = 1;
            LOG1("%s: AE converging -> converged, after running AE for %d times", __func__,
                 mAeRunCount);
        }
    } else {
        if (mLastAeConvergedFlag == true) {
            LOG1("%s: AE Converged -> converging", __func__);
            mAeRunCount = 1;
            mAeConvergedCount = 0;
        } else {
            mAeRunCount++;
            LOG2("%s: AE converging for %d frames", __func__, mAeRunCount);
        }
    }
    mLastAeConvergedFlag = aeConverged;

    updateResult(result);

    return icamera::OK;
}

}  // namespace camera3
