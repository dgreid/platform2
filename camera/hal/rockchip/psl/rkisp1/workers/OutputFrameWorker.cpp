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

#define LOG_TAG "OutputFrameWorker"

#include "LogHelper.h"
#include "PerformanceTraces.h"
#include "OutputFrameWorker.h"
#include "ColorConverter.h"
#include "NodeTypes.h"
#include "Camera3V4l2Format.h"
#include "ImageScalerCore.h"
#include <libyuv.h>
#include <sys/mman.h>

namespace android {
namespace camera2 {

OutputFrameWorker::OutputFrameWorker(std::shared_ptr<V4L2VideoNode> node, int cameraId,
                camera3_stream_t* stream, NodeTypes nodeName, size_t pipelineDepth) :
                FrameWorker(node, cameraId, pipelineDepth, "OutputFrameWorker"),
                mDummyBuffer(nullptr),
                mDummyIndex(0),
                mOutputBuffer(nullptr),
                mWorkingBuffer(nullptr),
                mStream(stream),
                mNeedPostProcess(false),
                mNodeName(nodeName),
                mCameraThread("OutputFrameWorker"),
                mProcessor(cameraId),
                mPostProcFramePool("PostProcFramePool")
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    if (mNode)
        LOG1("@%s, node name:%d, device name:%s", __FUNCTION__, nodeName, mNode->name());

    if (!mCameraThread.Start()) {
        LOGE("Camera thread failed to start");
    }
}

OutputFrameWorker::~OutputFrameWorker()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    if (mOutputForListener.get() && mOutputForListener->isLocked()) {
        mOutputForListener->unlock();
    }

    mCameraThread.Stop();
}

status_t
OutputFrameWorker::handleMessageProcess(MessageProcess msg)
{
    status_t status = mStreamToSWProcessMap[msg.frame->stream]->processFrame(msg.frame->processBuffer,
                                                  msg.frame->listenBuffer,
                                                  msg.frame->processingSettings,
                                                  msg.frame->request);
    if (status != OK) {
        LOGE("@%s, process for listener %p failed! [%d]!", __FUNCTION__,
             msg.frame->stream, status);
        msg.frame->request->setError();
    }

    CameraStream *stream = msg.frame->listenBuffer->getOwner();
    stream->captureDone(msg.frame->listenBuffer, msg.frame->request);
    return status;
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
    mPostProcFramePool.deInit();
}

status_t OutputFrameWorker::allocListenerProcessBuffers()
{
    mPostProcFramePool.init(mPipelineDepth);
    for (size_t i = 0; i < mPipelineDepth; i++)
    {
        std::shared_ptr<PostProcFrame> frame = nullptr;
        mPostProcFramePool.acquireItem(frame);
        if (frame.get() == nullptr) {
            LOGE("postproc task busy, no idle postproc frame!");
            return UNKNOWN_ERROR;
        }
        frame->processBuffer = CameraBuffer::allocateHeapBuffer(mFormat.width(),
                                              mFormat.height(),
                                              mFormat.bytesperline(),
                                              mFormat.pixelformat(),
                                              mCameraId);
        if (frame->processBuffer.get() == nullptr)
            return NO_MEMORY;

        LOG2("%s:%d: postproc buffer allocated, address(%p)", __func__, __LINE__, frame->processBuffer.get());
    }
    return OK;
}

status_t OutputFrameWorker::allocDummyBuffer()
{
    auto buffer = CameraBuffer::allocateHandleBuffer(
            mFormat.width(), mFormat.height(),
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
            GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_CAMERA_WRITE,
            mCameraId);
    if (!buffer) {
        return NO_MEMORY;
    }
    mDummyBuffer = std::move(buffer);
    return OK;
}

