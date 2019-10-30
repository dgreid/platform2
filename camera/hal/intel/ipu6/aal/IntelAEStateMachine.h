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
 * \struct AeControls
 *
 * Control Modes saved and passed back to control unit after reading
 *
 */
struct AeControls {
    uint8_t aeMode;              /**< AE_MODE */
    uint8_t aeLock;              /**< AE_LOCK */
    uint8_t aePreCaptureTrigger; /**< PRECAPTURE_TRIGGER */
    uint8_t sceneMode;           /**< SCENE_MODE */
    int32_t evCompensation;      /**< AE_EXPOSURE_COMPENSATION */
};

/**
 * \class IntelAEModeBase
 *
 * Base class for all the Autoexposure modes as defined by the Android
 * camera device V3.x API.
 * Each mode will follow certain state transitions. See documentation for
 * android.control.aeState
 *
 */
class IntelAEModeBase {
 public:
    IntelAEModeBase();
    virtual ~IntelAEModeBase(){};

    virtual int processState(uint8_t controlMode, uint8_t sceneMode,
                             const AeControls& aeControls) = 0;

    virtual int processResult(bool aeConverged, android::CameraMetadata* results) = 0;

    void resetState(void);
    uint8_t getState() const { return mCurrentAeState; }

 protected:
    void updateResult(android::CameraMetadata* results);

 protected:
    AeControls mLastAeControls;
    uint8_t mLastControlMode;
    uint8_t mLastSceneMode;
    bool mEvChanged; /**< set and kept to true when ev changes until
                          converged */

    bool mLastAeConvergedFlag;
    uint8_t mAeRunCount;
    uint8_t mAeConvergedCount;
    uint8_t mCurrentAeState;
};

/**
 * \class IntelAEModeAuto
 * Derived class from IntelAEModeBase for Auto mode
 *
 */
class IntelAEModeAuto : public IntelAEModeBase {
 public:
    IntelAEModeAuto();
    virtual int processState(uint8_t controlMode, uint8_t sceneMode, const AeControls& aeControls);
    virtual int processResult(bool aeConverged, android::CameraMetadata* result);
};

/**
 * \class IntelAEModeOFF
 * Derived class from IntelAEModeBase for OFF mode
 *
 */
class IntelAEModeOff : public IntelAEModeBase {
 public:
    IntelAEModeOff();
    virtual int processState(uint8_t controlMode, uint8_t sceneMode, const AeControls& aeControls);
    virtual int processResult(bool aeConverged, android::CameraMetadata* result);
};

/**
 * \class IntelAEStateMachine
 *
 * This class adapts the Android V3 AE triggers and state transitions to
 * the ones implemented by the Intel AIQ algorithm
 * This class is platform independent. Platform specific behaviors should be
 * implemented in derived classes from this one or from the IntelAEModeBase
 *
 */
class IntelAEStateMachine {
 public:
    IntelAEStateMachine(int cameraId);
    virtual ~IntelAEStateMachine();

    int processState(uint8_t controlMode, uint8_t sceneMode, const AeControls& aeControls);

    int processResult(bool aeConverged, android::CameraMetadata* results);

    uint8_t getState() const { return mCurrentAeMode->getState(); }

 private:
    // prevent copy constructor and assignment operator
    DISALLOW_COPY_AND_ASSIGN(IntelAEStateMachine);

 private: /* members*/
    int mCameraId;
    AeControls mLastAeControls;
    uint8_t mLastControlMode;
    uint8_t mLastSceneMode;

    IntelAEModeBase* mCurrentAeMode;

    IntelAEModeOff mOffMode;
    IntelAEModeAuto mAutoMode;
};

}  // namespace camera3
