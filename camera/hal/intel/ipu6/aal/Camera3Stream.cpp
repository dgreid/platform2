/*
 * Copyright (C) 2017-2020 Intel Corporation
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

#define LOG_TAG "Camera3Stream"

#include "Camera3Stream.h"

#include <utility>
#include <vector>

#include "CameraDump.h"
#include "Errors.h"
#include "HALv3Utils.h"
#include "ICamera.h"
#include "MetadataConvert.h"
#include "PlatformData.h"
#include "Utils.h"
#include "stdlib.h"

namespace camera3 {
Camera3Stream::Camera3Stream(int cameraId, CallbackEventInterface* callback,
                             uint32_t maxNumReqInProc, const icamera::stream_t& halStream,
                             const camera3_stream_t& stream, const camera3_stream_t* inputStream,
                             bool isHALStream)
        : mCameraId(cameraId),
          mEventCallback(callback),
          mPostProcessType(icamera::POST_PROCESS_NONE),
          mStreamState(false),
          mHALStream(halStream),
          mMaxNumReqInProc(maxNumReqInProc),
          mBufferPool(nullptr),
          mStream(stream),
          mFaceDetection(nullptr),
          mFDRunDefaultInterval(icamera::PlatformData::faceEngineRunningInterval(cameraId)),
          mFDRunIntervalNoFace(icamera::PlatformData::faceEngineRunningIntervalNoFace(cameraId)),
          mFDRunInterval(icamera::PlatformData::faceEngineRunningInterval(cameraId)),
          mFrameCnt(0),
          mInputPostProcessType(icamera::POST_PROCESS_NONE),
          mIsHALStream(isHALStream) {
    LOG1("[%p]@%s, buf num:%d, inputStream:%p, stream:%dx%d, format:%d, type:%d", this,
         __func__, mMaxNumReqInProc, inputStream, mHALStream.width, mHALStream.height,
         mHALStream.format, mStream.stream_type);

    mPostProcessor = std::unique_ptr<PostProcessor>(new PostProcessor(mCameraId, stream));
    mCaptureRequest.clear();
    mQueuedBuffer.clear();
    if (mIsHALStream) {
        mBufferPool = std::unique_ptr<Camera3BufferPool>(new Camera3BufferPool());
    }

    if (inputStream) {
        LOG2("@%s, inputStream: width:%d, height:%d, format:%d", __func__, inputStream->width,
             inputStream->height, inputStream->format);

        mInputPostProcessor = std::unique_ptr<PostProcessor>(new PostProcessor(mCameraId, stream));
        mInputStream = std::unique_ptr<camera3_stream_t>(new camera3_stream_t);
        *mInputStream = *inputStream;
    }

    LOG2("@%s, mFaceDetection:%p, Interval:%d, IntervalNoFace:%d", __func__,
         mFaceDetection, mFDRunDefaultInterval, mFDRunIntervalNoFace);
}

Camera3Stream::~Camera3Stream() {
    LOG1("[%p]@%s", this, __func__);

    setActive(false);

    for (auto& buf : mBuffers) {
        buf.second->unlock();
    }

    mBuffers.clear();
    std::lock_guard<std::mutex> l(mLock);
    mCaptureResultMap.clear();
}

void Camera3Stream::sendEvent(const icamera::camera_msg_data_t& data) {
    LOG2("@%s receive sof event: %ld", __func__, data.data.buffer_ready.timestamp);

    std::lock_guard<std::mutex> sofLock(mSofLock);
    mSofCondition.notify_one();
}

void Camera3Stream::handleSofAlignment() {
    if (!icamera::PlatformData::swProcessingAlignWithIsp(mCameraId)) return;

    std::unique_lock<std::mutex> sofLock(mSofLock);
    std::cv_status ret =
        mSofCondition.wait_for(sofLock, std::chrono::nanoseconds(kMaxDuration * SLOWLY_MULTIPLIER));

    if (ret == std::cv_status::timeout) {
        LOGW("%s, [%p] wait sof timeout, skip alignment this time", __func__, this);
    }
    LOG2("%s, [%p] running post processing align with sof event", __func__, this);
}

bool Camera3Stream::threadLoop() {
    LOG1("[%p] isHALStream: %d @%s", this, mIsHALStream, __func__);

    if (!waitCaptureResultReady()) {
        return true;
    }

    auto captureResult = mCaptureResultMap.begin();
    std::shared_ptr<CaptureResult> result = captureResult->second;
    uint32_t frameNumber = captureResult->first;

    // dequeue buffer from HAL
    icamera::camera_buffer_t* buffer = nullptr;
    icamera::Parameters parameter;
    std::shared_ptr<Camera3Buffer> inputCam3Buf = result->inputCam3Buf;
    std::shared_ptr<StreamComInfo> halOutput = nullptr;
    long sequence = -1;

    if (!inputCam3Buf && mIsHALStream) {
        LOG1("[%p]@ dqbuf for frameNumber %d", this, frameNumber);
        int ret = icamera::camera_stream_dqbuf(mCameraId, mHALStream.id, &buffer, &parameter);
        CheckError(ret != icamera::OK || !buffer, true, "[%p]failed to dequeue buffer, ret %d",
                   this, ret);
        LOG2("[%p]@ %s, buffer->timestamp:%lld addr %p", this, __func__, buffer->timestamp,
             buffer->addr);

        sequence = buffer->sequence;
        int32_t userRequestId = 0;
        if (parameter.getUserRequestId(userRequestId) == icamera::OK) {
            {
                std::unique_lock<std::mutex> lock(mLock);
                if (mCaptureResultMap.find(static_cast<uint32_t>(userRequestId)) !=
                    mCaptureResultMap.end()) {
                    frameNumber = static_cast<uint32_t>(userRequestId);
                    result = mCaptureResultMap[frameNumber];
                }
            }
        }

        // sync before notify listeners
        bool needAlignment = false;

        if (mHALStream.usage != icamera::CAMERA_STREAM_OPAQUE_RAW &&
            mPostProcessType != icamera::POST_PROCESS_NONE) {
            needAlignment = true;
        } else {
            for (auto& iter : mListeners) {
                if (!iter->getCaptureRequest(frameNumber)) {
                    continue;
                }
                needAlignment = true;
            }
        }
        if (needAlignment) {
            handleSofAlignment();
        }

        halOutput = std::make_shared<StreamComInfo>();
        {
            std::unique_lock<std::mutex> lock(mLock);

            /* get the Camera3Buffer object of camera_buffer_t dqueued from HAL
             * check the 3 sources: HAL stream, listener stream and buffer pool.
             */
            if (mQueuedBuffer.find(frameNumber) != mQueuedBuffer.end()) {
                /* check buffer pool first, the HAL stream may use buffer from pool even
                 * itself or it's listener has requested buffer.
                 */
                halOutput->cam3Buf = mQueuedBuffer[frameNumber];
            } else if (mCaptureRequest.find(frameNumber) != mCaptureRequest.end()) {
                if (buffer->addr == mCaptureRequest[frameNumber]->cam3Buf->data()) {
                    halOutput->cam3Buf = mCaptureRequest[frameNumber]->cam3Buf;
                }
            } else {
                std::shared_ptr<StreamComInfo> request = nullptr;
                for (auto& iter : mListeners) {
                    request = iter->getCaptureRequest(frameNumber);
                    if (request && request->cam3Buf->data() == buffer->addr) {
                        halOutput->cam3Buf = request->cam3Buf;
                        break;
                    }
                }
            }

            CheckError(!halOutput->cam3Buf, true, "can't identify the buffer source");
            halOutput->parameter = parameter;
            halOutput->cam3Buf->setTimeStamp(buffer->timestamp);
        }

        for (auto& iter : mListeners) {
            iter->notifyListenerBufferReady(frameNumber, halOutput);
        }

        if (!getCaptureRequest(frameNumber)) {
            // HAL stream is triggered by listener, itself not requested, start next
            // loop
            std::unique_lock<std::mutex> lock(mLock);
            mCaptureResultMap.erase(frameNumber);
            return true;
        }
    } else if (!inputCam3Buf) {
        // listener stream get the buffer from HAL stream
        std::unique_lock<std::mutex> lock(mLock);
        if (mHALStreamOutput.find(frameNumber) == mHALStreamOutput.end()) {
            LOGE("[%p] can't find HAL stream output", this);
            return true;
        }
        halOutput = mHALStreamOutput[frameNumber];
        mHALStreamOutput.erase(frameNumber);
    }

    {
        std::unique_lock<std::mutex> lock(mLock);
        mCaptureResultMap.erase(frameNumber);
    }

    icamera::camera_buffer_t outCamBuf = {};
    std::shared_ptr<Camera3Buffer> outCam3Buf = nullptr;

    // start process buffers, HAL stream and listeners will do the same process
    if (!inputCam3Buf) {
        outCam3Buf = halOutput->cam3Buf;
        if (outCam3Buf) {
            outCamBuf = outCam3Buf->getHalBuffer();
        }
        parameter = halOutput->parameter;
    }

    buffer_handle_t handle = result->handle;
    std::shared_ptr<Camera3Buffer> ccBuf = nullptr;
    {
        std::unique_lock<std::mutex> lock(mLock);
        CheckError(mBuffers.find(handle) == mBuffers.end(), false, "can't find handle %p", handle);

        ccBuf = mBuffers[handle];
        mBuffers.erase(handle);
        CheckError(ccBuf == nullptr, false, "ccBuf is nullptr");
    }

    if (inputCam3Buf || mHALStream.usage == icamera::CAMERA_STREAM_OPAQUE_RAW) {
        // notify shutter done
        ShutterEvent shutterEvent = {frameNumber, inputCam3Buf ? 0 : outCamBuf.timestamp};
        mEventCallback->shutterDone(shutterEvent);

        // notify metadata done
        MetadataEvent event = {frameNumber, &parameter};
        mEventCallback->metadataDone(event);
    }

    int dumpOutputFmt = V4L2_PIX_FMT_NV12;
    if (!inputCam3Buf && mHALStream.usage != icamera::CAMERA_STREAM_OPAQUE_RAW) {
        LOG2("%s, hal buffer: %p, ccBuf address: %p", __func__, outCamBuf.addr, ccBuf->data());
        if (mPostProcessType & icamera::POST_PROCESS_JPEG_ENCODING) {
            dumpOutputFmt = V4L2_PIX_FMT_JPEG;
            icamera::PlatformData::acquireMakernoteData(mCameraId, outCamBuf.timestamp, &parameter);
        }
        // handle normal postprocess
        if (mPostProcessType != icamera::POST_PROCESS_NONE) {
            LOG2("%s, do software postProcessing for sequence: %ld", __func__, outCamBuf.sequence);
            icamera::status_t status =
                mPostProcessor->doPostProcessing(outCam3Buf, parameter, ccBuf);
            CheckError(status != icamera::OK, true,
                       "@%s, doPostProcessing fails, mPostProcessType:%d", __func__,
                       mPostProcessType);
        } else if (outCam3Buf && outCam3Buf->data() != ccBuf->data()) {
            MEMCPY_S(ccBuf->data(), ccBuf->size(), outCam3Buf->data(), outCam3Buf->size());
        }
    } else if (inputCam3Buf) {
        parameter = result->param;
        LOG1("[%p] @%s process input frameNumber: %d", this, __func__, frameNumber);
        if (mInputPostProcessType & icamera::POST_PROCESS_JPEG_ENCODING) {
            dumpOutputFmt = V4L2_PIX_FMT_JPEG;
            icamera::PlatformData::acquireMakernoteData(mCameraId, inputCam3Buf->getTimeStamp(),
                                                        &parameter);
        }

        inputCam3Buf->dumpImage(frameNumber, icamera::DUMP_AAL_INPUT, V4L2_PIX_FMT_NV12);
        if (mInputPostProcessType != icamera::POST_PROCESS_NONE) {
            icamera::status_t status =
                mInputPostProcessor->doPostProcessing(inputCam3Buf, parameter, ccBuf);
            CheckError(status != icamera::OK, true,
                       "@%s, doPostProcessing fails, mInputPostProcessType:%d", __func__,
                       mInputPostProcessType);
        } else {
            MEMCPY_S(ccBuf->data(), ccBuf->size(), inputCam3Buf->data(), inputCam3Buf->size());
        }
    }

    faceRunningByCondition(ccBuf->getHalBuffer());

    if (mHALStream.usage != icamera::CAMERA_STREAM_OPAQUE_RAW) {
        ccBuf->dumpImage(frameNumber, icamera::DUMP_AAL_OUTPUT, dumpOutputFmt);
    }
    ccBuf->unlock();
    ccBuf->deinit();
    ccBuf->getFence(&result->outputBuffer);

    // notify frame done
    BufferEvent bufferEvent = {frameNumber, &result->outputBuffer, 0, -1};
    if (mHALStream.usage == icamera::CAMERA_STREAM_OPAQUE_RAW) {
        bufferEvent.sequence = sequence;
        bufferEvent.timestamp = outCamBuf.timestamp;
    }
    mEventCallback->bufferDone(bufferEvent);

    if (!inputCam3Buf) {
        std::unique_lock<std::mutex> lock(mLock);
        mCaptureRequest.erase(frameNumber);
    }

    return true;
}

