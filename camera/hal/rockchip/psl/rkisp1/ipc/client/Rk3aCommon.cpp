/*
 * Copyright (C) 2017 Intel Corporation.
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
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

#define LOG_TAG "Rk3aCommon"

#include "LogHelper.h"
#include "Rk3aCommon.h"
#include "PlatformData.h"

NAMESPACE_DECLARATION {
Rk3aCommon::Rk3aCommon()
{
    LOG1("@%s", __FUNCTION__);

    mClient = PlatformData::getRockchip3AClient();
    LOG1("@%s, mClient:%p", __FUNCTION__, mClient);
}

Rk3aCommon::~Rk3aCommon()
{
    LOG1("@%s", __FUNCTION__);
}

bool Rk3aCommon::allocShmMem(std::string& name, int size, ShmMemInfo* shm)
{
    LOG1("@%s", __FUNCTION__);
    CheckAndLogError(mClient == nullptr, false, "@%s, mClient is nullptr",
                     __FUNCTION__);

    shm->mName = name;
    shm->mSize = size;
    int ret = mClient->allocateShmMem(shm->mName, shm->mSize, &shm->mFd, &shm->mAddr);
    CheckAndLogError((ret != OK), false, "@%s, call allocateShmMem fail",
                     __FUNCTION__);

    shm->mHandle = mClient->registerBuffer(shm->mFd);
    CheckAndLogError((shm->mHandle < 0), false,
                     "@%s, call mBridge->RegisterBuffer fail", __FUNCTION__);

    return true;
}

bool Rk3aCommon::requestSync(IPC_CMD cmd, int32_t handle)
{
    LOG1("@%s", __FUNCTION__);
    CheckAndLogError(mClient == nullptr, false, "@%s, mClient is nullptr",
                     __FUNCTION__);

    return mClient->requestSync(cmd, handle) == OK ? true : false;
}

bool Rk3aCommon::requestSync(IPC_CMD cmd)
{
    LOG1("@%s", __FUNCTION__);
    CheckAndLogError(mClient == nullptr, false, "@%s, mClient is nullptr",
                     __FUNCTION__);

    return mClient->requestSync(cmd) == OK ? true : false;
}

void Rk3aCommon::freeShmMem(ShmMemInfo& shm)
{
    LOG1("@%s, mHandle:%d, mFd:%d, mName:%s, mSize:%d, mAddr:%p",
        __FUNCTION__, shm.mHandle, shm.mFd, shm.mName.c_str(), shm.mSize, shm.mAddr);
    CheckAndLogError(mClient == nullptr, VOID_VALUE, "@%s, mClient is nullptr",
                     __FUNCTION__);
    if (shm.mHandle < 0 || shm.mFd < 0) {
        LOGE("@%s, mHandle:%d, mFd:%d, one of them < 0", __FUNCTION__, shm.mHandle, shm.mFd);
        return;
    }

    mClient->deregisterBuffer(shm.mHandle);
    mClient->releaseShmMem(shm.mName, shm.mSize, shm.mFd, shm.mAddr);
}

bool Rk3aCommon::allocateAllShmMems(std::vector<ShmMem>& mems)
{
    LOG1("@%s", __FUNCTION__);

    for (auto& it : mems) {
        ShmMemInfo* mem = it.mem;
        mem->mName = it.name;
        mem->mSize = it.size;
        bool ret = allocShmMem(mem->mName, mem->mSize, mem);
        if (!ret) {
            LOGE("@%s, allocShmMem fails, name:%s, size:%d",
                __FUNCTION__, mem->mName.c_str(), mem->mSize);
            return false;
        }
        it.allocated = true;
    }

    return true;
}

void Rk3aCommon::releaseAllShmMems(std::vector<ShmMem>& mems)
{
    LOG1("@%s", __FUNCTION__);

    for (auto& it : mems) {
        if (it.allocated) {
            freeShmMem(*it.mem);
        }
    }
}
} NAMESPACE_DECLARATION_END