status_t OutputFrameWorker::configure(std::shared_ptr<GraphConfig> &/*config*/)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    bool listenerNeedPostProcess = false;
    status_t ret = mNode->getFormat(mFormat);
    if (ret != OK)
        return ret;

    LOG1("@%s allocate format: %s size: %d %dx%d", __func__, v4l2Fmt2Str(mFormat.pixelformat()),
            mFormat.sizeimage(),
            mFormat.width(),
            mFormat.height());

    ret = mProcessor.configure(mStream, mFormat.width(),
                               mFormat.height(), mFormat.pixelformat());
    CheckAndLogError((ret != OK), ret, "@%s mProcessor.configure failed %d",
                     __FUNCTION__, ret);
    mNeedPostProcess = mProcessor.needPostProcess();

    mIndex = 0;
    mDummyIndex = 0;
    mOutputBuffers.resize(mPipelineDepth);
    mWorkingBuffers.resize(mPipelineDepth);

    // Allocate extra slots for the dummy buffer.
    ret = setWorkerDeviceBuffers(
        mNeedPostProcess ? V4L2_MEMORY_MMAP : getDefaultMemoryType(mNodeName), mPipelineDepth);
    CheckAndLogError((ret != OK), ret, "@%s set worker device buffers failed.",
                     __FUNCTION__);

    ret = allocDummyBuffer();
    CheckAndLogError((ret != OK), ret, "@%s failed to allocate dummy buffer.",
                     __FUNCTION__);

    // Allocate internal buffer.
    if (mNeedPostProcess) {
        ret = allocateWorkerBuffers();
        CheckAndLogError((ret != OK), ret,
                         "@%s failed to allocate internal buffer.",
                         __FUNCTION__);
    }

    mStreamToSWProcessMap.clear();
    for (size_t i = 0; i < mListeners.size(); i++) {
        camera3_stream_t* listener = mListeners[i];
        std::unique_ptr<SWPostProcessor> processor(new SWPostProcessor(mCameraId));
        processor->configure(listener, mFormat.width(),
                             mFormat.height());
        mStreamToSWProcessMap[listener] = std::move(processor);
        if (mStreamToSWProcessMap[listener]->needPostProcess())
            listenerNeedPostProcess = true;
    }
    if (listenerNeedPostProcess)
        allocListenerProcessBuffers();

    return OK;
}