void Camera3Stream::faceRunningByCondition(const icamera::camera_buffer_t& buffer) {
    if (!mFaceDetection) return;

    LOG2("[%p]@%s", this, __func__);

    /*
       FD runs 1 frame every mFDRunInterval frames.
       And the default value of mFDRunInterval is mFDRunDefaultInterval
    */
    if (mFrameCnt % mFDRunInterval == 0) {
        mFaceDetection->runFaceDetection(buffer);
    }

    /*
       When face doesn't be detected during mFDRunIntervalNoFace's frame,
       we may change FD running's interval frames.
    */
    if (mFDRunIntervalNoFace > mFDRunDefaultInterval) {
        static unsigned int noFaceCnt = 0;
        int faceNum = mFaceDetection->getFaceNum();

        /*
           The purpose of changing the value of the variable is to run FD
           immediately when face is detected.
        */
        if (faceNum == 0) {
            if (mFDRunInterval != mFDRunIntervalNoFace) {
                noFaceCnt = ++noFaceCnt % mFDRunIntervalNoFace;
                if (noFaceCnt == 0) {
                    mFDRunInterval = mFDRunIntervalNoFace;
                }
            }
        } else {
            if (mFDRunInterval != mFDRunDefaultInterval) {
                mFDRunInterval = mFDRunDefaultInterval;
                mFrameCnt = mFDRunInterval - 1;
                noFaceCnt = 0;
            }
        }
        LOG2("%s, Currently running one time face detection every %d frames", __func__,
             mFDRunInterval);
    }

    mFrameCnt = ++mFrameCnt % mFDRunInterval;
}

