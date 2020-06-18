/*
 * Copyright (C) 2016-2020 Intel Corporation.
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

#define LOG_TAG "OutputFrameWorker"

#include "Camera3GFXFormat.h"
#include "LogHelper.h"
#include "NodeTypes.h"
#include "OutputFrameWorker.h"

namespace cros {
namespace intel {

OutputFrameWorker::OutputFrameWorker(std::shared_ptr<cros::V4L2VideoNode> node, int cameraId,
                camera3_stream_t* stream, IPU3NodeNames nodeName, size_t pipelineDepth, FaceEngine* faceEngine) :
                FrameWorker(node, cameraId, pipelineDepth, "OutputFrameWorker"),
                mStream(stream),
                mNeedPostProcess(false),
                mNodeName(nodeName),
                mProcessor(cameraId),
                mSensorOrientation(0),
                mFaceEngine(faceEngine),
                mFaceEngineRunInterval(PlatformData::faceEngineRunningInterval(cameraId)),
                mFrameCnt(0),
                mCamOriDetector(nullptr),
                mCameraThread("OutputFrameWorker" + std::to_string(nodeName)),
                mDoAsyncProcess(false)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    if (mNode) {
        LOG1("@%s, node name:%d, device name:%s, mStream:%p", __FUNCTION__,
        nodeName, mNode->Name().c_str(), mStream);
    }
    if (stream) {
        LOG1("@%s, node name:%d, width:%d, height:%d, format:%x, type:%d", __FUNCTION__,
        nodeName, stream->width, stream->height, stream->format, stream->stream_type);
    }

    if (!mCameraThread.Start()) {
        LOGE("Camera thread failed to start");
    }
    LOG2("@%s, mStream:%p, mFaceEngine:%p, mFaceEngineRunInterval:%d",
         __FUNCTION__, mStream, mFaceEngine, mFaceEngineRunInterval);

    if (mFaceEngine) {
        struct camera_info info;
        PlatformData::getCameraInfo(cameraId, &info);
        mSensorOrientation = info.orientation;
        mCamOriDetector = std::unique_ptr<CameraOrientationDetector>(new CameraOrientationDetector(info.facing));
        mCamOriDetector->prepare();
    }
}

OutputFrameWorker::~OutputFrameWorker()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    mCameraThread.Stop();
}

void OutputFrameWorker::addListener(camera3_stream_t* stream)
{
    if (stream != nullptr) {
        LOG1("stream %p has listener %p", mStream, stream);
        mListeners.push_back(stream);
    }
}

void OutputFrameWorker::clearListeners()
{
    mListeners.clear();
}

status_t OutputFrameWorker::configure(std::shared_ptr<GraphConfig> &/*config*/)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

    status_t ret = mNode->GetFormat(&mFormat);
    if (ret != OK)
        return ret;

    LOG1("@%s allocate format: %s size: %d %dx%d", __func__, v4l2Fmt2Str(mFormat.PixelFormat()),
            mFormat.SizeImage(0),
            mFormat.Width(),
            mFormat.Height());

    ret = mProcessor.configure(mStream, mFormat.Width(),
                               mFormat.Height());
    CheckError((ret != OK), ret, "@%s mProcessor.configure failed %d",
               __FUNCTION__, ret);
    mNeedPostProcess = mProcessor.needPostProcess();

    mIndex = 0;
    // If using internal buffer, only one buffer is required.
    ret = setWorkerDeviceBuffers(getDefaultMemoryType(mNodeName));
    CheckError((ret != OK), ret, "@%s set worker device buffers failed.",
               __FUNCTION__);

    // Allocate internal buffer.
    if (mNeedPostProcess || mListeners.size() > 0) {
        int gfxFormat = v4L2Fmt2GFXFmt(mFormat.PixelFormat());
        if (gfxFormat == HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL) {
          // Buffer manager does not support
          // HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL. Use fake
          // HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED instead.
          gfxFormat = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
        }
        ret = allocateWorkerBuffers(GRALLOC_USAGE_SW_READ_OFTEN |
                                    GRALLOC_USAGE_HW_CAMERA_WRITE, gfxFormat);
        CheckError(ret != OK, ret, "@%s failed to allocate internal buffer.",
                   __FUNCTION__);
        mInternalBuffers.clear();
        std::shared_ptr<CameraBuffer> buffer = nullptr;
        for (size_t i = 0; i < mPipelineDepth; i++) {
            buffer = std::make_shared<CameraBuffer>();
            buffer->init(mFormat.Width(), mFormat.Height(), gfxFormat,
                                 mBufferHandles[i], mCameraId);
            mInternalBuffers.push_back(buffer);
        }
    }

    mListenerProcessors.clear();
    for (size_t i = 0; i < mListeners.size(); i++) {
        camera3_stream_t* listener = mListeners[i];
        std::unique_ptr<SWPostProcessor> processor(new SWPostProcessor(mCameraId));
        processor->configure(listener, mFormat.Width(),
                             mFormat.Height());
        mListenerProcessors.push_back(std::move(processor));
    }

    return OK;
}

