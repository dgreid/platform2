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

#define LOG_TAG "Intel3AClient"
#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <sys/mman.h>

#include "Camera3HAL.h"
#include "Intel3AClient.h"
#include "Utils.h"

namespace cros {
namespace intel {
Intel3AClient::Intel3AClient():
    mErrCb(nullptr),
    mIPCStatus(true),
    mInitialized(false)
{
    LOG1("@%s", __FUNCTION__);

    mCallback = base::Bind(&Intel3AClient::callbackHandler, base::Unretained(this));
    Intel3AClient::return_callback = returnCallback;

    mNotifyCallback = base::Bind(&Intel3AClient::notifyHandler, base::Unretained(this));
    Intel3AClient::notify = notifyCallback;

    mBridge = cros::CameraAlgorithmBridge::CreateInstance(
        cros::CameraAlgorithmBackend::kVendorCpu, g_mojo_manager_token);
    CheckError(!mBridge, VOID_VALUE, "@%s, mBridge is nullptr", __FUNCTION__);
    CheckError((mBridge->Initialize(this) != 0), VOID_VALUE, "@%s, call mBridge->Initialize fail", __FUNCTION__);

    for (int i = 0; i < IPC_GROUP_NUM; i++) {
        mRunner[i] = std::unique_ptr<Runner>(new Runner(static_cast<IPC_GROUP>(i), mBridge.get()));
    }

    mInitialized = true;
}

Intel3AClient::~Intel3AClient()
{
    LOG1("@%s", __FUNCTION__);
}

bool Intel3AClient::isInitialized()
{
    LOG1("@%s, mInitialized:%d", __FUNCTION__, mInitialized);

    return mInitialized;
}

bool Intel3AClient::isIPCFine()
{
    std::lock_guard<std::mutex> l(mIPCStatusMutex);
    LOG1("@%s, mIPCStatus:%d", __FUNCTION__, mIPCStatus);

    return mIPCStatus;
}

void Intel3AClient::registerErrorCallback(IErrorCallback* errCb)
{
    LOG1("@%s, errCb:%p", __FUNCTION__, errCb);

    std::lock_guard<std::mutex> l(mIPCStatusMutex);
    mErrCb = errCb;

    if (!mIPCStatus && mErrCb) {
        mErrCb->deviceError();
    }
}

int Intel3AClient::allocateShmMem(std::string& name, int size, int* fd, void** addr)
{
    LOG1("@%s, name:%s, size:%d", __FUNCTION__, name.c_str(), size);

    *fd = -1;
    *addr = nullptr;
    int shmFd = -1;
    void* shmAddr = nullptr;

    shmFd = shm_open(name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    CheckError((shmFd == -1), UNKNOWN_ERROR, "@%s, call shm_open fail", __FUNCTION__);

    int ret = fcntl(shmFd, F_GETFD);
    CheckError((ret == -1), UNKNOWN_ERROR, "@%s, call fcntl fail", __FUNCTION__);

    ret = ftruncate(shmFd, size);
    CheckError((ret == -1), UNKNOWN_ERROR, "@%s, call fcntl fail", __FUNCTION__);

    struct stat sb;
    ret = fstat(shmFd, &sb);
    CheckError((ret == -1), UNKNOWN_ERROR, "@%s, call fstat fail", __FUNCTION__);
    CheckError((sb.st_size != size), UNKNOWN_ERROR, "@%s, sb.st_size:%jd", __FUNCTION__, (intmax_t)sb.st_size);

    shmAddr = mmap(0, sb.st_size, PROT_WRITE, MAP_SHARED, shmFd, 0);
    CheckError((!shmAddr), UNKNOWN_ERROR, "@%s, call mmap fail", __FUNCTION__);

    *fd = shmFd;
    *addr = shmAddr;

    return OK;
}

void Intel3AClient::releaseShmMem(std::string& name, int size, int fd, void* addr)
{
    LOG1("@%s, name:%s, size:%d, fd:%d, addr:%p", __FUNCTION__, name.c_str(), size, fd, addr);

    munmap(addr, size);
    close(fd);
    shm_unlink(name.c_str());
}

int Intel3AClient::requestSync(IPC_CMD cmd, int32_t bufferHandle)
{
    LOG1("@%s, cmd:%d:%s, bufferHandle:%d, mInitialized:%d",
         __FUNCTION__, cmd, Intel3AIpcCmdToString(cmd), bufferHandle, mInitialized);
    CheckError(!mInitialized, UNKNOWN_ERROR, "@%s, mInitialized is false", __FUNCTION__);
    CheckError(!isIPCFine(), UNKNOWN_ERROR, "@%s, IPC error happens", __FUNCTION__);

    IPC_GROUP group = Intel3AIpcCmdToGroup(cmd);

    return mRunner[group]->requestSync(cmd, bufferHandle);
}

int Intel3AClient::requestSync(IPC_CMD cmd)
{
    return requestSync(cmd, -1);
}

int32_t Intel3AClient::registerBuffer(int bufferFd)
{
    LOG1("@%s, bufferFd:%d, mInitialized:%d", __FUNCTION__, bufferFd, mInitialized);
    CheckError(!mInitialized, -1, "@%s, mInitialized is false", __FUNCTION__);
    CheckError(!isIPCFine(), -1, "@%s, IPC error happens", __FUNCTION__);

    return mBridge->RegisterBuffer(bufferFd);
}

void Intel3AClient::deregisterBuffer(int32_t bufferHandle)
{
    LOG1("@%s, bufferHandle:%d, mInitialized:%d", __FUNCTION__, bufferHandle, mInitialized);
    CheckError(!mInitialized, VOID_VALUE, "@%s, mInitialized is false", __FUNCTION__);
    CheckError(!isIPCFine(), VOID_VALUE, "@%s, IPC error happens", __FUNCTION__);

    std::vector<int32_t> handles({bufferHandle});
    mBridge->DeregisterBuffers(handles);
}

void Intel3AClient::callbackHandler(uint32_t req_id, uint32_t status, int32_t buffer_handle)
{
    LOG2("@%s, req_id:%d, status:%d, buffer_handle:%d",
         __FUNCTION__, req_id, status, buffer_handle);

    IPC_GROUP group = Intel3AIpcCmdToGroup(static_cast<IPC_CMD>(req_id));
    mRunner[group]->callbackHandler(status, buffer_handle);
}

void Intel3AClient::notifyHandler(uint32_t msg)
{
    LOG2("@%s, msg:%d", __FUNCTION__, msg);

    if (msg != CAMERA_ALGORITHM_MSG_IPC_ERROR) {
        LOGE("@%s, receive msg:%d, not CAMERA_ALGORITHM_MSG_IPC_ERROR", __FUNCTION__, msg);
        return;
    }

    std::lock_guard<std::mutex> l(mIPCStatusMutex);
    mIPCStatus = false;
    if (mErrCb) {
        mErrCb->deviceError();
    } else {
        LOGE("@%s, mErrCb is nullptr, no device error is sent out", __FUNCTION__);
    }
    LOGE("@%s, receive CAMERA_ALGORITHM_MSG_IPC_ERROR", __FUNCTION__);
}

void Intel3AClient::returnCallback(const camera_algorithm_callback_ops_t* callback_ops,
                                   uint32_t req_id,
                                   uint32_t status,
                                   int32_t buffer_handle)
{
    LOG2("@%s", __FUNCTION__);
    CheckError(!callback_ops, VOID_VALUE, "@%s, callback_ops is nullptr", __FUNCTION__);

    auto s = const_cast<Intel3AClient*>(static_cast<const Intel3AClient*>(callback_ops));
    s->mCallback.Run(req_id, status, buffer_handle);
}

void Intel3AClient::notifyCallback(const struct camera_algorithm_callback_ops* callback_ops,
                                 camera_algorithm_error_msg_code_t msg)
{
    LOG2("@%s", __FUNCTION__);
    CheckError(!callback_ops, VOID_VALUE, "@%s, callback_ops is nullptr", __FUNCTION__);

    auto s = const_cast<Intel3AClient*>(static_cast<const Intel3AClient*>(callback_ops));
    s->mNotifyCallback.Run((uint32_t)msg);
}

Intel3AClient::Runner::Runner(IPC_GROUP group, cros::CameraAlgorithmBridge* bridge):
    mGroup(group),
    mBridge(bridge),
    mIsCallbacked(false),
    mCbResult(true),
    mInitialized(false)
{
    LOG1("@%s, group:%d", __FUNCTION__, mGroup);

    pthread_condattr_t attr;
    int ret = pthread_condattr_init(&attr);
    if (ret != 0) {
        LOGE("@%s, call pthread_condattr_init fails, ret:%d", __FUNCTION__, ret);
        pthread_condattr_destroy(&attr);
        return;
    }

    ret = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (ret != 0) {
        LOGE("@%s, call pthread_condattr_setclock fails, ret:%d", __FUNCTION__, ret);
        pthread_condattr_destroy(&attr);
        return;
    }

    ret = pthread_cond_init(&mCbCond, &attr);
    if (ret != 0) {
        LOGE("@%s, call pthread_cond_init fails, ret:%d", __FUNCTION__, ret);
        pthread_condattr_destroy(&attr);
        return;
    }

    pthread_condattr_destroy(&attr);

    ret = pthread_mutex_init(&mCbLock, nullptr);
    CheckError(ret != 0, VOID_VALUE, "@%s, call pthread_mutex_init fails, ret:%d", __FUNCTION__, ret);

    mInitialized = true;
}

Intel3AClient::Runner::~Runner()
{
    LOG1("@%s, group:%d", __FUNCTION__, mGroup);

    int ret = pthread_cond_destroy(&mCbCond);
    if (ret != 0) {
        LOGE("@%s, call pthread_cond_destroy fails, ret:%d", __FUNCTION__, ret);
    }

    ret = pthread_mutex_destroy(&mCbLock);
    if (ret != 0) {
        LOGE("@%s, call pthread_mutex_destroy fails, ret:%d", __FUNCTION__, ret);
    }
}

int Intel3AClient::Runner::requestSync(IPC_CMD cmd, int32_t bufferHandle)
{
    LOG1("@%s, cmd:%d:%s, group:%d, bufferHandle:%d, mInitialized:%d",
          __FUNCTION__, cmd, Intel3AIpcCmdToString(cmd), mGroup, bufferHandle, mInitialized);
    CheckError(!mInitialized, UNKNOWN_ERROR, "@%s, mInitialized is false", __FUNCTION__);

    std::lock_guard<std::mutex> lck(mMutex);

    std::vector<uint8_t> reqHeader(IPC_REQUEST_HEADER_USED_NUM);
    reqHeader[0] = IPC_MATCHING_KEY;

    // cmd is for request id, no duplicate command will be issued at any given time.
    mBridge->Request(cmd, reqHeader, bufferHandle);
    int ret = waitCallback();
    CheckError((ret != OK), UNKNOWN_ERROR, "@%s, call waitCallback fail", __FUNCTION__);

    LOG2("@%s, cmd:%d:%s, group:%d, mCbResult:%d, done!",
          __FUNCTION__, cmd, Intel3AIpcCmdToString(cmd), mGroup, mCbResult);

    // check callback result
    CheckError((mCbResult != true), UNKNOWN_ERROR, "@%s, callback fail", __FUNCTION__);

    return OK;
}

void Intel3AClient::Runner::callbackHandler(uint32_t status, int32_t buffer_handle)
{
    LOG2("@%s, group:%d, status:%d, buffer_handle:%d", __FUNCTION__, mGroup, status, buffer_handle);
    if (status != 0) {
        LOGE("@%s, group:%d, status:%d, buffer_handle:%d", __FUNCTION__, mGroup, status, buffer_handle);
    }
    mCbResult = status != 0 ? false : true;

    pthread_mutex_lock(&mCbLock);
    mIsCallbacked = true;
    int ret = pthread_cond_signal(&mCbCond);
    pthread_mutex_unlock(&mCbLock);

    CheckError(ret != 0, VOID_VALUE, "@%s, group:%d, call pthread_cond_signal fails, ret:%d", __FUNCTION__, mGroup, ret);
}

int Intel3AClient::Runner::waitCallback()
{
    LOG2("@%s, group:%d", __FUNCTION__, mGroup);
    nsecs_t startTime = systemTime();

    pthread_mutex_lock(&mCbLock);
    if (!mIsCallbacked) {
        int ret = 0;
        struct timespec ts = {0, 0};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec += 5; // 5s timeout

        while (!mIsCallbacked && !ret) {
            ret = pthread_cond_timedwait(&mCbCond, &mCbLock, &ts);
        }
        if (ret != 0) {
            LOGE("@%s, group:%d, call pthread_cond_timedwait fail, ret:%d, it takes %" PRId64 "ms",
                  __FUNCTION__, mGroup, ret, (systemTime() - startTime) / 1000000);
            pthread_mutex_unlock(&mCbLock);
            return UNKNOWN_ERROR;
        }
    }
    mIsCallbacked = false;
    pthread_mutex_unlock(&mCbLock);

    LOG2("@%s: group:%d, it takes %" PRId64 "ms", __FUNCTION__, mGroup, (systemTime() - startTime) / 1000000);

    return OK;
}

} /* namespace intel */
} /* namespace cros */
