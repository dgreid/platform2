/*
 * Copyright (C) 2014-2017 Intel Corporation.
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

#define LOG_TAG "StreamOut_Task"

#include "StreamOutputTask.h"
#include "LogHelper.h"
#include "CameraStream.h"

namespace android {
namespace camera2 {

StreamOutputTask::StreamOutputTask():
    ITaskEventSource(),
    mCaptureDoneCount(0)

{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
}

StreamOutputTask::~StreamOutputTask()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
}

status_t
StreamOutputTask::notifyPUTaskEvent(PUTaskMessage *puMsg)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    std::shared_ptr<CameraBuffer> buffer;
    Camera3Request* request;
    CameraStream *stream;

    if (puMsg == nullptr) {
        return UNKNOWN_ERROR;
    }

    if (puMsg->id == PU_TASK_MSG_ID_ERROR) {
        // ProcUnit Task error
        return UNKNOWN_ERROR;
    }

    switch (puMsg->event.type) {
    case PU_TASK_EVENT_BUFFER_COMPLETE:
    {
        buffer = puMsg->event.buffer.get();
        request = puMsg->event.request;

        if (buffer == nullptr) {
            LOGE("No buffer provided for captureDone ");
            return UNKNOWN_ERROR;
        }
        if (request == nullptr) {
            LOGE("No request provided for captureDone ");
            return UNKNOWN_ERROR;
        }

        stream = buffer->getOwner();

        // Dump the buffers if enabled in flags
        if (buffer->format() == HAL_PIXEL_FORMAT_BLOB) {
            buffer->dumpImage(CAMERA_DUMP_JPEG, "JPEG");
        } else if (buffer->format() == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
            if (stream->usage() & GRALLOC_USAGE_HW_VIDEO_ENCODER) {
                buffer->dumpImage(CAMERA_DUMP_VIDEO, "VIDEO");
            } else {
                buffer->dumpImage(CAMERA_DUMP_PREVIEW, "PREVIEW");
            }
        }

        // call capturedone for the stream of the buffer
        stream->captureDone(buffer, request);
        mCaptureDoneCount++;
        break;
    }
    default:
        LOGW("Unsupported ProcUnit Task event: %d", puMsg->event.type);
        break;
    }
    return OK;
}

void StreamOutputTask::cleanListeners()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    cleanListener();
}

}  // namespace camera2
}  // namespace android
