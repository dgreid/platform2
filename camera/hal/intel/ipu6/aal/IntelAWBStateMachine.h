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

/**
 * \struct AwbControls
 *
 * Control Modes saved and passed back to control unit after reading
 *
 */
struct AwbControls {
    uint8_t awbMode;                       /**< AWB_MODE */
    uint8_t awbLock;                       /**< AWB_LOCK */
    uint8_t colorCorrectionMode;           /**< COLOR_CORRECTION_MODE */
    uint8_t colorCorrectionAberrationMode; /**< COLOR_CORRECTION_ABERRATION_MODE */
};

/**
 * \class IntelAWBModeBase
 *
 * Base class for all the Auto white balance modes as defined by the Android
 * camera device V3.x API.
 * Each mode will follow certain state transitions. See documentation for
 * android.control.awbState
 *
 */
class IntelAWBModeBase {
 public:
    IntelAWBModeBase();
    virtual ~IntelAWBModeBase(){};

    virtual int processState(uint8_t controlMode, uint8_t sceneMode,
                             const AwbControls& awbControls) = 0;

    virtual int processResult(bool converged, android::CameraMetadata* results) = 0;

    void resetState(void);
    uint8_t getState() const { return mCurrentAwbState; }

 protected:
    void updateResult(android::CameraMetadata* results);

 protected:
    AwbControls mLastAwbControls;
    uint8_t mLastControlMode;
    uint8_t mLastSceneMode;

    uint8_t mCurrentAwbState;
};

/**
 * \class IntelAWBModeAuto
 * Derived class from IntelAWBModeBase for Auto mode
 *
 */
class IntelAWBModeAuto : public IntelAWBModeBase {
 public:
    IntelAWBModeAuto();
    virtual int processState(uint8_t controlMode, uint8_t sceneMode,
                             const AwbControls& awbControls);

    virtual int processResult(bool converged, android::CameraMetadata* results);
};

/**
 * \class IntelAWBModeOFF
 * Derived class from IntelAWBModeBase for OFF mode
 *
 */
class IntelAWBModeOff : public IntelAWBModeBase {
 public:
    IntelAWBModeOff();
    virtual int processState(uint8_t controlMode, uint8_t sceneMode,
                             const AwbControls& awbControls);

    virtual int processResult(bool converged, android::CameraMetadata* results);
};

/**
 * \class IntelAWBStateMachine
 *
 * This class adapts the Android V3 AWB triggers and state transitions to
 * the ones implemented by the Intel AIQ algorithm
 * This class is platform independent. Platform specific behaviors should be
 * implemented in derived classes from this one or from the IntelAWBModeBase
 *
 */
class IntelAWBStateMachine {
 public:
    IntelAWBStateMachine(int CameraId);
    virtual ~IntelAWBStateMachine();

    int processState(uint8_t controlMode, uint8_t sceneMode, const AwbControls& awbControls);

    int processResult(bool converged, android::CameraMetadata* results);

    uint8_t getState() const { return mCurrentAwbMode->getState(); }

 private:
    // prevent copy constructor and assignment operator
    DISALLOW_COPY_AND_ASSIGN(IntelAWBStateMachine);

 private: /* members*/
    int mCameraId;
    AwbControls mLastAwbControls;
    uint8_t mLastControlMode;
    uint8_t mLastSceneMode;

    IntelAWBModeBase* mCurrentAwbMode;

    IntelAWBModeOff mOffMode;
    IntelAWBModeAuto mAutoMode;
};

}  // namespace camera3
