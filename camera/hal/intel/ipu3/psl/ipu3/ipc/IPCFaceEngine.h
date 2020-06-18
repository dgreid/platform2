/*
 * Copyright (C) 2018-2020 Intel Corporation.
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

#ifndef IPC_FACE_ENGINE_H_
#define IPC_FACE_ENGINE_H_

#include "IPCCommon.h"

#include "pvl_types.h"
#include "pvl_config.h"
#include "ia_face.h"
#include "pvl_face_detection.h"
#include "pvl_eye_detection.h"
#include "pvl_mouth_detection.h"

namespace cros {
namespace intel {
#define MAX_FACES_DETECTABLE 10

#define RECT_SIZE 4
#define LM_SIZE 6

typedef enum {
    FD_MODE_OFF,
    FD_MODE_SIMPLE,  /**< Provide face area */
    FD_MODE_FULL,    /**< Provide face area, eye and mouth coordinates */
} face_detection_mode;

struct FaceEngineResult {
    int faceNum;
    pvl_face_detection_result faceResults[MAX_FACES_DETECTABLE];
    pvl_eye_detection_result eyeResults[MAX_FACES_DETECTABLE];
    pvl_mouth_detection_result mouthResults[MAX_FACES_DETECTABLE];
};

struct face_engine_init_params {
    unsigned int max_face_num;
    face_detection_mode fd_mode;
};

#define MAX_FACE_FRAME_WIDTH 1920
#define MAX_FACE_FRAME_HEIGHT 1280
#define MAX_FACE_FRAME_SIZE (MAX_FACE_FRAME_WIDTH * MAX_FACE_FRAME_HEIGHT) // only using Y
struct face_engine_run_params {
    uint8_t data[MAX_FACE_FRAME_SIZE]; // TODO: use dma buf to optimize.
    uint32_t size;
    int32_t width;
    int32_t height;
    pvl_image_format format;
    int32_t stride;
    int32_t rotation;

    FaceEngineResult results;
};

class IPCFaceEngine {
public:
    IPCFaceEngine();
    virtual ~IPCFaceEngine();

    bool clientFlattenInit(unsigned int max_face_num,
                           face_detection_mode fd_mode,
                           face_engine_init_params* params);
    bool clientFlattenRun(const pvl_image& frame, face_engine_run_params* params);
    bool serverUnflattenRun(const face_engine_run_params& inParams, pvl_image* image);
};
} /* namespace intel */
} /* namespace cros */
#endif // IPC_FACE_ENGINE_H_
