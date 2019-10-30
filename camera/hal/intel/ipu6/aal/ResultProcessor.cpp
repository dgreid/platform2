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

#define LOG_TAG "ResultProcessor"

#include "ResultProcessor.h"

#include <mutex>

#include "Errors.h"
#include "HALv3Utils.h"
#include "MetadataConvert.h"
#include "PlatformData.h"
#include "Utils.h"

namespace camera3 {

#define META_ENTRY_COUNT 256
#define META_DATA_COUNT 80000
#define FPS_FRAME_COUNT 60  // the frame interval to print fps

MetadataMemory::MetadataMemory() : mMeta(META_ENTRY_COUNT, META_DATA_COUNT), mMemory(nullptr) {}

MetadataMemory::~MetadataMemory() {
    // Return memory to metadata
    getMetadata();
}

android::CameraMetadata* MetadataMemory::getMetadata() {
    if (mMemory) {
        mMeta.acquire(mMemory);
        mMemory = nullptr;
    }
    return &mMeta;
}

camera_metadata_t* MetadataMemory::getMemory() {
    if (!mMemory) {
        mMemory = mMeta.release();
    }
    return mMemory;
}

void MetadataMemory::copyMetadata(const camera_metadata_t* src) {
    getMemory();
    // Clear old metadata
    mMemory = place_camera_metadata(mMemory, get_camera_metadata_size(mMemory),
                                    get_camera_metadata_entry_capacity(mMemory),
                                    get_camera_metadata_data_capacity(mMemory));
    getMetadata();
    mMeta.append(src);
}

ResultProcessor::ResultProcessor(int cameraId, const camera3_callback_ops_t* callback,
                                 RequestManagerCallback* requestManagerCallback)
        : mCameraId(cameraId),
          mCallbackOps(callback),
          mLastSettings(nullptr),
          mRequestManagerCallback(requestManagerCallback) {
    UNUSED(mCameraId);
    LOG1("@%s, mCameraId %d", __func__, mCameraId);

    mLastSettings = acquireMetadataMemory();

    mResultThread = std::unique_ptr<ResultThread>(new ResultThread(cameraId, this));

    mCamera3AMetadata = new Camera3AMetadata(mCameraId);
    gettimeofday(&mRequestTime, nullptr);

    mLastParams.sensorExposure = -1;
    mLastParams.sensorIso = -1;
}

ResultProcessor::~ResultProcessor() {
    LOG1("@%s", __func__);

    for (auto& item : mRequestStateVector) {
        releaseMetadataMemory(item.metaResult);
    }
    mRequestStateVector.clear();

    releaseMetadataMemory(mLastSettings);
    while (mMetadataVector.size() > 0) {
        LOG1("%s: release meta %p", __func__, mMetadataVector.back());
        delete mMetadataVector.back();
        mMetadataVector.pop_back();
    }

    delete mCamera3AMetadata;

    mInputCam3Bufs.clear();
}

void ResultProcessor::callbackNotify(const icamera::camera_msg_data_t& data) {
    LOG2("@%s, type %d", __func__, data.type);

    mResultThread->sendEvent(data);
}

int ResultProcessor::registerRequest(const camera3_capture_request_t* request,
                                     std::shared_ptr<Camera3Buffer> inputCam3Buf) {
    LOG1("@%s frame_number:%u, inputCam3Buf:%p", __func__, request->frame_number,
         inputCam3Buf.get());

    RequestState req;
    req.frameNumber = request->frame_number;
    req.buffersToReturn = request->num_output_buffers;
    req.partialResultCount = 1;

    std::lock_guard<std::mutex> l(mLock);
    // Copy settings
    if (request->settings) {
        mLastSettings->copyMetadata(request->settings);
    }

    req.metaResult = acquireMetadataMemory();
    req.metaResult->copyMetadata(mLastSettings->getMemory());

    if (inputCam3Buf) {
        mInputCam3Bufs[req.frameNumber] = inputCam3Buf;
    }
    mRequestStateVector.push_back(req);

    return icamera::OK;
}

void ResultProcessor::notifyError() {
    std::lock_guard<std::mutex> l(mLock);

    camera3_notify_msg_t notifyMsg;
    notifyMsg.type = CAMERA3_MSG_ERROR;
    notifyMsg.message.error = {0, nullptr, CAMERA3_MSG_ERROR_DEVICE};

    mCallbackOps->notify(mCallbackOps, &notifyMsg);
    LOGW("%s, Camera error happened", __func__);
}

int ResultProcessor::shutterDone(const ShutterEvent& event) {
    std::lock_guard<std::mutex> l(mLock);
    bool inputBuffer = mInputCam3Bufs.find(event.frameNumber) != mInputCam3Bufs.end();
    for (uint32_t i = 0; i < mRequestStateVector.size(); i++) {
        if (mRequestStateVector.at(i).frameNumber != event.frameNumber ||
            mRequestStateVector.at(i).isShutterDone) {
            continue;
        }

        camera3_notify_msg_t notifyMsg;
        notifyMsg.type = CAMERA3_MSG_SHUTTER;
        notifyMsg.message.shutter.frame_number = mRequestStateVector.at(i).frameNumber;
        notifyMsg.message.shutter.timestamp = event.timestamp;

        MetadataMemory* metaResult = mRequestStateVector[i].metaResult;
        android::CameraMetadata* meta = metaResult->getMetadata();
        if (!inputBuffer) {
            meta->update(ANDROID_SENSOR_TIMESTAMP,
                         reinterpret_cast<const int64_t*>(&event.timestamp), 1);
        } else {
            // update shutter timestamp if there is input stream
            camera_metadata_entry entry = meta->find(ANDROID_SENSOR_TIMESTAMP);
            if (entry.count == 1) {
                notifyMsg.message.shutter.timestamp = entry.data.i64[0];
            }
        }

        mCallbackOps->notify(mCallbackOps, &notifyMsg);
        mRequestStateVector.at(i).isShutterDone = true;
        LOG2("@%s, frame_number:%u, shutter timestamp:%lld", __func__,
             notifyMsg.message.shutter.frame_number, notifyMsg.message.shutter.timestamp);
        if (checkRequestDone(mRequestStateVector.at(i))) {
            returnRequestDone(notifyMsg.message.shutter.frame_number);
            releaseMetadataMemory(mRequestStateVector.at(i).metaResult);
            mRequestStateVector.erase(mRequestStateVector.begin() + i);
        }
        return icamera::OK;
    }

    LOGW("@%s frame_number:%u wasn't found!", __func__, event.frameNumber);
    return icamera::OK;
}

int ResultProcessor::metadataDone(const MetadataEvent& event) {
    MetadataMemory* metaMem = nullptr;
    {
        std::lock_guard<std::mutex> l(mLock);
        for (auto& reqStat : mRequestStateVector) {
            if (reqStat.frameNumber == event.frameNumber &&
                reqStat.partialResultReturned < reqStat.partialResultCount) {
                reqStat.partialResultReturned = 1;
                metaMem = reqStat.metaResult;
            }
        }
    }

    if (metaMem) {
        camera3_capture_result_t result;
        CLEAR(result);
        result.frame_number = event.frameNumber;
        result.output_buffers = nullptr;
        result.num_output_buffers = 0;

        if (mInputCam3Bufs.find(result.frame_number) == mInputCam3Bufs.end()) {
            android::CameraMetadata* metaResult = metaMem->getMetadata();
            MetadataConvert::HALMetadataToRequestMetadata(*(event.parameter), metaResult,
                                                          mCameraId);
            updateMetadata(*(event.parameter), metaResult);
            mCamera3AMetadata->process3Astate(*(event.parameter), metaResult);
        }

        result.result = metaMem->getMemory();
        result.partial_result = 1;
        mCallbackOps->process_capture_result(mCallbackOps, &result);

        LOG2("@%s frame_number:%u, metadataDone", __func__, event.frameNumber);
    }

    bool found = false;
    std::lock_guard<std::mutex> l(mLock);
    for (uint32_t i = 0; i < mRequestStateVector.size(); i++) {
        if (mRequestStateVector.at(i).frameNumber == event.frameNumber) {
            if (checkRequestDone(mRequestStateVector.at(i))) {
                returnInputBuffer(event.frameNumber);
                returnRequestDone(event.frameNumber);
                releaseMetadataMemory(mRequestStateVector.at(i).metaResult);
                mRequestStateVector.erase(mRequestStateVector.begin() + i);
            }
            found = true;
        }
    }
    if (!found) {
        LOGW("%s, event.frameNumber %u wasn't found!", __func__, event.frameNumber);
    } else {
        LOG2("%s, event.frameNumber %u was returned", __func__, event.frameNumber);
    }

    return icamera::OK;
}

int ResultProcessor::bufferDone(const BufferEvent& event) {
    camera3_capture_result_t result;
    CLEAR(result);

    result.frame_number = event.frameNumber;
    result.output_buffers = event.outputBuffer;
    result.num_output_buffers = 1;
    result.result = nullptr;
    result.partial_result = 0;

    mCallbackOps->process_capture_result(mCallbackOps, &result);

    bool found = false;
    {
        std::lock_guard<std::mutex> l(mLock);
        for (uint32_t i = 0; i < mRequestStateVector.size(); i++) {
            if (mRequestStateVector.at(i).frameNumber == event.frameNumber) {
                mRequestStateVector.at(i).buffersReturned++;
                if (checkRequestDone(mRequestStateVector.at(i))) {
                    returnInputBuffer(event.frameNumber);
                    returnRequestDone(event.frameNumber);
                    releaseMetadataMemory(mRequestStateVector.at(i).metaResult);
                    mRequestStateVector.erase(mRequestStateVector.begin() + i);
                }
                found = true;
            }
        }
    }
    if (!found) {
        LOGW("%s, event.frameNumber %u wasn't found!", __func__, event.frameNumber);
    } else {
        LOG2("%s, event.frameNumber %u was returned", __func__, event.frameNumber);
    }

    if (event.timestamp != 0 && event.sequence != -1) {
        std::lock_guard<std::mutex> lock(mmOpaqueRawInfoMapLock);
        // Raw buffer cached in HAL
        int savedRawBufNum = icamera::PlatformData::getMaxRawDataNum(mCameraId) -
                             icamera::PlatformData::getMaxRequestsInflight(mCameraId);
        // There are buffers processed in PSYS which may return to sensor, so the
        // last max in fight buffers are not safe now.
        int securityRawBufNum =
            savedRawBufNum - icamera::PlatformData::getMaxRequestsInflight(mCameraId);
        if (mOpaqueRawInfoMap.size() >= securityRawBufNum)
            mOpaqueRawInfoMap.erase(mOpaqueRawInfoMap.begin());
        // Only save Raw buffer info matching with saved Raw bufer Queue in PSYS
        mOpaqueRawInfoMap[event.sequence] = event.timestamp;
    }
    return icamera::OK;
}

void ResultProcessor::clearRawBufferInfoMap() {
    std::lock_guard<std::mutex> lock(mmOpaqueRawInfoMapLock);
    mOpaqueRawInfoMap.clear();
}

void ResultProcessor::checkAndChangeRawbufferInfo(long* sequence, uint64_t* timestamp) {
    CheckError(!sequence || !timestamp, VOID_VALUE, "invilid input parameter!");

    std::lock_guard<std::mutex> lock(mmOpaqueRawInfoMapLock);
    if (mOpaqueRawInfoMap.empty()) return;
    if (mOpaqueRawInfoMap.find(*sequence) != mOpaqueRawInfoMap.end()) return;

    // Raw buffer is too old and can't be handled, just use oldest buffer
    auto it = mOpaqueRawInfoMap.cbegin();
    *sequence = (*it).first;
    *timestamp = (*it).second;
    LOG2("%s, update raw info sequence %ld, timestamp %ld", __func__, *sequence, *timestamp);
}

void ResultProcessor::updateMetadata(const icamera::Parameters& parameter,
                                     android::CameraMetadata* settings) {
    /*
     *  if we support face ae and the face detection mode is not off,
     *  set face detect to request metadata, then the face area will be drawn.
     */
    if (icamera::PlatformData::isFaceAeEnabled(mCameraId)) {
        uint8_t faceDetectMode;
        int ret = parameter.getFaceDetectMode(faceDetectMode);
        if (ret == icamera::OK && faceDetectMode != icamera::FD_MODE_OFF) {
            icamera::CVFaceDetectionAbstractResult faceResult;
            ret = icamera::FaceDetection::getResult(mCameraId, &faceResult);
            if (ret == icamera::OK) {
                MetadataConvert::convertFaceDetectionMetadata(faceResult, settings);
                LOG2("@%s, set face detection metadata, face number:%d", __func__,
                     faceResult.faceNum);
            }
        }
    }

    int64_t exposure = 0;
    int32_t sensorIso = 0;
    parameter.getExposureTime(exposure);
    parameter.getSensitivityIso(sensorIso);

    // If the state of black level lock is ON in the first request, the value must
    // be set ON. Other request sets the black level lock value according sensor
    // exposure time and iso.
    uint8_t lockMode = ANDROID_BLACK_LEVEL_LOCK_OFF;
    camera_metadata_entry entry = settings->find(ANDROID_BLACK_LEVEL_LOCK);
    if (entry.count == 1 && (entry.data.u8[0] == ANDROID_BLACK_LEVEL_LOCK_ON)) {
        lockMode =
            ((exposure == mLastParams.sensorExposure && sensorIso == mLastParams.sensorIso) ||
             (-1 == mLastParams.sensorExposure && -1 == mLastParams.sensorIso))
                ? ANDROID_BLACK_LEVEL_LOCK_ON
                : ANDROID_BLACK_LEVEL_LOCK_OFF;
    }
    LOG2("@%s, the black level lock metadata: %d", __func__, lockMode);
    settings->update(ANDROID_BLACK_LEVEL_LOCK, &lockMode, 1);

    mLastParams.sensorExposure = exposure;
    mLastParams.sensorIso = sensorIso;
}

// the input buffer must be returned as the last one buffer
void ResultProcessor::returnInputBuffer(uint32_t frameNumber) {
    if (mInputCam3Bufs.find(frameNumber) == mInputCam3Bufs.end()) {
        return;
    }

    std::shared_ptr<Camera3Buffer> inBuf = mInputCam3Bufs[frameNumber];
    if (!inBuf) {
        return;
    }

    camera3_stream_t* s = inBuf->getStream();
    if (s) {
        LOG2("@%s, frame_number:%u, w:%d, h:%d, f:%d", __func__, frameNumber, s->width, s->height,
             s->format);
    }

    camera3_stream_buffer_t buf = {};
    buf.stream = s;
    buf.buffer = inBuf->getBufferHandle();
    buf.status = inBuf->status();

    inBuf->getFence(&buf);
    inBuf->unlock();
    inBuf->deinit();

    camera3_capture_result_t result = {};
    result.frame_number = frameNumber;
    result.result = nullptr;
    result.input_buffer = &buf;

    LOG1("@%s, frame_number:%u, return the input buffer", __func__, frameNumber);
    mCallbackOps->process_capture_result(mCallbackOps, &result);

    mInputCam3Bufs.erase(frameNumber);
}

bool ResultProcessor::checkRequestDone(const RequestState& requestState) {
    LOG1("@%s", __func__);

    return (requestState.isShutterDone &&
            requestState.partialResultCount == requestState.partialResultReturned &&
            requestState.buffersToReturn == requestState.buffersReturned);
}

void ResultProcessor::returnRequestDone(uint32_t frameNumber) {
    LOG2("@%s frame_number:%d", __func__, frameNumber);
    TRACE_LOG_POINT("ResultProcessor", __func__, MAKE_COLOR(frameNumber), frameNumber);

    if ((frameNumber % FPS_FRAME_COUNT == 0) &&
        icamera::Log::isDebugLevelEnable(icamera::CAMERA_DEBUG_LOG_FPS)) {
        struct timeval curTime;
        gettimeofday(&curTime, nullptr);
        int duration = static_cast<int>(curTime.tv_usec - mRequestTime.tv_usec +
                                        ((curTime.tv_sec - mRequestTime.tv_sec) * 1000000));
        if (frameNumber == 0) {
            LOGFPS("%s, time of launch to preview: %dms", __func__, (duration / 1000));
        } else {
            float curFps =
                static_cast<float>(1000000) / static_cast<float>(duration / FPS_FRAME_COUNT);
            LOGFPS("@%s, fps: %02f", __func__, curFps);
        }
        gettimeofday(&mRequestTime, nullptr);
    }

    mRequestManagerCallback->returnRequestDone(frameNumber);
}

MetadataMemory* ResultProcessor::acquireMetadataMemory() {
    MetadataMemory* metaMem = nullptr;
    if (mMetadataVector.size() > 0) {
        metaMem = mMetadataVector.back();
        mMetadataVector.pop_back();
    } else {
        metaMem = new MetadataMemory();
        LOG1("%s: allocate new one: %p", __func__, metaMem);
    }

    return metaMem;
}

void ResultProcessor::releaseMetadataMemory(MetadataMemory* metaMem) {
    CheckError(metaMem == nullptr, VOID_VALUE, "%s: null metaMem!", __func__);
    mMetadataVector.push_back(metaMem);
}

ResultProcessor::ResultThread::ResultThread(int cameraId, ResultProcessor* resultProcessor)
        : mCameraId(cameraId),
          mResultProcessor(resultProcessor) {
    LOG1("@%s", __func__);

    run("ResultThread");
}

ResultProcessor::ResultThread::~ResultThread() {
    LOG1("@%s", __func__);

    requestExit();
    std::lock_guard<std::mutex> l(mEventQueueLock);
    mEventCondition.notify_one();
}

void ResultProcessor::ResultThread::sendEvent(const icamera::camera_msg_data_t& data) {
    LOG2("@%s", __func__);
    std::lock_guard<std::mutex> l(mEventQueueLock);
    mEventQueue.push(data);
    mEventCondition.notify_one();
}

bool ResultProcessor::ResultThread::threadLoop() {
    LOG2("@%s", __func__);

    icamera::camera_msg_data_t data;
    // mutex only protects mEventQueue
    {
        std::unique_lock<std::mutex> l(mEventQueueLock);
        // check if there is event queued
        if (mEventQueue.empty()) {
            std::cv_status ret = mEventCondition.wait_for(
                l, std::chrono::nanoseconds(kMaxDuration * SLOWLY_MULTIPLIER));

            if (ret == std::cv_status::timeout) {
                LOGW("%s, wait event timeout", __func__);
            }

            return true;
        }

        // parse event
        data = std::move(mEventQueue.front());
        mEventQueue.pop();
    }

    // handle message
    switch (data.type) {
        // Regards isp buffer ready as shutter event
        case icamera::CAMERA_ISP_BUF_READY: {
            ShutterEvent event = {data.data.buffer_ready.frameNumber,
                                  data.data.buffer_ready.timestamp};
            LOG2("@%s, frameNumber %d, timestamp %ld, mResultProcessor:%p", __func__,
                 event.frameNumber, event.timestamp, mResultProcessor);
            mResultProcessor->shutterDone(event);

            icamera::Parameters parameter;
            icamera::camera_get_parameters(mCameraId, parameter, data.data.buffer_ready.sequence);
            MetadataEvent metadataEvent = {data.data.buffer_ready.frameNumber, &parameter};
            mResultProcessor->metadataDone(metadataEvent);
            break;
        }
        case icamera::CAMERA_IPC_ERROR: {
            mResultProcessor->notifyError();
            break;
        }
        default: {
            LOGW("unknown message type %d", data.type);
            break;
        }
    }

    return true;
}

}  // namespace camera3
