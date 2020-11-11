/*
 * Copyright (C) 2018 Intel Corporation
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

#ifndef FACE_ENGINE_LIBRARY_H_
#define FACE_ENGINE_LIBRARY_H_

#include "base/macros.h"
#include "IPCCommon.h"
#include "IPCFaceEngine.h"
#include <memory>
#include <mutex>
#include "Utils.h"
#include <utils/Errors.h>

namespace cros {
namespace intel {

class FaceEngineLibrary {
public:
    FaceEngineLibrary();
    FaceEngineLibrary(const FaceEngineLibrary&) = delete;
    FaceEngineLibrary& operator=(const FaceEngineLibrary&) = delete;

    virtual ~FaceEngineLibrary();

    status_t init(void* pData, int dataSize);
    status_t uninit();
    status_t run(void* pData, int dataSize);

private:
    IPCFaceEngine mIpc;

    pvl_face_detection* mFDHandle;
    pvl_eye_detection* mEDHandle;
    pvl_mouth_detection* mMDHandle;

    face_detection_mode mMode;
    unsigned int mMaxFacesNum;

    void convertCoordinate(int faceId, int width, int height, pvl_rect& src, pvl_rect* dst);
};
} /* namespace intel */
} /* namespace cros */
#endif // FACE_ENGINE_LIBRARY_H_