status_t OutputFrameWorker::prepareRun(std::shared_ptr<DeviceMessage> msg)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    mMsg = msg;
    status_t status = NO_ERROR;
    size_t bufIndex = mIndex;

    mOutputBuffers[bufIndex] = nullptr;
    mPollMe = true;

    if (!mStream)
        return NO_ERROR;

    Camera3Request* request = mMsg->cbMetadataMsg.request;
    request->setSequenceId(-1);

    FrameInfo config;
    mNode->getConfig(config);
    int numPlanes = numOfNonContiguousPlanes(config.format);

    std::shared_ptr<CameraBuffer> buffer = findBuffer(request, mStream);
    if (buffer.get()) {
        // Work for mStream
        LOG2("@%s, stream:%p, mStream:%p", __FUNCTION__,
             buffer->getOwner()->getStream(), mStream);
        status = prepareBuffer(buffer);
        if (status != NO_ERROR) {
            LOGE("prepare buffer error!");
            goto exit;
        }

        // If output format is something else than
        // NV21 or Android flexible YCbCr 4:2:0, return
        if (buffer->format() != HAL_PIXEL_FORMAT_YCrCb_420_SP &&
                buffer->format() != HAL_PIXEL_FORMAT_YCbCr_420_888 &&
                buffer->format() != HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
            buffer->format() != HAL_PIXEL_FORMAT_BLOB)  {
            LOGE("Bad format %d", buffer->format());
            status = BAD_TYPE;
            goto exit;
        }

        mOutputBuffers[bufIndex] = buffer;
    } else if (checkListenerBuffer(request)) {
        // Work for listeners
        LOG2("%s: stream %p works for listener only in req %d",
             __FUNCTION__, mStream, request->getId());
    } else {
        LOG2("No work for this worker mStream: %p; use dummy buffer", mStream);
        bufIndex = mPipelineDepth + mDummyIndex;
        mDummyIndex = (mDummyIndex + 1) % mPipelineDepth;
    }

    /*
     * store the buffer in a map where the key is the terminal UID
     */
    if (bufIndex >= mPipelineDepth) {
        switch (mNode->getMemoryType()) {
        case V4L2_MEMORY_DMABUF:
            mBuffers[bufIndex].setNumPlanes(numPlanes);
            for (int plane = 0; plane < numPlanes; plane++) {
                mBuffers[bufIndex].setFd(mDummyBuffer->dmaBufFd(plane), plane);
                mBuffers[bufIndex].get()->m.planes[plane].data_offset = mDummyBuffer->dmaBufFdOffset(plane);
            }
            LOG2("%s mBuffers[%d].fd: %d", __FUNCTION__, bufIndex, mBuffers[bufIndex].fd());
            break;
        case V4L2_MEMORY_MMAP:
            LOG2("%s mBuffers[%d].offset: 0x%x",
                __FUNCTION__, bufIndex, mBuffers[bufIndex].offset());
            break;
        default:
            LOGE("%s unsupported memory type.", __FUNCTION__);
            status = BAD_VALUE;
            goto exit;
        }
    } else if (!mNeedPostProcess) {
        // Use stream buffer for zero-copy
        if (buffer.get() == nullptr) {
            buffer = getOutputBufferForListener();
            CheckAndLogError((buffer.get() == nullptr), UNKNOWN_ERROR,
                             "failed to allocate listener buffer");
        }
        switch (mNode->getMemoryType()) {
        case V4L2_MEMORY_DMABUF:
            mBuffers[bufIndex].setNumPlanes(numPlanes);
            for (int plane = 0; plane < numPlanes; plane++) {
                mBuffers[bufIndex].setFd(buffer->dmaBufFd(plane), plane);
                mBuffers[bufIndex].get()->m.planes[plane].data_offset = buffer->dmaBufFdOffset(plane);
            }
            LOG2("%s mBuffers[%d].fd: %d", __FUNCTION__, bufIndex, mBuffers[bufIndex].fd());
            break;
        case V4L2_MEMORY_MMAP:
            LOG2("%s mBuffers[%d].offset: 0x%x",
                __FUNCTION__, bufIndex, mBuffers[bufIndex].offset());
            break;
        default:
            LOGE("%s unsupported memory type.", __FUNCTION__);
            status = BAD_VALUE;
            goto exit;
        }
        mWorkingBuffers[bufIndex] = buffer;
    } else {
        mWorkingBuffers[bufIndex] = mCameraBuffers[bufIndex];
    }
    status |= mNode->putFrame(mBuffers[bufIndex]);
    LOG2("%s:%d:instance(%p), requestId(%d), index(%d)", __func__, __LINE__, this, request->getId(), bufIndex);
    if (bufIndex < mPipelineDepth) {
        mIndex = (mIndex + 1) % mPipelineDepth;
    }

exit:
    if (status < 0)
        returnBuffers(true);

    return status < 0 ? status : OK;
}

status_t OutputFrameWorker::run()
{
    status_t status = NO_ERROR;
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);

    V4L2BufferInfo outBuf;

    if (!mDevError)
        status = mNode->grabFrame(&outBuf);

    int index = outBuf.vbuffer.index();

    if (index >= mPipelineDepth) {
        // Dummy buffer. We don't need to do anything.
        return NO_ERROR;
    }

    // Update request sequence if needed
    Camera3Request* request = mMsg->cbMetadataMsg.request;
    int sequence = outBuf.vbuffer.sequence();
    if (request->sequenceId() < sequence)
        request->setSequenceId(sequence);

    if (mDevError) {
        for (int i = 0; i < mPipelineDepth; i++)
            if (mOutputBuffers[(i + mIndex) % mPipelineDepth]) {
                index = (i + mIndex) % mPipelineDepth;
                break;
            }
    }
    mOutputBuffer = mOutputBuffers[index];
    mWorkingBuffer = mWorkingBuffers[index];
    mOutputBuffers[index] = nullptr;
    mWorkingBuffers[index] = nullptr;

    ICaptureEventListener::CaptureMessage outMsg;
    outMsg.data.event.reqId = request->getId();
    outMsg.id = ICaptureEventListener::CAPTURE_MESSAGE_ID_EVENT;
    outMsg.data.event.type = ICaptureEventListener::CAPTURE_EVENT_SHUTTER;
    outMsg.data.event.timestamp = outBuf.vbuffer.timestamp();
    outMsg.data.event.sequence = outBuf.vbuffer.sequence();
    notifyListeners(&outMsg);

    LOG2("%s:%d:instance(%p), frame_id(%d), requestId(%d), index(%d)", __func__, __LINE__, this, outBuf.vbuffer.sequence(), request->getId(), index);

    if (status < 0)
        returnBuffers(true);

    return status < 0 ? status : OK;
}

