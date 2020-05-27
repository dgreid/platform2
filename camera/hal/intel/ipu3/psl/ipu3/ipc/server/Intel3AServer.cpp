/*
 * Copyright (C) 2017-2020 Intel Corporation.
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

#define LOG_TAG "Intel3AServer"

#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "Intel3AServer.h"
#include "LogHelper.h"

#include <ia_log.h>

namespace cros {
namespace intel {

Intel3AServer* Intel3AServer::mInstance = nullptr;

void Intel3AServer::init()
{
    LOG1("@%s", __FUNCTION__);

    if (mInstance == nullptr) {
        mInstance = new Intel3AServer;
    }
}

void Intel3AServer::deInit()
{
    LOG1("@%s", __FUNCTION__);

    delete mInstance;
    mInstance = nullptr;
}

Intel3AServer::Intel3AServer():
    mCallback(nullptr),
    mIaLogInitialized(false)
{
    LOG1("@%s", __FUNCTION__);

    for (int i = 0; i < IPC_GROUP_NUM; i++) {
        std::string name = std::string("Intel3AServer") + std::to_string(i) + std::string(" Thread");
        mThreads[i] = std::unique_ptr<base::Thread>(new base::Thread(name));
        mThreads[i]->Start();
    }

    mHandleSeed = 1;
}

Intel3AServer::~Intel3AServer()
{
    LOG1("@%s", __FUNCTION__);

    if (mIaLogInitialized) {
        ia_log_deinit();
    }
}

int32_t Intel3AServer::initialize(const camera_algorithm_callback_ops_t* callback_ops)
{
    LOG1("@%s, callback_ops:%p", __FUNCTION__, callback_ops);

    CheckError((!callback_ops), -EINVAL, "@%s, the callback_ops is nullptr", __FUNCTION__);

    mCallback = callback_ops;

    // ia log redirection
    if (mIaLogInitialized == false) {
        ia_env env = {
            &LogHelper::cca_print_debug,
            &LogHelper::cca_print_error,
            &LogHelper::cca_print_info
        };
        ia_err ret = ia_log_init(&env);
        CheckError(ret != ia_err_none, -ENOMEM, "@%s, ia_log_init fails, ret:%d", __FUNCTION__, ret);
        mIaLogInitialized = true;
    }

    return 0;
}

int32_t Intel3AServer::registerBuffer(int buffer_fd)
{
    LOG1("@%s, buffer_fd:%d", __FUNCTION__, buffer_fd);
    CheckError((mHandles.find(buffer_fd) != mHandles.end()),
                -EINVAL, "@%s, Buffer already registered", __FUNCTION__);

    struct stat sb;
    int ret = fstat(buffer_fd, &sb);
    CheckError((ret == -1), -EBADFD, "@%s, Failed to get buffer status", __FUNCTION__);

    void* addr = mmap(0, sb.st_size, PROT_WRITE, MAP_SHARED, buffer_fd, 0);
    CheckError((!addr), -EBADFD, "@%s, Failed to map buffer", __FUNCTION__);

    int32_t handle = mHandleSeed++;
    mHandles[buffer_fd] = handle;

    mShmInfoMap[handle].fd = buffer_fd;
    mShmInfoMap[handle].addr = addr;
    mShmInfoMap[handle].size = sb.st_size;

    return handle;
}

int Intel3AServer::parseReqHeader(const uint8_t req_header[], uint32_t size)
{
    LOG1("@%s, size:%d", __FUNCTION__, size);
    if (size < IPC_REQUEST_HEADER_USED_NUM || req_header[0] != IPC_MATCHING_KEY) {
        LOGE("@%s, fail, request header size:%d, req_header[0]:%d", __FUNCTION__, size, req_header[0]);
        return -1;
    }

    return 0;
}

void Intel3AServer::returnCallback(uint32_t req_id, status_t status, int32_t buffer_handle)
{
    LOG2("@%s, req_id:%d:%s, status:%d", __FUNCTION__,
         req_id, Intel3AIpcCmdToString(static_cast<IPC_CMD>(req_id)), status);
    (*mCallback->return_callback)(mCallback, req_id, (status == OK ? 0 : 1), buffer_handle);
}

void Intel3AServer::handleRequest(MsgReq msg)
{
    uint32_t req_id = msg.req_id;
    int32_t buffer_handle = msg.buffer_handle;
    int reqeustSize = 0;
    void* addr = nullptr;

    if (buffer_handle != -1) {
        if (mShmInfoMap.find(buffer_handle) == mShmInfoMap.end()) {
            LOGE("@%s, Invalid buffer handle", __FUNCTION__);
            returnCallback(req_id, UNKNOWN_ERROR, buffer_handle);
            return;
        }
        ShmInfo info = mShmInfoMap[buffer_handle];
        LOG2("@%s, info.fd:%d, info.size:%zu", __FUNCTION__, info.fd, info.size);

        reqeustSize = info.size;
        addr = info.addr;
    }

    LOG1("@%s, req_id:%d:%s, reqeustSize:%d, addr:%p, buffer_handle:%d",
         __FUNCTION__, req_id, Intel3AIpcCmdToString(static_cast<IPC_CMD>(req_id)),
         reqeustSize, addr, buffer_handle);

    if (req_id != IPC_FACE_UNINIT && addr == nullptr) {
        LOGE("@%s, addr is nullptr", __FUNCTION__);
        returnCallback(req_id, UNKNOWN_ERROR, buffer_handle);
        return;
    }

    status_t status = OK;
    switch (req_id) {
        case IPC_3A_AIC_INIT:
            status = mAic.init(addr, reqeustSize);
            break;
        case IPC_3A_AIC_RUN:
            mAic.run(addr, reqeustSize);
            break;
        case IPC_3A_AIC_RESET:
            mAic.reset(addr, reqeustSize);
            break;
        case IPC_3A_AIC_GETAICVERSION:
            status = mAic.getAicVersion(addr, reqeustSize);
            break;
        case IPC_3A_AIC_GETAICCONFIG:
            status = mAic.getAicConfig(addr, reqeustSize);
            break;
        case IPC_3A_CMC_INIT:
            status = mCmc.ia_cmc_init(addr, reqeustSize);
            break;
        case IPC_3A_CMC_DEINIT:
            status = mCmc.ia_cmc_deinit(addr, reqeustSize);
            break;
        case IPC_3A_EXC_ANALOG_GAIN_TO_SENSOR:
            status = mExc.analog_gain_to_sensor_units(addr, reqeustSize);
            break;
        case IPC_3A_EXC_SENSOR_TO_ANALOG_GAIN:
            status = mExc.sensor_units_to_analog_gain(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_INIT:
            status = mAiq.aiq_init(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_DEINIT:
            status = mAiq.aiq_deinit(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_AE_RUN:
            status = mAiq.aiq_ae_run(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_AF_RUN:
            status = mAiq.aiq_af_run(addr, reqeustSize);;
            break;
        case IPC_3A_AIQ_AWB_RUN:
            status = mAiq.aiq_awb_run(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_PA_RUN:
            status = mAiq.aiq_pa_run(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_SA_RUN:
            status = mAiq.aiq_sa_run(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_GBCE_RUN:
            status = mAiq.aiq_gbce_run(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_GET_AIQ_DATA:
            status = mAiq.aiq_get_aiqd_data(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_STATISTICS_SET:
            status = mAiq.statistics_set(addr, reqeustSize);
            break;
        case IPC_3A_AIQ_GET_VERSION:
            status = mAiq.aiq_get_version(addr, reqeustSize);
            break;
        case IPC_3A_MKN_INIT:
            status = mMkn.init(addr, reqeustSize);
            break;
        case IPC_3A_MKN_UNINIT:
            status = mMkn.uninit(addr, reqeustSize);
            break;
        case IPC_3A_MKN_PREPARE:
            status = mMkn.prepare(addr, reqeustSize);
            break;
        case IPC_3A_MKN_ENABLE:
            status = mMkn.enable(addr, reqeustSize);
            break;
        case IPC_3A_COORDINATE_COVERT:
            status = mCoordinate.convert(addr, reqeustSize);
            break;
        case IPC_FACE_INIT:
            status = mFace.init(addr, reqeustSize);
            break;
        case IPC_FACE_UNINIT:
            status = mFace.uninit();
            break;
        case IPC_FACE_RUN:
            status = mFace.run(addr, reqeustSize);
            break;
        default:
            LOGE("@%s, req_id:%d is not defined", __FUNCTION__, req_id);
            status = UNKNOWN_ERROR;
            break;
    }

    returnCallback(req_id, status, buffer_handle);
}

void Intel3AServer::request(uint32_t req_id,
                            const uint8_t req_header[],
                            uint32_t size,
                            int32_t buffer_handle)
{
    LOG1("@%s, size:%d, buffer_handle:%d", __FUNCTION__, size, buffer_handle);
    LOG2("@%s, req_id:%d:%s", __FUNCTION__,
         req_id, Intel3AIpcCmdToString(static_cast<IPC_CMD>(req_id)));

    IPC_GROUP group = Intel3AIpcCmdToGroup(static_cast<IPC_CMD>(req_id));

    int ret = parseReqHeader(req_header, size);
    if (ret != 0) {
        returnCallback(req_id, UNKNOWN_ERROR, buffer_handle);
        return;
    }

    MsgReq msg = {req_id, buffer_handle};
    mThreads[group]->task_runner()->PostTask(FROM_HERE,
        base::Bind(&Intel3AServer::handleRequest, base::Unretained(this), msg));
}

void Intel3AServer::deregisterBuffers(const int32_t buffer_handles[], uint32_t size)
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


static int32_t initialize(const camera_algorithm_callback_ops_t* callback_ops)
{
    LOG1("@%s, callback_ops:%p", __FUNCTION__, callback_ops);
    return Intel3AServer::getInstance()->initialize(callback_ops);
}

static int32_t registerBuffer(int32_t buffer_fd)
{
    LOG1("@%s, buffer_fd:%d", __FUNCTION__, buffer_fd);
    return Intel3AServer::getInstance()->registerBuffer(buffer_fd);
}

static void request(uint32_t req_id,
                    const uint8_t req_header[],
                    uint32_t size,
                    int32_t buffer_handle)
{
    LOG1("@%s, size:%d, buffer_handle:%d", __FUNCTION__, size, buffer_handle);
    Intel3AServer::getInstance()->request(req_id, req_header, size, buffer_handle);
}

static void deregisterBuffers(const int32_t buffer_handles[], uint32_t size)
{
    LOG1("@%s, size:%d", __FUNCTION__, size);
    return Intel3AServer::getInstance()->deregisterBuffers(buffer_handles, size);
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

__attribute__((constructor)) void initIntel3AServer() {
    LogHelper::setDebugLevel();
    Intel3AServer::init();
}

__attribute__((destructor)) void deinitIntel3AServer() {
    Intel3AServer::deInit();
}

} /* namespace intel */
} /* namespace cros */
