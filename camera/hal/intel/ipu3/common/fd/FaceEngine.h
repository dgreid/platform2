/*
 * Copyright (C) 2019 Intel Corporation
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

#ifndef FACE_ENGINE_H_
#define FACE_ENGINE_H_

#include <memory>
#include <mutex>

#include "CameraBuffer.h"
#include <cros-camera/camera_thread.h>
#include "ia_face.h"
#include "Intel3aCommon.h"
#include "IntelFaceEngine.h"
#include "pvl_types.h"
#include "pvl_config.h"
#include "pvl_face_detection.h"
#include "pvl_eye_detection.h"
#include "pvl_mouth_detection.h"
#include <utils/Errors.h>
#include "Utils.h"

namespace cros {
namespace intel {

typedef struct CVFaceEngineAbstractResult {
    uint32_t requestId;

    /* Face Detection results */
    int faceNum;
    int faceIds[MAX_FACES_DETECTABLE];
    int faceLandmarks[LM_SIZE * MAX_FACES_DETECTABLE];
    int faceRect[RECT_SIZE * MAX_FACES_DETECTABLE];
    uint8_t faceScores[MAX_FACES_DETECTABLE];
} CVFaceEngineAbstractResult;

class FaceEngine {
public:
    FaceEngine(int cameraId, unsigned int maxFaceNum,
                   int maxWidth, int maxHeight, face_detection_mode fdMode);
    FaceEngine(const FaceEngine&) = delete;
    FaceEngine& operator=(const FaceEngine&) = delete;

    ~FaceEngine();

    void run(const pvl_image& frame);

    void getMaxSupportedResolution(int *maxWidth, int *maxHeight) const;

    face_detection_mode getMode() const { return mMode; }
    int getFacesNum(void);
    int getResult(FaceEngineResult* result);
    int getResult(ia_face_state* faceState);
    int getResult(CVFaceEngineAbstractResult* result);

private:
    void handleRun(void);

    int mCameraId;

    bool mInitialized;

    face_detection_mode mMode;

    // for performance reason, let's limit the max image size.
    int mMaxWidth;
    int mMaxHeight;

    // width and height will not change runtime.
    int mWidth;
    int mHeight;

    std::mutex mLock;
    FaceEngineResult mResult;

    IntelFaceEngine mFace;

    cros::CameraThread mCameraThread;
};  // class FaceEngine
}  // namespace intel
}  // namespace cros
#endif // FACE_ENGINE_H_
