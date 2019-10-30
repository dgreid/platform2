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

#define LOG_TAG "IntelAWBStateMachine"

#include "IntelAWBStateMachine.h"

#include "Errors.h"
#include "HALv3Utils.h"
#include "Utils.h"

namespace camera3 {

IntelAWBStateMachine::IntelAWBStateMachine(int aCameraId)
        : mCameraId(aCameraId),
          mLastControlMode(0),
          mLastSceneMode(0),
          mCurrentAwbMode(NULL) {
    LOG1("%s mCameraId %d", __func__, mCameraId);
    mCurrentAwbMode = &mAutoMode;
    CLEAR(mLastAwbControls);
    mLastAwbControls.awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
}

IntelAWBStateMachine::~IntelAWBStateMachine() {
    LOG1("%s mCameraId %d", __func__, mCameraId);
}

int IntelAWBStateMachine::processState(uint8_t controlMode, uint8_t sceneMode,
                                       const AwbControls& awbControls) {
    if (controlMode == ANDROID_CONTROL_MODE_OFF) {
        mCurrentAwbMode = &mOffMode;

        if (controlMode != mLastControlMode)
            LOG1("%s: Set AWB offMode: controlMode = %d, awbMode = %d", __func__, controlMode,
                 awbControls.awbMode);
    } else {
        if (awbControls.awbMode == ANDROID_CONTROL_AWB_MODE_OFF) {
            mCurrentAwbMode = &mOffMode;
            if (awbControls.awbMode != mLastAwbControls.awbMode)
                LOG1("%s: Set AWB offMode: controlMode = %d, awbMode = %d", __func__, controlMode,
                     awbControls.awbMode);
        } else {
            mCurrentAwbMode = &mAutoMode;
            if (awbControls.awbMode != mLastAwbControls.awbMode)
                LOG1("%s: Set AWB autoMode: controlMode = %d, awbMode = %d", __func__, controlMode,
                     awbControls.awbMode);
        }
    }

    mLastAwbControls = awbControls;
    mLastSceneMode = sceneMode;
    mLastControlMode = controlMode;
    return mCurrentAwbMode->processState(controlMode, sceneMode, awbControls);
}

int IntelAWBStateMachine::processResult(bool converged, android::CameraMetadata* result) {
    CheckError(!mCurrentAwbMode, icamera::UNKNOWN_ERROR, "Invalid AWB mode");
    CheckError(!result, icamera::UNKNOWN_ERROR, "%s, result is nullptr", __func__);

    return mCurrentAwbMode->processResult(converged, result);
}

/******************************************************************************
 * AWB MODE   -  BASE
 ******************************************************************************/
IntelAWBModeBase::IntelAWBModeBase()
        : mLastControlMode(0),
          mLastSceneMode(0),
          mCurrentAwbState(ANDROID_CONTROL_AWB_STATE_INACTIVE) {
    LOG1("%s", __func__);

    CLEAR(mLastAwbControls);
}

void IntelAWBModeBase::updateResult(android::CameraMetadata* results) {
    CheckError(!results, VOID_VALUE, "%s, result is nullptr", __func__);
    LOG2("%s: current AWB state is: %d", __func__, mCurrentAwbState);

    //# METADATA_Dynamic control.awbMode done
    results->update(ANDROID_CONTROL_AWB_MODE, &mLastAwbControls.awbMode, 1);
    //# METADATA_Dynamic control.awbLock done
    results->update(ANDROID_CONTROL_AWB_LOCK, &mLastAwbControls.awbLock, 1);
    //# METADATA_Dynamic control.awbState done
    results->update(ANDROID_CONTROL_AWB_STATE, &mCurrentAwbState, 1);
}

void IntelAWBModeBase::resetState() {
    LOG2("%s", __func__);

    mCurrentAwbState = ANDROID_CONTROL_AWB_STATE_INACTIVE;
}

/******************************************************************************
 * AWB MODE   -  OFF
 ******************************************************************************/

IntelAWBModeOff::IntelAWBModeOff() : IntelAWBModeBase() {
    LOG1("%s", __func__);
}

int IntelAWBModeOff::processState(uint8_t controlMode, uint8_t sceneMode,
                                  const AwbControls& awbControls) {
    LOG2("%s", __func__);

    int ret = icamera::OK;

    mLastAwbControls = awbControls;
    mLastSceneMode = sceneMode;
    mLastControlMode = controlMode;

    if (controlMode == ANDROID_CONTROL_MODE_OFF ||
        awbControls.awbMode == ANDROID_CONTROL_AWB_MODE_OFF) {
        resetState();
    } else {
        LOGE("AWB State machine should not be OFF! - Fix bug");
        ret = icamera::UNKNOWN_ERROR;
    }

    return ret;
}

int IntelAWBModeOff::processResult(bool converged, android::CameraMetadata* result) {
    UNUSED(converged);
    CheckError(!result, icamera::UNKNOWN_ERROR, "%s, result is nullptr", __func__);
    LOG2("%s", __func__);

    mCurrentAwbState = ANDROID_CONTROL_AWB_STATE_INACTIVE;
    updateResult(result);

    return icamera::OK;
}

/******************************************************************************
 * AWB MODE   -  AUTO
 ******************************************************************************/

IntelAWBModeAuto::IntelAWBModeAuto() : IntelAWBModeBase() {
    LOG1("%s", __func__);
}

int IntelAWBModeAuto::processState(uint8_t controlMode, uint8_t sceneMode,
                                   const AwbControls& awbControls) {
    if (controlMode != mLastControlMode) {
        LOG1("%s: control mode has changed %d -> %d, reset AWB State", __func__, mLastControlMode,
             controlMode);
        resetState();
    }

    if (awbControls.awbLock == ANDROID_CONTROL_AWB_LOCK_ON) {
        mCurrentAwbState = ANDROID_CONTROL_AWB_STATE_LOCKED;
    } else if (awbControls.awbMode != mLastAwbControls.awbMode ||
               (controlMode == ANDROID_CONTROL_MODE_USE_SCENE_MODE &&
                sceneMode != mLastSceneMode)) {
        resetState();
    } else {
        switch (mCurrentAwbState) {
            case ANDROID_CONTROL_AWB_STATE_LOCKED:
                mCurrentAwbState = ANDROID_CONTROL_AWB_STATE_INACTIVE;
                break;
            case ANDROID_CONTROL_AWB_STATE_INACTIVE:
            case ANDROID_CONTROL_AWB_STATE_SEARCHING:
            case ANDROID_CONTROL_AWB_STATE_CONVERGED:
                // do nothing
                break;
            default:
                LOGE("Invalid AWB state!, State set to INACTIVE");
                mCurrentAwbState = ANDROID_CONTROL_AWB_STATE_INACTIVE;
        }
    }
    mLastAwbControls = awbControls;
    mLastSceneMode = sceneMode;
    mLastControlMode = controlMode;
    return icamera::OK;
}

int IntelAWBModeAuto::processResult(bool converged, android::CameraMetadata* result) {
    CheckError(!result, icamera::UNKNOWN_ERROR, "%s, result is nullptr", __func__);
    switch (mCurrentAwbState) {
        case ANDROID_CONTROL_AWB_STATE_LOCKED:
            // do nothing
            break;
        case ANDROID_CONTROL_AWB_STATE_INACTIVE:
        case ANDROID_CONTROL_AWB_STATE_SEARCHING:
        case ANDROID_CONTROL_AWB_STATE_CONVERGED:
            if (converged)
                mCurrentAwbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
            else
                mCurrentAwbState = ANDROID_CONTROL_AWB_STATE_SEARCHING;
            break;
        default:
            LOGE("invalid AWB state!, State set to INACTIVE");
            mCurrentAwbState = ANDROID_CONTROL_AWB_STATE_INACTIVE;
    }

    updateResult(result);

    return icamera::OK;
}

}  // namespace camera3