status_t OutputFrameWorker::prepareRun(std::shared_ptr<DeviceMessage> msg)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);
    mMsg = msg;
    mPollMe = false;
    status_t status = NO_ERROR;

    mProcessingData.mOutputBuffer = nullptr;
    mProcessingData.mWorkingBuffer = nullptr;
    mProcessingData.mMsg = nullptr;
    mDoAsyncProcess = false;

    if (!mStream) {
        return NO_ERROR;
    }

    Camera3Request* request = mMsg->cbMetadataMsg.request;
    std::shared_ptr<CameraBuffer> buffer = findBuffer(request, mStream);
    if (buffer.get()) {
        // Work for mStream
        LOG2("@%s, stream:%p, mStream:%p", __FUNCTION__,
             buffer->getOwner()->getStream(), mStream);
        buffer->setRequestId(request->getId());
        status = prepareBuffer(buffer);
        if (status != NO_ERROR) {
            LOGE("prepare buffer error!");
            buffer->getOwner()->captureDone(buffer, request);
            return status;
        }
    } else if (!checkListenerBuffer(request)) {
        LOG2("No work for this worker mStream: %p", mStream);
        return NO_ERROR;
    }

    /*
     * store the buffer in a map where the key is the terminal UID
     */
    if (!mNeedPostProcess) {
        // Use stream buffer for zero-copy
        std::shared_ptr<CameraBuffer> listenerBuf = buffer;
        if (listenerBuf.get() == nullptr) {
            listenerBuf = mInternalBuffers[mIndex];
            CheckError((listenerBuf.get() == nullptr), UNKNOWN_ERROR,
                       "failed to allocate listener buffer");
        }
        mBuffers[mIndex].SetFd(listenerBuf->dmaBufFd(), 0);
    }
    LOG2("%s mBuffers[%d].fd: %d, %s", __FUNCTION__, mIndex, mBuffers[mIndex].Fd(0),
          mNode->Name().c_str());
    status |= mNode->PutFrame(&mBuffers[mIndex]);
    CheckError(status < 0, status, "failed to put frame");

    {
    std::lock_guard<std::mutex> l(mProcessingDataLock);
    ProcessingData processingData;
    processingData.mOutputBuffer = buffer;
    if (mNeedPostProcess || buffer.get() == nullptr)
        processingData.mWorkingBuffer = mInternalBuffers[mIndex];
    else
        processingData.mWorkingBuffer = buffer;
    processingData.mMsg = msg;

    if (isAsyncProcessingNeeded(processingData.mOutputBuffer)) {
        LOG2("process request async, mStream %p in req %d", mStream, request->getId());
        mProcessingDataQueue.push(std::move(processingData));
        mDoAsyncProcess = true;
    } else {
        LOG2("process request sync, mStream %p in req %d", mStream, request->getId());
        mProcessingData = std::move(processingData);
    }
    }

    mPollMe = true;
    return OK;
}

status_t OutputFrameWorker::run()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);
    if (mMsg == nullptr) {
        LOGE("Message not found - Fix the bug");
        return UNKNOWN_ERROR;
    }

    if (!mPollMe) {
        LOG1("No work for this worker");
        return OK;
    }

    cros::V4L2Buffer outBuf;
    LOG2("%s mBuffers[%d].fd: %d, %s", __FUNCTION__, mIndex, mBuffers[mIndex].Fd(0),
          mNode->Name().c_str());
    status_t status = mNode->GrabFrame(&outBuf);
    return (status < 0) ? status : OK;
}

status_t OutputFrameWorker::postRun()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);

    mIndex = (mIndex + 1) % mPipelineDepth;

    CheckError(mMsg == nullptr, UNKNOWN_ERROR, "Message null - Fix the bug");

    Camera3Request* request = mMsg->cbMetadataMsg.request;
    if (request == nullptr) {
        LOGE("No request provided for captureDone");
        mMsg = nullptr;
        return UNKNOWN_ERROR;
    }

    if (mDoAsyncProcess) {
        {
        std::lock_guard<std::mutex> l(mProcessingDataLock);
        if (mProcessingDataQueue.empty()) {
            LOG1("No processing data available!");
            mMsg = nullptr;
            return OK;
        }
        }

        base::Callback<status_t()> closure =
                base::Bind(&OutputFrameWorker::handlePostRun, base::Unretained(this));

        mCameraThread.PostTaskAsync<status_t>(FROM_HERE, closure);
    } else if (mProcessingData.mMsg != nullptr) {
        processData(std::move(mProcessingData));
    }

    mMsg = nullptr;
    return OK;
}