void Camera3Stream::requestExit() {
    LOG1("[%p]@%s", this, __func__);

    icamera::Thread::requestExit();
    std::lock_guard<std::mutex> l(mLock);

    mBufferDoneCondition.notify_one();

    if (mFaceDetection) {
        icamera::FaceDetection::destoryInstance(mCameraId);
        mFaceDetection = nullptr;
    }
}

int Camera3Stream::processRequest(const std::shared_ptr<Camera3Buffer>& inputCam3Buf,
                                  const camera3_stream_buffer_t& outputBuffer,
                                  uint32_t frame_number) {
    LOG1("[%p] isHALStream: %d @%s", this, mIsHALStream, __func__);

    std::shared_ptr<Camera3Buffer> ccBuf = std::make_shared<Camera3Buffer>();
    buffer_handle_t handle = *outputBuffer.buffer;

    {
        std::unique_lock<std::mutex> lock(mLock);
        CheckError(mBuffers.find(handle) != mBuffers.end(), icamera::BAD_VALUE,
                   "handle %p is duplicated!", handle);

        mBuffers[handle] = ccBuf;
    }

    icamera::status_t status = ccBuf->init(&outputBuffer, mCameraId);
    CheckError(status != icamera::OK, icamera::BAD_VALUE, "Failed to init CameraBuffer");
    status = ccBuf->waitOnAcquireFence();
    CheckError(status != icamera::OK, icamera::BAD_VALUE, "Failed to sync CameraBuffer");
    status = ccBuf->lock();
    CheckError(status != icamera::OK, icamera::BAD_VALUE, "Failed to lock buffer");

    std::shared_ptr<StreamComInfo> streamInfo = std::make_shared<StreamComInfo>();
    streamInfo->cam3Buf = nullptr;
    {
        std::lock_guard<std::mutex> l(mLock);

        if (inputCam3Buf) {
            LOG1("[%p] frameNumber %d input buffer requested ", this, frame_number);
            return icamera::OK;
        }

        if (mPostProcessType == icamera::POST_PROCESS_NONE) {
            streamInfo->cam3Buf = ccBuf;
        }
        mCaptureRequest[frame_number] = streamInfo;
    }

    return icamera::OK;
}

