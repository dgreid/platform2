/*
 * Copyright (C) 2018-2020 Intel Corporation
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

#include "IntelAEStateMachine.h"
#include "IntelAFStateMachine.h"
#include "IntelAWBStateMachine.h"
#include "Utils.h"
#include "Parameters.h"

namespace camera3 {

/**
 * \class Camera3AMetadata
 *
 * This class is used to handle 3A related metadata. It also returns
 * 3A state.
 */
class Camera3AMetadata {
 public:
    Camera3AMetadata(int cameraId);
    ~Camera3AMetadata();

    void process3Astate(const icamera::Parameters& parameter, android::CameraMetadata* metadata);

 private:
    DISALLOW_COPY_AND_ASSIGN(Camera3AMetadata);

 private:
    int mCameraId;

    IntelAFStateMachine* mIntelAFStateMachine;
    IntelAEStateMachine* mIntelAEStateMachine;
    IntelAWBStateMachine* mIntelAWBStateMachine;
};

}  // namespace camera3