status_t OutputFrameWorker::handlePostRun()
{
    ProcessingData processingData;
    {
    std::lock_guard<std::mutex> l(mProcessingDataLock);
    LOG2("%s, line %d, queue size %ld", __func__, __LINE__, mProcessingDataQueue.size());
    processingData = std::move(mProcessingDataQueue.front());
    mProcessingDataQueue.pop();
    }

    return processData(std::move(processingData));
}

bool OutputFrameWorker::isAsyncProcessingNeeded(std::shared_ptr<CameraBuffer> outBuf)
{
    if (mNeedPostProcess && outBuf != nullptr) return true;

    Camera3Request* request = mMsg->cbMetadataMsg.request;
    if (request->hasInputBuf()) {
        return true;
    }
    for (size_t i = 0; i < mListeners.size(); i++) {
        std::shared_ptr<CameraBuffer> listenerBuf = findBuffer(request, mListeners[i]);
        if (listenerBuf != nullptr && mListenerProcessors[i]->needPostProcess()) {
            return true;
        }
    }
    return false;
}

status_t OutputFrameWorker::processData(ProcessingData processingData)
{
    status_t status = OK;
    CameraStream *stream = nullptr;

    Camera3Request* request = processingData.mMsg->cbMetadataMsg.request;
    bool needReprocess = request->hasInputBuf();

    // Handle for listeners at first
    for (size_t i = 0; i < mListeners.size(); i++) {
        camera3_stream_t* listener = mListeners[i];
        std::shared_ptr<CameraBuffer> listenerBuf = findBuffer(request, listener);
        if (listenerBuf.get() == nullptr) {
            continue;
        }

        listenerBuf->setRequestId(request->seqenceId());

        status = prepareBuffer(listenerBuf);
        CheckError(status != NO_ERROR, status, "prepare listener buffer error!");

        stream = listenerBuf->getOwner();
        if (mListenerProcessors[i]->needPostProcess()) {
            status = mListenerProcessors[i]->processFrame(
                                                 processingData.mWorkingBuffer,
                                                 listenerBuf,
                                                 processingData.mMsg->pMsg.processingSettings,
                                                 request, needReprocess);
            CheckError(status != OK, status, "@%s, process for listener %p failed! [%d]!", __FUNCTION__,
                     listener, status);
        } else {
            if (!processingData.mWorkingBuffer->isLocked()) {
                status_t ret = processingData.mWorkingBuffer->lock();
                CheckError(ret != NO_ERROR, NO_MEMORY, "@%s, lock fails", __FUNCTION__);
            }

            MEMCPY_S(listenerBuf->data(), listenerBuf->size(),
                     processingData.mWorkingBuffer->data(), processingData.mWorkingBuffer->size());
        }

        dump(listenerBuf, stream);

        stream->captureDone(listenerBuf, request);
        LOG2("%s, line %d, req id %d frameDone",  __func__, __LINE__, request->seqenceId());
    }

    if (processingData.mOutputBuffer == nullptr) {
        if (needReprocess) {
            const camera3_stream_buffer* inputBuf = request->getInputBuffer();
            CheckError(!inputBuf, UNKNOWN_ERROR, "@%s, getInputBuffer fails", __FUNCTION__);

            int fmt = inputBuf->stream->format;
            CheckError((fmt != HAL_PIXEL_FORMAT_YCbCr_420_888), UNKNOWN_ERROR,
            "@%s, input stream is not YCbCr_420_888, format:%x", __FUNCTION__, fmt);

            const CameraStream* s = request->getInputStream();
            CheckError(!s, UNKNOWN_ERROR, "@%s, getInputStream fails", __FUNCTION__);

            std::shared_ptr<CameraBuffer> buf = request->findBuffer(s);
            CheckError((buf == nullptr), UNKNOWN_ERROR, "@%s, findBuffer fails", __FUNCTION__);

            buf->getOwner()->captureDone(buf, request);
        }
        LOG2("No buffer provided for captureDone");
        return OK;
    }

    stream = processingData.mOutputBuffer->getOwner();
    if (mNeedPostProcess || needReprocess) {
        status = mProcessor.processFrame(processingData.mWorkingBuffer,
                                         processingData.mOutputBuffer,
                                         processingData.mMsg->pMsg.processingSettings,
                                         request,
                                         needReprocess);
        CheckError(status != OK, status, "@%s, postprocess failed! [%d]!", __FUNCTION__, status);
    }

    dump(processingData.mOutputBuffer, stream);

    if (mFaceEngine &&
        (mFaceEngine->getMode() != FD_MODE_OFF) &&
        (mFrameCnt % mFaceEngineRunInterval == 0)) {
        if (!processingData.mOutputBuffer->isLocked()) {
            status_t ret = processingData.mOutputBuffer->lock();
            CheckError(ret != NO_ERROR, NO_MEMORY, "@%s, lock fails", __FUNCTION__);
        }

        pvl_image image;
        image.data = static_cast<uint8_t*>(processingData.mOutputBuffer->data());
        image.width = processingData.mOutputBuffer->width();
        image.height = processingData.mOutputBuffer->height();
        image.stride = processingData.mOutputBuffer->stride();
        image.size = image.stride * image.height;
        image.format = pvl_image_format_gray;
        image.rotation = (mSensorOrientation + mCamOriDetector->getOrientation()) % 360;
        mFaceEngine->run(image);
    }
    mFrameCnt = ++mFrameCnt % mFaceEngineRunInterval;

    // call capturedone for the stream of the buffer
    stream->captureDone(processingData.mOutputBuffer, request);
    LOG2("%s, line %d, req id %d frameDone",  __func__, __LINE__, request->seqenceId());

    return status;
}