void Camera3Stream::queueBufferDone(uint32_t frameNumber,
                                    const std::shared_ptr<Camera3Buffer>& inputCam3Buf,
                                    const camera3_stream_buffer_t& outputBuffer,
                                    const icamera::Parameters& param) {
    LOG1("[%p]@%s, frameNumber:%d", this, __func__, frameNumber);
    std::lock_guard<std::mutex> l(mLock);

    std::shared_ptr<CaptureResult> result = std::make_shared<CaptureResult>();

    result->frameNumber = frameNumber;
    result->outputBuffer = outputBuffer;
    result->handle = *outputBuffer.buffer;
    result->outputBuffer.buffer = &result->handle;
    result->inputCam3Buf = inputCam3Buf;
    result->param = param;

    mCaptureResultMap[frameNumber] = result;
    mBufferDoneCondition.notify_one();
}

int Camera3Stream::setActive(bool state) {
    LOG1("[%p]@%s isHALStream: %d state %d", this, __func__, mIsHALStream, state);

    if (!mStreamState && state) {
        std::string threadName = "Cam3Stream-";
        threadName += std::to_string(mHALStream.id);

        // Run Camera3Stream thread
        run(threadName);

        if (mHALStream.usage != icamera::CAMERA_STREAM_OPAQUE_RAW) {
            // configure the post processing.
            // Note: the mHALStream may be changed after calling this function
            mPostProcessor->configure(mStream, mHALStream);
            mPostProcessType = mPostProcessor->getPostProcessType();
            LOG2("@%s, mPostProcessType:%d", __func__, mPostProcessType);
        }

        if (mIsHALStream) {
            mBufferPool->createBufferPool(mCameraId, mMaxNumReqInProc, mHALStream);
            LOG2("@%s, HAL stream create BufferPool", __func__);
        }

        if (mInputPostProcessor) {
            mInputPostProcessor->configure(mStream, *mInputStream.get());
            mInputPostProcessType = mInputPostProcessor->getPostProcessType();
        }
    } else if (mStreamState && !state) {
        mPostProcessType = icamera::POST_PROCESS_NONE;

        if (mInputPostProcessor) {
            mInputPostProcessType = icamera::POST_PROCESS_NONE;
        }

        if (mBufferPool) {
            mBufferPool->destroyBufferPool();
        }

        // Exit Camera3Stream thread
        requestExit();
    }

    mStreamState = state;

    return icamera::OK;
}

