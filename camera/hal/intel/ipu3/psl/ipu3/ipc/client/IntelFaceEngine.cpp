/*
 * Copyright (C) 2019-2020 Intel Corporation
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

#define LOG_TAG "IA_FACE_ENGINE_IPC"

#include "LogHelper.h"
#include "IntelFaceEngine.h"
#include <utils/Errors.h>
#include "UtilityMacros.h"

namespace cros {
namespace intel {
IntelFaceEngine::IntelFaceEngine():
    mInitialized(false)
{
    LOG1("@%s", __FUNCTION__);

    mMems = {{"/faceEngineInitShm", sizeof(face_engine_init_params), &mMemInit, false}};
    for (uint32_t i= 0; i < MAX_STORE_FACE_DATA_BUF_NUM; i++) {
        mMems.push_back({("/faceEngineRunShm" + std::to_string(i)), sizeof(face_engine_run_params), &mMemRunBufs[i], false});
        mMemRunPool.push(&mMemRunBufs[i]);
    }

    bool success = mCommon.allocateAllShmMems(mMems);
    if (!success) {
        mCommon.releaseAllShmMems(mMems);
        return;
    }

    LOG1("@%s, done", __FUNCTION__);
    mInitialized = true;
}

IntelFaceEngine::~IntelFaceEngine()
{
    LOG1("@%s", __FUNCTION__);
    mCommon.releaseAllShmMems(mMems);
}

bool IntelFaceEngine::init(unsigned int max_face_num, int maxWidth, int maxHeight, face_detection_mode fd_mode)
{
    LOG1("@%s, max_face_num:%d, fd_mode:%d", __FUNCTION__, max_face_num, fd_mode);
    CheckError(!mInitialized, false, "@%s, mInitialized is false", __FUNCTION__);

    CheckError((maxWidth * maxHeight > MAX_FACE_FRAME_SIZE), false,
                "@%s, maxWidth:%d, maxHeight:%d > MAX_FACE_FRAME_SIZE", __FUNCTION__, maxWidth, maxHeight);

    face_engine_init_params* params = static_cast<face_engine_init_params*>(mMemInit.mAddr);

    bool ret = mIpc.clientFlattenInit(max_face_num, fd_mode, params);
    CheckError(!ret, false, "@%s, clientFlattenInit fails", __FUNCTION__);

    ret = mCommon.requestSync(IPC_FACE_INIT, mMemInit.mHandle);
    CheckError(!ret, false, "@%s, requestSync fails", __FUNCTION__);

    return true;
}

void IntelFaceEngine::uninit()
{
    LOG1("@%s", __FUNCTION__);
    CheckError(!mInitialized, VOID_VALUE, "@%s, mInitialized is false", __FUNCTION__);

    bool ret = mCommon.requestSync(IPC_FACE_UNINIT);
    CheckError(!ret, VOID_VALUE, "@%s, requestSync fails", __FUNCTION__);
}

bool IntelFaceEngine::prepareRun(const pvl_image &frame)
{
    LOG1("@%s, size:%d, w:%d, h:%d, f:%d, s:%d, r:%d", __FUNCTION__,
         frame.size, frame.width, frame.height, frame.format, frame.stride, frame.rotation);
    CheckError(!mInitialized, false, "@%s, mInitialized is false", __FUNCTION__);

    ShmMemInfo *memRunBufTmp = acquireRunBuf();
    CheckError((!memRunBufTmp), false, "Failed to acquire buffer");

    face_engine_run_params* params = static_cast<face_engine_run_params*>(memRunBufTmp->mAddr);
    bool ret = mIpc.clientFlattenRun(frame, params);
    if (!ret) {
        LOGE("@%s, clientFlattenInit fails", __FUNCTION__);
        returnRunBuf(memRunBufTmp);
        return false;
    }

    std::lock_guard<std::mutex> l(mMemRunningPoolLock);
    mMemRunningPool.push(memRunBufTmp);
    return true;
}

bool IntelFaceEngine::run(FaceEngineResult *results)
{
    LOG1("@%s", __FUNCTION__);
    CheckError(!mInitialized, false, "@%s, mInitialized is false", __FUNCTION__);
    CheckError(!results, false, "input parameters are invalid");
    CLEAR(*results);

    ShmMemInfo *memRunBuf = nullptr;
    {
        std::lock_guard<std::mutex> l(mMemRunningPoolLock);
        CheckError(mMemRunningPool.empty(), false, "mMemRunningPool is empty");
        memRunBuf = mMemRunningPool.front();
        mMemRunningPool.pop();
    }

    face_engine_run_params* params = static_cast<face_engine_run_params*>(memRunBuf->mAddr);
    bool ret = mCommon.requestSync(IPC_FACE_RUN, memRunBuf->mHandle);
    if (ret) {
        *results = params->results;
    } else {
        LOGE("@%s, requestSync fails", __FUNCTION__);
    }
    returnRunBuf(memRunBuf);

    return ret;
}

ShmMemInfo *IntelFaceEngine::acquireRunBuf()
{
    std::lock_guard<std::mutex> l(mMemRunPoolLock);
    LOG2("%s size is %zu", __FUNCTION__, mMemRunPool.size());

    ShmMemInfo *memRunBuf = nullptr;
    if (!mMemRunPool.empty()) {
        memRunBuf = mMemRunPool.front();
        mMemRunPool.pop();
    }
    return memRunBuf;
}

void IntelFaceEngine::returnRunBuf(ShmMemInfo *memRunBuf)
{
    LOG2("Push back run buffer");
    std::lock_guard<std::mutex> l(mMemRunPoolLock);
    mMemRunPool.push(memRunBuf);
}

} /* namespace intel */
} /* namespace cros */