status_t OutputFrameWorker::postRun()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);

    status_t status = OK;
    CameraStream *stream;
    Camera3Request* request = nullptr;

    if (mMsg == nullptr) {
        LOGE("Message null - Fix the bug");
        status = UNKNOWN_ERROR;
        goto exit;
    }

    request = mMsg->cbMetadataMsg.request;
    if (request == nullptr) {
        LOGE("No request provided for captureDone");
        status = UNKNOWN_ERROR;
        goto exit;
    }

    // Handle for listeners at first
    for (size_t i = 0; i < mListeners.size(); i++) {
        camera3_stream_t* listener = mListeners[i];
        std::shared_ptr<CameraBuffer> listenerBuf = findBuffer(request, listener);
        if (listenerBuf.get() == nullptr) {
            continue;
        }

        stream = listenerBuf->getOwner();
        if (NO_ERROR != prepareBuffer(listenerBuf)) {
            LOGE("prepare listener buffer error!");
            listenerBuf->getOwner()->captureDone(listenerBuf, request);
            status = UNKNOWN_ERROR;
            continue;
        }
        if (mStreamToSWProcessMap[listener]->needPostProcess()) {
            MessageProcess msg;
            mPostProcFramePool.acquireItem(msg.frame);

            msg.frame->request = request;
            msg.frame->stream = listener;
            msg.frame->processingSettings = mMsg->pMsg.processingSettings;
            msg.frame->listenBuffer = listenerBuf;

            MEMCPY_S(msg.frame->processBuffer->data(), msg.frame->processBuffer->size(),
                     mWorkingBuffer->data(), mWorkingBuffer->size());

            base::Callback<status_t()> closure =
                base::Bind(&OutputFrameWorker::handleMessageProcess,
                        base::Unretained(this),
                        base::Passed(std::move(msg)));
            mCameraThread.PostTaskAsync(FROM_HERE, closure);
        } else {
            MEMCPY_S(listenerBuf->data(), listenerBuf->size(),
                     mWorkingBuffer->data(), mWorkingBuffer->size());
            stream->captureDone(listenerBuf, request);
        }
    }
    if (status != OK)
        goto exit;

    // All done
    if (mOutputBuffer == nullptr)
        goto exit;

    stream = mOutputBuffer->getOwner();
    if (mNeedPostProcess) {
        status = mProcessor.processFrame(mWorkingBuffer,
                                         mOutputBuffer,
                                         mMsg->pMsg.processingSettings,
                                         request);
    }
    if (status != OK) {
        LOGE("@%s, postprocess failed! [%d]!", __FUNCTION__, status);
        goto exit;
    }

    // Dump the buffers if enabled in flags
    if (mOutputBuffer->format() == HAL_PIXEL_FORMAT_BLOB) {
        mOutputBuffer->dumpImage(CAMERA_DUMP_JPEG, ".jpg");
    } else if (mOutputBuffer->format() ==
                              HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED
            || mOutputBuffer->format() == HAL_PIXEL_FORMAT_YCbCr_420_888) {
        if (stream->usage() & GRALLOC_USAGE_HW_VIDEO_ENCODER) {
            mOutputBuffer->dumpImage(CAMERA_DUMP_VIDEO, "VIDEO");
        } else {
            mOutputBuffer->dumpImage(CAMERA_DUMP_PREVIEW, "PREVIEW");
        }
    }
    // call capturedone for the stream of the buffer
    stream->captureDone(mOutputBuffer, request);

