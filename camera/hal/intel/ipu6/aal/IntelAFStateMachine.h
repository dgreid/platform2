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

#pragma once

#include "HALv3Header.h"
#include "Utils.h"

namespace camera3 {

typedef int64_t usecs_t;

/**
 * \struct AfControls
 *
 * Control Modes saved and passed back to control unit after reading
 *
 */
struct AfControls {
    uint8_t afMode;    /**< AF_MODE */
    uint8_t afTrigger; /**< AF_TRIGGER */
};

/**
 * \class IntelAfModeBase
 *
 * Base class for all the AutoFocus modes as defined by the Android
 * camera device V3.x API.
 * Each mode will follow certain state transitions. See documentation for
 * android.control.afState
 *
 */
class IntelAfModeBase {
 public:
    IntelAfModeBase();
    virtual ~IntelAfModeBase(){};

    virtual int processTriggers(uint8_t afTrigger, uint8_t afMode) = 0;
    virtual int processResult(int afState, bool lensMoving, android::CameraMetadata* result) = 0;

    void resetState(void);
    void resetTrigger(usecs_t triggerTime);
    int getState() { return mCurrentAfState; }
    void updateResult(android::CameraMetadata* results);

 protected:
    void checkIfFocusTimeout();

 protected:
    AfControls mLastAfControls;
    uint8_t mCurrentAfState;
    uint8_t mLensState;
    usecs_t mLastActiveTriggerTime; /**< in useconds */
    uint32_t mFramesSinceTrigger;
};

/**
 * \class IntelAFModeAuto
 * Derived class from IntelAFModeBase for Auto mode
 *
 */
class IntelAFModeAuto : public IntelAfModeBase {
 public:
    IntelAFModeAuto();
    virtual int processTriggers(uint8_t afTrigger, uint8_t afMode);
    virtual int processResult(int afState, bool lensMoving, android::CameraMetadata* result);
};

/**
 * \class IntelAFModeContinuousPicture
 * Derived class from IntelAFModeBase for Continuous AF mode
 *
 */
class IntelAFModeContinuousPicture : public IntelAfModeBase {
 public:
    IntelAFModeContinuousPicture();
    virtual int processTriggers(uint8_t afTrigger, uint8_t afMode);
    virtual int processResult(int afState, bool lensMoving, android::CameraMetadata* result);
};

/**
 * \class IntelAFModeOff
 * Derived class from IntelAFModeBase for OFF mode
 *
 */
class IntelAFModeOff : public IntelAfModeBase {
 public:
    IntelAFModeOff();
    virtual int processTriggers(uint8_t afTrigger, uint8_t afMode);
    virtual int processResult(int afState, bool lensMoving, android::CameraMetadata* result);
};

/**
 * \class IntelAFStateMachine
 *
 * This class adapts the Android V3 AF triggers and state transitions to
 * the ones implemented by the Intel AIQ algorithm
 * This class is platform independent. Platform specific behaviors should be
 * implemented in derived classes from this one or from the IntelAFModeBase
 *
 */
class IntelAFStateMachine {
 public:
    IntelAFStateMachine(int cameraId);
    virtual ~IntelAFStateMachine();

    int processTriggers(uint8_t afTrigger, uint8_t afMode);
    int processResult(int afState, bool lensMoving, android::CameraMetadata* result);

    int updateDefaults(android::CameraMetadata* result, bool fixedFocus = false) const;

 private:
    // prevent copy constructor and assignment operator
    DISALLOW_COPY_AND_ASSIGN(IntelAFStateMachine);

 private: /* members*/
    int mCameraId;
    AfControls mLastAfControls;
    IntelAfModeBase* mCurrentAfMode;

    std::vector<uint8_t> mAvailableModes;

    IntelAFModeOff mOffMode;
    IntelAFModeAuto mAutoMode;

    IntelAFModeContinuousPicture mContinuousPictureMode;
};

}  // namespace camera3
