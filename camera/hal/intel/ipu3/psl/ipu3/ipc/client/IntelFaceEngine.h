/*
 * Copyright (C) 2019 Intel Corporation.
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

#ifndef INTEL_FACE_ENGINE_H_
#define INTEL_FACE_ENGINE_H_

#include <vector>
#include <queue>
#include <ia_types.h>

#include "Intel3aCommon.h"
#include "IPCFaceEngine.h"

#define MAX_STORE_FACE_DATA_BUF_NUM 3

namespace cros {
namespace intel {
class IntelFaceEngine {
public:
    IntelFaceEngine();
    IntelFaceEngine(const IntelFaceEngine&) = delete;
    IntelFaceEngine& operator=(const IntelFaceEngine&) = delete;

    virtual ~IntelFaceEngine();

    bool init(unsigned int max_face_num, int maxWidth, int maxHeight, face_detection_mode fd_mode);
    void uninit();
    bool prepareRun(const pvl_image &frame);
    bool run(FaceEngineResult *results);

private:
    ShmMemInfo *acquireRunBuf();
    void returnRunBuf(ShmMemInfo *memRunBuf);

    IPCFaceEngine mIpc;
    Intel3aCommon mCommon;

    bool mInitialized;

    ShmMemInfo mMemInit;
    std::mutex mMemRunPoolLock;
    std::queue<ShmMemInfo *> mMemRunPool;
    ShmMemInfo mMemRunBufs[MAX_STORE_FACE_DATA_BUF_NUM];

    std::mutex mMemRunningPoolLock;
    std::queue<ShmMemInfo *> mMemRunningPool;

    std::vector<ShmMem> mMems;
};
} /* namespace intel */
} /* namespace cros */
#endif // INTEL_FACE_ENGINE_H_
