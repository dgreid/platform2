/*
 * Copyright (C) 2016-2017 Intel Corporation.
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

#include "Camera3V4l2Format.h"
#define LOG_TAG "FrameWorker"

#include "FrameWorker.h"
#include "Camera3GFXFormat.h"
#include <sys/mman.h>

namespace android {
namespace camera2 {

FrameWorker::FrameWorker(std::shared_ptr<V4L2VideoNode> node,
                         int cameraId, size_t pipelineDepth, std::string name) :
        IDeviceWorker(cameraId),
        mIndex(0),
        mNode(node),
        mPollMe(false),
        mPipelineDepth(pipelineDepth)
{
    LOG1("%s handling node %s", name.c_str(), mNode->name());
}

FrameWorker::~FrameWorker()
{
}

status_t FrameWorker::configure(std::shared_ptr<GraphConfig> & /*config*/)
{
    return OK;
}

status_t FrameWorker::startWorker()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    status_t ret = mNode->start(0);
    if (ret != OK) {
        LOGE("Unable to start device: %s ret: %d", mNode->name(), ret);
    }

    return ret;
}

status_t FrameWorker::stopWorker()
{
    return mNode->stop(true);
}

status_t FrameWorker::setWorkerDeviceFormat(FrameInfo &frame)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    status_t ret = mNode->setFormat(frame);
    CheckAndLogError(ret != NO_ERROR, ret, "@%s set worker format failed",
                     __FUNCTION__);
    ret = mNode->getFormat(mFormat);
    CheckAndLogError(ret != NO_ERROR, ret, "@%s get worker format failed",
                     __FUNCTION__);

    return OK;
}

status_t FrameWorker::setWorkerDeviceBuffers(int memType, size_t extraBufferCount)
{
    for (unsigned int i = 0; i < mPipelineDepth + extraBufferCount; i++) {
        V4L2Buffer buffer;
        mBuffers.push_back(buffer);
    }
    status_t ret = mNode->setBufferPool(mBuffers, true, memType);
    if (ret != OK) {
        LOGE("Unable to set buffer pool, ret = %d", ret);
        return ret;
    }

    return OK;
}

status_t FrameWorker::allocateWorkerBuffers()
{
    int memType = mNode->getMemoryType();
    int lengthY = 0;
    int lengthUV = 0;
    int offsetY = 0;
    int offsetUV = 0;
    int numPlanes = numOfNonContiguousPlanes(mFormat.pixelformat());
    // Number of non contiguous planes can only be
    // 1 for NV12, NV21 and meta frame
    // 2 for NV12M and NV21M
    if (numPlanes > 2) {
        LOGE("@%s Unsupported pixelformat %s", __func__,
             v4l2Fmt2Str(mFormat.pixelformat()));
    }
    std::shared_ptr<CameraBuffer> buf = nullptr;
    for (unsigned int i = 0; i < mPipelineDepth; i++) {
        LOG2("@%s allocate format: %s size: %d %dx%d bytesperline: %d", __func__, v4l2Fmt2Str(mFormat.pixelformat()),
                mFormat.sizeimage(),
                mFormat.width(),
                mFormat.height(),
                mFormat.bytesperline());
        switch (memType) {
        case V4L2_MEMORY_MMAP:
            lengthY = mBuffers[i].length(0);
            offsetY = mBuffers[i].offset(0);
            if (numPlanes > 1) {
                lengthUV = mBuffers[i].length(1);
                offsetUV = mBuffers[i].offset(1);
            }
            buf = CameraBuffer::createMMapBuffer(mFormat.width(),
                    mFormat.height(),
                    mFormat.bytesperline(),
                    mNode->getFd(),
                    lengthY, lengthUV,
                    mFormat.pixelformat(),
                    offsetY, offsetUV,
                    PROT_READ | PROT_WRITE, MAP_SHARED);
            if (buf.get() == nullptr)
                return BAD_VALUE;
            break;
        default:
            LOGE("@%s Unsupported memory type %d", __func__, memType);
            return BAD_VALUE;
        }

        mBuffers[i].setBytesused(mFormat.sizeimage());
        mCameraBuffers.push_back(buf);
    }

    return OK;
}

} /* namespace camera2 */
} /* namespace android */

