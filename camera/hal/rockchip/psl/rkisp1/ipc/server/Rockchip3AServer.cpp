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

#define LOG_TAG "Rockchip3AServer"

#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "Rockchip3AServer.h"
#include "LogHelper.h"

namespace rockchip {
namespace camera {

Rockchip3AServer* Rockchip3AServer::mInstance = nullptr;

void Rockchip3AServer::init()
{
    LOG1("@%s", __FUNCTION__);

    if (mInstance == nullptr)
        mInstance = new Rockchip3AServer;
}

void Rockchip3AServer::deInit()
{
    LOG1("@%s", __FUNCTION__);

    delete mInstance;
    mInstance = nullptr;
}

Rockchip3AServer::Rockchip3AServer():
    mThread("Rockchip3AServer Thread"),
    mCallback(nullptr)
{
    LOG1("@%s", __FUNCTION__);

    mThread.Start();
    mHandleSeed = 1;
}

Rockchip3AServer::~Rockchip3AServer()
{
    LOG1("@%s", __FUNCTION__);
}

int32_t Rockchip3AServer::initialize(const camera_algorithm_callback_ops_t* callback_ops)
{
    LOG1("@%s, callback_ops:%p", __FUNCTION__, callback_ops);

    CheckAndLogError((!callback_ops), -EINVAL,
                     "@%s, the callback_ops is nullptr", __FUNCTION__);

    mCallback = callback_ops;

    return 0;
}

int32_t Rockchip3AServer::registerBuffer(int buffer_fd)
{
    LOG1("@%s, buffer_fd:%d", __FUNCTION__, buffer_fd);
    CheckAndLogError((mHandles.find(buffer_fd) != mHandles.end()), -EINVAL,
                     "@%s, Buffer already registered", __FUNCTION__);

    struct stat sb;
    int ret = fstat(buffer_fd, &sb);
    CheckAndLogError((ret == -1), -EBADFD, "@%s, Failed to get buffer status",
                     __FUNCTION__);

    void* addr = mmap(0, sb.st_size, PROT_WRITE, MAP_SHARED, buffer_fd, 0);
    CheckAndLogError((!addr), -EBADFD, "@%s, Failed to map buffer",
                     __FUNCTION__);

    int32_t handle = mHandleSeed++;
    mHandles[buffer_fd] = handle;

    mShmInfoMap[handle].fd = buffer_fd;
    mShmInfoMap[handle].addr = addr;
    mShmInfoMap[handle].size = sb.st_size;

    return handle;
}

int Rockchip3AServer::parseReqHeader(const uint8_t req_header[], uint32_t size, uint8_t* cmd)
{
    LOG1("@%s, size:%d", __FUNCTION__, size);
    if (size < IPC_REQUEST_HEADER_USED_NUM || req_header[0] != IPC_MATCHING_KEY) {
        LOGE("@%s, fail, request header size:%d, req_header[0]:%d", __FUNCTION__, size, req_header[0]);
        return -1;
    }

    *cmd = req_header[1];

    LOG2("@%s, size:%d, cmd:%d:%s",
        __FUNCTION__, size, *cmd, Rockchip3AIpcCmdToString((IPC_CMD)(*cmd)));

    return 0;
}

uint32_t Rockchip3AServer::handleRequest(uint8_t cmd, int reqeustSize, void* addr)
{
    LOG1("@%s, cmd:%d:%s, reqeustSize:%d, addr:%p",
        __FUNCTION__, cmd, Rockchip3AIpcCmdToString((IPC_CMD)cmd), reqeustSize, addr);

    CheckAndLogError((addr == nullptr), 1, "@%s, addr is nullptr",
                     __FUNCTION__);

    status_t status = OK;
    switch (cmd) {
        case IPC_3A_AIQ_INIT:
            status = mAiq.aiq_init(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_DEINIT:
            status = mAiq.aiq_deinit(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_AE_RUN:
            status = mAiq.aiq_ae_run(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_AWB_RUN:
            status = mAiq.aiq_awb_run(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_MISC_ISP_RUN:
            status = mAiq.aiq_misc_run(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_STATISTICS_SET:
            status = mAiq.statistics_set(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_GET_VERSION:
            status = mAiq.aiq_get_version(addr, reqeustSize);
            break;
        default:
            LOGE("@%s, cmd:%d is not defined", __FUNCTION__, cmd);
            break;
    }

    LOG2("@%s, cmd:%d:%s, status:%d", __FUNCTION__, cmd, Rockchip3AIpcCmdToString((IPC_CMD)cmd), status);
    return status == OK ? 0 : 1;
}

void Rockchip3AServer::request(
    uint32_t req_id,
    const uint8_t req_header[],
    uint32_t size,
    int32_t buffer_handle)
{
    LOG1("@%s, size:%d, buffer_handle:%d", __FUNCTION__, size, buffer_handle);
    uint32_t status = 0;

    uint8_t cmd = -1;
    int ret = parseReqHeader(req_header, size, &cmd);
    CheckAndLogError((ret != 0), VOID_VALUE, "@%s, call parseReqHeader fail",
                     __FUNCTION__);

    LOG2("@%s, buffer_handle:%d", __FUNCTION__, buffer_handle);
    if (buffer_handle == -1) {
        status = handleRequest(cmd, 0, nullptr);
    } else {
        CheckAndLogError((mShmInfoMap.find(buffer_handle) == mShmInfoMap.end()),
                         VOID_VALUE, "@%s, Invalid buffer handle",
                         __FUNCTION__);
        ShmInfo info = mShmInfoMap[buffer_handle];

        LOG2("@%s, info.fd:%d, info.size:%zu", __FUNCTION__, info.fd, info.size);
        status = handleRequest(cmd, info.size, info.addr);
    }

    mThread.task_runner()->PostTask(FROM_HERE,
        base::Bind(&Rockchip3AServer::returnCallback, base::Unretained(this),
                   req_id, status, buffer_handle));
}

void Rockchip3AServer::deregisterBuffers(const int32_t buffer_handles[], uint32_t size)
{
    LOG1("@%s, size:%d", __FUNCTION__, size);
    for (uint32_t i = 0; i < size; i++) {
        int32_t handle = buffer_handles[i];
        if (mShmInfoMap.find(handle) == mShmInfoMap.end()) {
            continue;
        }

        mHandles.erase(mShmInfoMap[handle].fd);

        munmap(mShmInfoMap[handle].addr, mShmInfoMap[handle].size);
        close(mShmInfoMap[handle].fd);
        mShmInfoMap.erase(handle);
    }
}

void Rockchip3AServer::returnCallback(
    uint32_t req_id, uint32_t status, int32_t buffer_handle)
{
    LOG1("@%s, buffer_handle:%d", __FUNCTION__, buffer_handle);
    (*mCallback->return_callback)(mCallback, req_id, status, buffer_handle);
}


static int32_t initialize(const camera_algorithm_callback_ops_t* callback_ops)
{
    LOG1("@%s, callback_ops:%p", __FUNCTION__, callback_ops);
    return Rockchip3AServer::getInstance()->initialize(callback_ops);
}

static int32_t registerBuffer(int32_t buffer_fd)
{
    LOG1("@%s, buffer_fd:%d", __FUNCTION__, buffer_fd);
    return Rockchip3AServer::getInstance()->registerBuffer(buffer_fd);
}

static void request(uint32_t req_id,
                    const uint8_t req_header[],
                    uint32_t size,
                    int32_t buffer_handle)
{
    LOG1("@%s, size:%d, buffer_handle:%d", __FUNCTION__, size, buffer_handle);
    Rockchip3AServer::getInstance()->request(
        req_id, req_header, size, buffer_handle);
}

static void deregisterBuffers(const int32_t buffer_handles[], uint32_t size)
{
    LOG1("@%s, size:%d", __FUNCTION__, size);
    return Rockchip3AServer::getInstance()->deregisterBuffers(buffer_handles, size);
}

extern "C" {
camera_algorithm_ops_t CAMERA_ALGORITHM_MODULE_INFO_SYM
    __attribute__((__visibility__("default"))) = {
        .initialize = initialize,
        .register_buffer = registerBuffer,
        .request = request,
        .deregister_buffers = deregisterBuffers
    };
}

__attribute__((constructor)) void initRockchip3AServer() {
    LogHelper::setDebugLevel();
    Rockchip3AServer::init();
}

__attribute__((destructor)) void deinitRockchip3AServer() {
    Rockchip3AServer::deInit();
}

} /* namespace camera */
} /* namespace rockchip */