exit:
    /* Prevent from using old data */
    mMsg = nullptr;
    mOutputBuffer = nullptr;

    if (status != OK)
        returnBuffers(false);

    return status;
}

bool OutputFrameWorker::isHalUsingRequestBuffer()
{
    LOG2("%s, mNeedPostProcess %d, mListeners.size() %zu",
          __FUNCTION__, mNeedPostProcess, mListeners.size());
    return (mNeedPostProcess || mListeners.size() > 0);
}

void OutputFrameWorker::returnBuffers(bool returnListenerBuffers)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);

    if (!mMsg || !mMsg->cbMetadataMsg.request)
        return;

    Camera3Request* request = mMsg->cbMetadataMsg.request;
    std::shared_ptr<CameraBuffer> buffer;

    buffer = findBuffer(request, mStream);
    if (buffer.get() && buffer->isRegistered())
        buffer->getOwner()->captureDone(buffer, request);

    if (!returnListenerBuffers)
        return;

    for (size_t i = 0; i < mListeners.size(); i++) {
        camera3_stream_t* listener = mListeners[i];
        buffer = findBuffer(request, listener);
        if (buffer.get() == nullptr || !buffer->isRegistered())
            continue;

        buffer->getOwner()->captureDone(buffer, request);
    }
}

status_t
OutputFrameWorker::prepareBuffer(std::shared_ptr<CameraBuffer>& buffer)
{
    CheckAndLogError((buffer.get() == nullptr), UNKNOWN_ERROR, "null buffer!");

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
    CheckAndLogError((request == nullptr || stream == nullptr), nullptr,
                     "null request/stream!");

    CameraStream *s = nullptr;
    std::shared_ptr<CameraBuffer> buffer = nullptr;
    const std::vector<camera3_stream_buffer>* outBufs =
                                        request->getOutputBuffers();
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

std::shared_ptr<CameraBuffer>
OutputFrameWorker::getOutputBufferForListener()
{
    // mOutputForListener buffer infor is same with mOutputBuffer,
    // and only allocated once
    if (mOutputForListener.get() == nullptr) {
        // Allocate buffer for listeners
        if (mNode->getMemoryType() == V4L2_MEMORY_DMABUF) {
            mOutputForListener = CameraBuffer::allocateHandleBuffer(
                    mFormat.width(),
                    mFormat.height(),
                    HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
                    GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_CAMERA_WRITE,
                    mCameraId);
        } else if (mNode->getMemoryType() == V4L2_MEMORY_MMAP) {
            int lengthY = mBuffers[0].length();
            int offsetY = mBuffers[0].offset();
            int lengthUV = 0;
            int offsetUV = 0;
            if (numOfNonContiguousPlanes(mFormat.pixelformat()) > 1) {
                lengthUV = mBuffers[0].length(1);
                offsetUV = mBuffers[0].length(1);
            }
            mOutputForListener = CameraBuffer::createMMapBuffer(
                    mFormat.width(),
                    mFormat.height(),
                    mFormat.bytesperline(),
                    mNode->getFd(),
                    lengthY, lengthUV,
                    mFormat.pixelformat(),
                    offsetY, offsetUV, PROT_READ | PROT_WRITE, MAP_SHARED);
        } else if (mNode->getMemoryType() == V4L2_MEMORY_USERPTR) {
            mOutputForListener = CameraBuffer::allocateHeapBuffer(
                    mFormat.width(),
                    mFormat.height(),
                    mFormat.bytesperline(),
                    mFormat.pixelformat(),
                    mCameraId);
        } else {
            LOGE("bad type for stream buffer %d", mNode->getMemoryType());
            return nullptr;
        }
        CheckAndLogError((mOutputForListener.get() == nullptr), nullptr,
                         "Can't allocate buffer for listeners!");
    }

    if (!mOutputForListener->isLocked()) {
        mOutputForListener->lock();
    }

    LOG2("%s, get output buffer for Listeners", __FUNCTION__);
    return mOutputForListener;
}

OutputFrameWorker::SWPostProcessor::SWPostProcessor(int cameraId) :
    mCameraId(cameraId),
    mProcessType(PROCESS_NONE),
    mStream(nullptr)
{
}

OutputFrameWorker::SWPostProcessor::~SWPostProcessor()
{
    mPostProcessBufs.clear();
}

status_t OutputFrameWorker::SWPostProcessor::configure(
                               camera3_stream_t* outStream,
                               int inputW, int inputH,
                               int inputFmt)
{
    mProcessType = PROCESS_NONE;

    if (outStream == nullptr) {
        LOG1("%s, stream is nullptr", __FUNCTION__);
        return OK;
    }
    // Only support NV12 and NV12M
    CheckAndLogError(
        (inputFmt != V4L2_PIX_FMT_NV12 && (inputFmt != V4L2_PIX_FMT_NV12M)),
        BAD_VALUE, "Don't support format 0x%x, %s", inputFmt,
        v4l2Fmt2Str(inputFmt));

    int type = PROCESS_NONE;
    if (getRotationDegrees(outStream) > 0) {
        type |= PROCESS_CROP_ROTATE_SCALE;
    }
    if (outStream->format == HAL_PIXEL_FORMAT_BLOB) {
        type |= PROCESS_JPEG_ENCODING;
    }
    if (inputW * inputH < outStream->width * outStream->height) {
        type |= PROCESS_SCALING;
    } else if (!(type & PROCESS_JPEG_ENCODING)
         && inputW * inputH > outStream->width * outStream->height) {
        // Don't need dowscaling for jpeg, because jpeg encoder support it
        type |= PROCESS_SCALING;
    }

    if ((type & PROCESS_JPEG_ENCODING) && !mJpegTask.get()) {
        LOG2("Create JpegEncodeTask");
        mJpegTask.reset(new JpegEncodeTask(mCameraId));

        if (mJpegTask->init() != NO_ERROR) {
            LOGE("Failed to init JpegEncodeTask Task");
            mJpegTask.reset();
            return UNKNOWN_ERROR;
        }
    }

    LOG1("%s: postprocess type 0x%x for stream %p", __FUNCTION__,
         type, outStream);
    mProcessType = type;
    mStream = outStream;
    mPostProcessBufs.clear();

    return OK;
}

status_t OutputFrameWorker::SWPostProcessor::processFrame(
                               std::shared_ptr<CameraBuffer>& input,
                               std::shared_ptr<CameraBuffer>& output,
                               std::shared_ptr<ProcUnitSettings>& settings,
                               Camera3Request* request)
{
    if (mProcessType == PROCESS_NONE) {
        return NO_ERROR;
    }

    status_t status = OK;

    // Rotate input buffer is mPostProcessBufs[0]
    // and output buffer will be mPostProcessBufs[1] or directly mOutputBuffer
    if (mProcessType & PROCESS_CROP_ROTATE_SCALE) {
        int angle = getRotationDegrees(mStream);
        // Check if any post-processing needed after rotate
        if (mProcessType & PROCESS_JPEG_ENCODING
            || mProcessType & PROCESS_SCALING) {
            if (mPostProcessBufs.empty()
                || mPostProcessBufs[0]->width() != input->width()
                || mPostProcessBufs[0]->height() != input->height()) {
                mPostProcessBufs.clear();
                // Create rotate output working buffer
                std::shared_ptr<CameraBuffer> buf;
                buf = CameraBuffer::allocateHeapBuffer(
                         input->width(),
                         input->height(),
                         input->width(),
                         input->v4l2Fmt(),
                         mCameraId,
                         PAGE_ALIGN(input->size()));
                CheckAndLogError((buf.get() == nullptr), NO_MEMORY,
                                 "@%s, No memory for rotate", __FUNCTION__);
                mPostProcessBufs.push_back(buf);
            }
            // Rotate to internal post-processing buffer
            status = ImageScalerCore::cropRotateScaleFrame(
                    input, mPostProcessBufs[0], angle, mRotateBuffer, mScaleBuffer);
        } else {
            // Rotate to internal post-processing buffer
            status = ImageScalerCore::cropRotateScaleFrame(
                    input, output, angle, mRotateBuffer, mScaleBuffer);
        }
        CheckAndLogError((status != OK), status,
                         "@%s, Scale frame failed! [%d]!", __FUNCTION__,
                         status);
    } else {
        if (!mPostProcessBufs.empty())
            mPostProcessBufs.erase(mPostProcessBufs.begin());

        mPostProcessBufs.insert(mPostProcessBufs.begin(), input);
    }

    // Scale input buffer is mPostProcessBufs[0]
    // and output buffer will be mPostProcessBufs[1] or directly mOutputBuffer
    if (mProcessType & PROCESS_SCALING) {
        if (mProcessType & PROCESS_JPEG_ENCODING) {
            if (mPostProcessBufs.empty()
                || mPostProcessBufs.back()->width() != mStream->width
                || mPostProcessBufs.back()->height() != mStream->height) {
                // Create scale output working buffer
                std::shared_ptr<CameraBuffer> buf;
                buf = CameraBuffer::allocateHeapBuffer(
                         mStream->width,
                         mStream->height,
                         mStream->width,
                         mPostProcessBufs.back()->v4l2Fmt(),
                         mCameraId,
                         PAGE_ALIGN(mStream->width * mStream->height * 3 / 2));
                CheckAndLogError((buf.get() == nullptr), NO_MEMORY,
                                 "@%s, No memory for scale", __FUNCTION__);
                mPostProcessBufs.push_back(buf);
            }
            // Scale to internal post-processing buffer
            status = ImageScalerCore::scaleFrame(mPostProcessBufs[0], mPostProcessBufs[1]);
        } else {
            // Scale to output dst buffer
            status = ImageScalerCore::scaleFrame(mPostProcessBufs[0], output);
        }
        CheckAndLogError((status != OK), status,
                         "@%s, Scale frame failed! [%d]!", __FUNCTION__,
                         status);
    }

    // Jpeg input buffer is always mPostProcessBufs.back()
    if (mProcessType & PROCESS_JPEG_ENCODING) {
        mPostProcessBufs.back()->dumpImage(CAMERA_DUMP_JPEG,
                                           "before_jpeg_converion_nv12");
        // JPEG encoding
        status = mJpegTask->handleMessageSettings(*(settings.get()));
        CheckAndLogError((status != OK), status,
                         "@%s, set settings failed! [%d]!", __FUNCTION__,
                         status);
        status = convertJpeg(mPostProcessBufs.back(), output, request);
        CheckAndLogError((status != OK), status,
                         "@%s, JPEG conversion failed! [%d]!", __FUNCTION__,
                         status);
    }

    return status;
}

int OutputFrameWorker::SWPostProcessor::getRotationDegrees(
                                  camera3_stream_t* stream) const
{
    CheckAndLogError((stream == nullptr), 0, "%s, stream is nullptr",
                     __FUNCTION__);

    if (stream->stream_type != CAMERA3_STREAM_OUTPUT) {
        LOG1("%s, no need rotation for stream type %d", __FUNCTION__,
             stream->stream_type);
        return 0;
    }

    if (stream->crop_rotate_scale_degrees == CAMERA3_STREAM_ROTATION_90)
        return 90;
    else if (stream->crop_rotate_scale_degrees == CAMERA3_STREAM_ROTATION_270)
        return 270;

    return 0;
}

/**
 * Do jpeg conversion
 * \param[in] input
 * \param[in] output
 * \param[in] request
 */
status_t OutputFrameWorker::SWPostProcessor::convertJpeg(
                               std::shared_ptr<CameraBuffer> input,
                               std::shared_ptr<CameraBuffer> output,
                               Camera3Request *request)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    ITaskEventListener::PUTaskEvent msg;
    msg.buffer = output;
    msg.jpegInputbuffer = input;
    msg.request = request;

    status_t status = NO_ERROR;
    if (mJpegTask.get()) {
        status = mJpegTask->handleMessageNewJpegInput(msg);
    }

    return status;
}

} /* namespace camera2 */
} /* namespace android */