void Camera3Stream::activateFaceDetection(unsigned int maxFaceNum) {
    LOG1("[%p]@%s maxFaceNum %d, mCameraId %d", this, __func__, maxFaceNum, mCameraId);

    mFaceDetection = icamera::FaceDetection::createInstance(mCameraId, maxFaceNum, mHALStream.id,
                                                            mHALStream.width, mHALStream.height);
}

void Camera3Stream::addListener(Camera3Stream* listener) {
    mListeners.push_back(listener);
}

/* fetch the buffers will be queued to Hal, the buffer has 3 sources:
 * 1st using HAL stream's request, then the buffer->addr should equal
 *     request->cam3Buf->addr()
 * 2nd if using listener buffer directly
 * 3rd using bufferpool, the buffer from pool is stored in mQueuedBuffer
 */
bool Camera3Stream::fetchRequestBuffers(icamera::camera_buffer_t* buffer, uint32_t frameNumber) {
    if (!mIsHALStream) return false;
    LOG1("[%p]@%s isHALStream: %d frameNumber %d", this, __func__, mIsHALStream, frameNumber);

    int requestStreamCount = 0;
    std::shared_ptr<StreamComInfo> request = nullptr;
    std::shared_ptr<Camera3Buffer> buf = nullptr;

    // check if any one provided a buffer in listeners
    for (auto& iter : mListeners) {
        request = iter->getCaptureRequest(frameNumber);
        if (request) {
            requestStreamCount++;
            buf = request->cam3Buf ? request->cam3Buf : buf;
        }
    }

    // if HAL stream has a buffer, use HAL stream's buffer to qbuf/dqbuf
    request = getCaptureRequest(frameNumber);
    if (request) {
        requestStreamCount++;
        buf = request->cam3Buf ? request->cam3Buf : buf;
    }

    if (!requestStreamCount) {
        // no stream requested
        return false;
    }

    /* Fix me... if has 2 or more streams, use the same buffer.
     ** if >= 2 streams request in same frame, we use buffer pool temporary.
     ** to do: prefer to use user buffer to avoid memcpy
     */
    if (!buf || requestStreamCount >= 2) {
        LOG1("[%p]@%s get buffer from pool", this, __func__);
        if (mBufferPool) {
            buf = mBufferPool->acquireBuffer();
            CheckError(buf == nullptr, false, "no available internal buffer");
            // using buffer pool, store the buffer, then can return it when frame done
            mQueuedBuffer[frameNumber] = buf;
        } else {
            return false;
        }
    }

    *buffer = buf->getHalBuffer();
    // Fill the specific setting
    buffer->s.usage = mHALStream.usage;
    buffer->s.id = mHALStream.id;

    return true;
}