bool OutputFrameWorker::isHalUsingRequestBuffer()
{
    LOG2("%s, mNeedPostProcess %d, mListeners.size() %zu",
          __FUNCTION__, mNeedPostProcess, mListeners.size());
    return (mNeedPostProcess || mListeners.size() > 0);
}

status_t
OutputFrameWorker::prepareBuffer(std::shared_ptr<CameraBuffer>& buffer)
{
    CheckError((buffer.get() == nullptr), UNKNOWN_ERROR, "null buffer!");

    status_t status = NO_ERROR;
    if (!buffer->isLocked() && isHalUsingRequestBuffer()) {
        status = buffer->lock();
        if (CC_UNLIKELY(status != NO_ERROR)) {
            LOGE("Could not lock the buffer error %d", status);
            return UNKNOWN_ERROR;
        }
    }
    status = buffer->waitOnAcquireFence();
    if (CC_UNLIKELY(status != NO_ERROR)) {
        LOGW("Wait on fence for buffer %p timed out", buffer.get());
    }
    return status;
}

std::shared_ptr<CameraBuffer>
OutputFrameWorker::findBuffer(Camera3Request* request,
                              camera3_stream_t* stream)
{
    CheckError((request == nullptr || stream == nullptr), nullptr,
                "null request/stream!");

    CameraStream *s = nullptr;
    std::shared_ptr<CameraBuffer> buffer = nullptr;
    const std::vector<camera3_stream_buffer>* outBufs = request->getOutputBuffers();
    CheckError(outBufs == nullptr, nullptr, "@%s: outBufs is nullptr", __FUNCTION__);

    for (camera3_stream_buffer outputBuffer : *outBufs) {
        s = reinterpret_cast<CameraStream *>(outputBuffer.stream->priv);
        if (s->getStream() == stream) {
            buffer = request->findBuffer(s, false);
            if (CC_UNLIKELY(buffer == nullptr)) {
                LOGW("buffer not found for stream");
            }
            break;
        }
    }

    if (buffer.get() == nullptr) {
        LOG2("No buffer for stream %p in req %d", stream, request->getId());
    }
    return buffer;
}

bool OutputFrameWorker::checkListenerBuffer(Camera3Request* request)
{
    bool required = false;
    for (auto* s : mListeners) {
        std::shared_ptr<CameraBuffer> listenerBuf = findBuffer(request, s);
        if (listenerBuf.get()) {
            required = true;
            break;
        }
    }

    LOG2("%s, required is %s", __FUNCTION__, (required ? "true" : "false"));
    return required;
}

void OutputFrameWorker::dump(std::shared_ptr<CameraBuffer> buf, const CameraStream* stream)
{
    CheckError(stream == nullptr, VOID_VALUE, "@%s, stream is nullptr", __FUNCTION__);
    LOG2("@%s", __FUNCTION__);

    if (buf->format() == HAL_PIXEL_FORMAT_BLOB) {
        buf->dumpImage(CAMERA_DUMP_JPEG, ".jpg");
    } else if (buf->format() == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED ||
               buf->format() == HAL_PIXEL_FORMAT_YCbCr_420_888) {
        if (stream->usage() & GRALLOC_USAGE_HW_VIDEO_ENCODER) {
            buf->dumpImage(CAMERA_DUMP_VIDEO, "video.nv12");
        } else {
            buf->dumpImage(CAMERA_DUMP_PREVIEW, "preview.nv12");
        }
    }
}
} /* namespace intel */
} /* namespace cros */