// to check if a HW stream is triggered by it's listener
void Camera3Stream::checkListenerRequest(uint32_t frameNumber) {
    if (!mIsHALStream) return;

    LOG1("[%p]@%s, frameNumber:%d", this, __func__, frameNumber);

    bool listenerRequested = false;
    for (auto& iter : mListeners) {
        listenerRequested |= iter->getCaptureRequest(frameNumber) != nullptr;
    }

    if (!getCaptureRequest(frameNumber) && listenerRequested) {
        // HW stream is enabled by listener's request
        std::shared_ptr<CaptureResult> result = std::make_shared<CaptureResult>();
        LOG1("[%p]@%s, frameNumber:%d, only listener requested", this, __func__,
             frameNumber);
        if (result) {
            std::lock_guard<std::mutex> l(mLock);
            result->frameNumber = frameNumber;
            result->inputCam3Buf = nullptr;
            mCaptureResultMap[frameNumber] = result;
        }
        mBufferDoneCondition.notify_one();
    }
}

void Camera3Stream::notifyListenerBufferReady(uint32_t frameNumber,
                                              const std::shared_ptr<StreamComInfo>& halOutput) {
    LOG1("[%p] @%s", this, __func__);
    std::lock_guard<std::mutex> l(mLock);
    if (mCaptureRequest.find(frameNumber) != mCaptureRequest.end()) {
        mHALStreamOutput[frameNumber] = halOutput;
        mBufferDoneCondition.notify_one();
    }
}

std::shared_ptr<StreamComInfo> Camera3Stream::getCaptureRequest(uint32_t frameNumber) {
    std::lock_guard<std::mutex> l(mLock);
    std::shared_ptr<StreamComInfo> request = nullptr;

    if (mCaptureRequest.find(frameNumber) != mCaptureRequest.end()) {
        request = mCaptureRequest[frameNumber];
    }
    return request;
}

bool Camera3Stream::waitCaptureResultReady() {
    std::unique_lock<std::mutex> lock(mLock);
    /* 1st loop, the HAL and listener stream wait on the CaptureResult
     * BufferDoneCondition if the CaptureResultVector not empty.
     * 2nd loop, the CaptureResult is not empty, and the HAL stream will start to
     * dqbuf the listener stream should wait HAL Stream send out buffer ready event
     * if it doesn't have inputCam3Buf.
     * 3rd loop, (listener stream only) both vecotr are not empty, return true.
     */
    // HAL stream and listener stream should wait RequestManager notification
    bool needWaitBufferReady = mCaptureResultMap.empty();
    // listeners stream should wait HAL output buffer if not has input buffer
    if (!mIsHALStream && !mCaptureResultMap.empty()) {
        auto captureResult = mCaptureResultMap.begin();
        std::shared_ptr<CaptureResult> result = captureResult->second;
        needWaitBufferReady = !result->inputCam3Buf && mHALStreamOutput.empty();
    }
    if (needWaitBufferReady) {
        std::cv_status ret = mBufferDoneCondition.wait_for(
            lock, std::chrono::nanoseconds(kMaxDuration * SLOWLY_MULTIPLIER));
        if (ret == std::cv_status::timeout) {
            LOGW("[%p]%s, wait buffer ready time out", this, __func__);
        }
        // return false to make the threadLoop run again
        return false;
    }

    return true;
}

void Camera3Stream::requestStreamDone(uint32_t frameNumber) {
    if (!mIsHALStream) return;

    LOG1("[%p] @%s frameNumber: %d", this, __func__, frameNumber);

    /* release buffers. if the buffer used to qbuf/dqbuf is from listener or HAL
     * stream, it will be released in its stream, because now we use buffer pool
     * to sync buffer between frames.
     */
    std::unique_lock<std::mutex> lock(mLock);
    if (mQueuedBuffer.find(frameNumber) != mQueuedBuffer.end()) {
        // if HAL stream using buffer from pool to qbuf/dqbuf, return it
        mBufferPool->returnBuffer(mQueuedBuffer[frameNumber]);
        mQueuedBuffer.erase(frameNumber);
    }
}
}  // namespace camera3
