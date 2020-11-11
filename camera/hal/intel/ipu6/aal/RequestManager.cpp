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

#define LOG_TAG "RequestManager"

#include "RequestManager.h"

#include <hardware/gralloc.h>
#include <linux/videodev2.h>
#include <math.h>

#include <algorithm>
#include <cstdlib>
#include <list>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Errors.h"
#include "HALv3Utils.h"
#include "ICamera.h"
#include "MetadataConvert.h"
#include "Parameters.h"
#include "PlatformData.h"
#include "Utils.h"

namespace camera3 {
RequestManager::RequestManager(int cameraId)
        : mCameraId(cameraId),
          mCallbackOps(nullptr),
          mCameraDeviceStarted(false),
          mResultProcessor(nullptr),
          mInputStreamConfigured(false),
          mRequestInProgress(0) {
    LOG1("@%s", __func__);

    CLEAR(mCameraBufferInfo);
}

RequestManager::~RequestManager() {
    LOG1("@%s", __func__);

    deleteStreams(false);

    delete mResultProcessor;
}

int RequestManager::init(const camera3_callback_ops_t* callback_ops) {
    LOG1("@%s", __func__);

    // Update the default settings from camera HAL
    icamera::Parameters parameter;
    int ret = icamera::camera_get_parameters(mCameraId, parameter);
    CheckError(ret != icamera::OK, ret, "failed to get parameters, ret %d", ret);
    StaticCapability::getInstance(mCameraId);

    android::CameraMetadata defaultRequestSettings;
    // Get static metadata
    MetadataConvert::HALCapabilityToStaticMetadata(parameter, &defaultRequestSettings);

    // Get defalut settings
    MetadataConvert::constructDefaultMetadata(mCameraId, &defaultRequestSettings);
    MetadataConvert::HALMetadataToRequestMetadata(parameter, &defaultRequestSettings, mCameraId);

    mDefaultRequestSettings[CAMERA3_TEMPLATE_PREVIEW] = defaultRequestSettings;
    MetadataConvert::updateDefaultRequestSettings(
        mCameraId, CAMERA3_TEMPLATE_PREVIEW, &mDefaultRequestSettings[CAMERA3_TEMPLATE_PREVIEW]);

    mResultProcessor = new ResultProcessor(mCameraId, callback_ops, this);
    mCallbackOps = callback_ops;

    // Register callback to icamera HAL
    icamera::camera_callback_ops_t::notify = RequestManager::callbackNotify;
    icamera::camera_callback_register(mCameraId,
                                      static_cast<icamera::camera_callback_ops_t*>(this));

    return icamera::OK;
}

int RequestManager::deinit() {
    LOG1("@%s", __func__);

    // Unregister callback to icamera HAL
    icamera::camera_callback_register(mCameraId, nullptr);

    if (mCameraDeviceStarted) {
        int ret = icamera::camera_device_stop(mCameraId);
        CheckError(ret != icamera::OK, ret, "failed to stop camera device, ret %d", ret);
        mCameraDeviceStarted = false;
    }

    mRequestInProgress = 0;
    StaticCapability::releaseInstance(mCameraId);
    return icamera::OK;
}

void RequestManager::callbackNotify(const icamera::camera_callback_ops* cb,
                                    const icamera::camera_msg_data_t& data) {
    LOG2("@%s, type %d", __func__, data.type);
    RequestManager* callback = const_cast<RequestManager*>(static_cast<const RequestManager*>(cb));

    callback->mResultProcessor->callbackNotify(data);
    callback->handleCallbackEvent(data);
}

void RequestManager::handleCallbackEvent(const icamera::camera_msg_data_t& data) {
    LOG2("@%s, cameraId: %d", __func__, mCameraId);

    if (!icamera::PlatformData::swProcessingAlignWithIsp(mCameraId)) return;

    for (auto& stream : mCamera3StreamVector) {
        if (stream->getPostProcessType() != icamera::POST_PROCESS_NONE) {
            stream->sendEvent(data);
        }
    }
}

int RequestManager::configureStreams(camera3_stream_configuration_t* stream_list) {
    LOG1("@%s", __func__);

    int ret = checkStreamRotation(stream_list);

    CheckError(ret != icamera::OK, icamera::BAD_VALUE, "Unsupport rotation degree!");

    if (mCameraDeviceStarted) {
        ret = icamera::camera_device_stop(mCameraId);
        CheckError(ret != icamera::OK, ret, "failed to stop camera device, ret %d", ret);
        mCameraDeviceStarted = false;
    }

    icamera::stream_t requestStreams[kMaxStreamNum];  // not include CAMERA3_STREAM_INPUT stream
    uint32_t streamsNum = stream_list->num_streams;
    uint32_t operationMode = stream_list->operation_mode;
    LOG1("@%s, streamsNum:%d, operationMode:%d", __func__, streamsNum, operationMode);
    CheckError((operationMode != CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE &&
                operationMode != CAMERA3_STREAM_CONFIGURATION_CONSTRAINED_HIGH_SPEED_MODE),
               icamera::BAD_VALUE, "Unknown operation mode %d!", operationMode);

    int inputStreamNum = 0;
    int outStreamNum = 0;
    camera3_stream_t* stream = nullptr;
    for (uint32_t i = 0; i < streamsNum; i++) {
        stream = stream_list->streams[i];
        LOG1("@%s, Config stream (%s):%dx%d, f:%d, u:%d, buf num:%d, priv:%p", __func__,
             HalV3Utils::getCamera3StreamType(stream->stream_type), stream->width, stream->height,
             stream->format, stream->usage, stream->max_buffers, stream->priv);
        if (stream->stream_type == CAMERA3_STREAM_OUTPUT) {
            outStreamNum++;
        } else if (stream->stream_type == CAMERA3_STREAM_INPUT) {
            inputStreamNum++;
            mInputStreamConfigured = true;
        } else if (stream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL) {
            inputStreamNum++;
            outStreamNum++;
            mInputStreamConfigured = true;
        } else {
            LOGE("@%s, Unknown stream type %d!", __func__, stream->stream_type);
            return icamera::BAD_VALUE;
        }
        // In ZSL case, RAW input and YUV input will be configured together.
        CheckError(inputStreamNum > 2, icamera::BAD_VALUE, "Too many input streams : %d !",
                   inputStreamNum);
    }
    CheckError(outStreamNum == 0, icamera::BAD_VALUE, "No output streams!");

    /*
     * Configure stream
     */
    mResultProcessor->clearRawBufferInfoMap();
    int requestStreamNum = 0;
    camera3_stream_t* inputStream = nullptr;
    // Enable video pipe if yuv stream exists (for 3A stats data)
    bool needAssignPreviewStream = true;
    icamera::stream_t* yuvStream = nullptr;
    for (uint32_t i = 0; i < streamsNum; i++) {
        /*
         * 1, for CAMERA3_STREAM_INPUT stream, format YCbCr_420_888 is for YUV
         * reprocessing, other formats (like IMPLEMENTATION_DEFINED, RAW_OPAQUE) are
         * for RAW reprocessing.
         * 2, for CAMERA3_STREAM_BIDIRECTIONAL stream, it is for RAW reprocessing.
         * 3, for CAMERA3_STREAM_OUTPUT stream, if format is IMPLEMENTATION_DEFINED
         * and usage doesn't include COMPOSE or TEXTURE, it is for RAW reprocessing.
         * if format is RAW_OPAQUE, it is for RAW reprocessing.
         */
        if (stream_list->streams[i]->stream_type == CAMERA3_STREAM_INPUT) {
            if (stream_list->streams[i]->format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
                inputStream = stream_list->streams[i];
                LOG1("@%s, input stream: w:%d, h:%d, f:%d", __func__, inputStream->width,
                     inputStream->height, inputStream->format);
            } else {
                stream_list->streams[i]->usage |= GRALLOC_USAGE_HW_CAMERA_ZSL;
            }
            stream_list->streams[i]->max_buffers = 2;
            continue;
        } else if (stream_list->streams[i]->stream_type == CAMERA3_STREAM_BIDIRECTIONAL) {
            stream_list->streams[i]->usage |= GRALLOC_USAGE_HW_CAMERA_ZSL;
        } else {
            if (stream_list->streams[i]->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
                inputStreamNum > 0) {
                if (!(stream_list->streams[i]->usage &
                      (GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_TEXTURE))) {
                    stream_list->streams[i]->usage |= GRALLOC_USAGE_HW_CAMERA_ZSL;
                }
            } else if (stream_list->streams[i]->format == HAL_PIXEL_FORMAT_RAW_OPAQUE) {
                stream_list->streams[i]->usage |= GRALLOC_USAGE_HW_CAMERA_ZSL;
            }
        }

        ret = HalV3Utils::fillHALStreams(mCameraId, *stream_list->streams[i],
                                         &requestStreams[requestStreamNum]);
        CheckError(ret != icamera::OK, ret, "failed to fill requestStreams[%d], ret:%d", ret,
                   requestStreamNum);

        if (!yuvStream && stream_list->streams[i]->format != HAL_PIXEL_FORMAT_BLOB &&
            !IS_ZSL_USAGE(stream_list->streams[i]->usage)) {
            yuvStream = &requestStreams[requestStreamNum];
        }
        if (requestStreams[requestStreamNum].usage == icamera::CAMERA_STREAM_PREVIEW ||
            requestStreams[requestStreamNum].usage == icamera::CAMERA_STREAM_VIDEO_CAPTURE)
            needAssignPreviewStream = false;

        requestStreamNum++;
    }
    if (needAssignPreviewStream && yuvStream) {
        yuvStream->usage = icamera::CAMERA_STREAM_PREVIEW;
    }

    CLEAR(mHALStream);

    int halStreamFlag[kMaxStreamNum];
    int halStreamNum = chooseHALStreams(requestStreamNum, halStreamFlag, requestStreams);
    // halStreamNum should not exceed videoNum + 2 (1 opaque raw and 1 still)
    int maxSupportStreamNum = icamera::PlatformData::getVideoStreamNum(mCameraId) + 2;
    CheckError(halStreamNum > maxSupportStreamNum || halStreamNum <= 0, icamera::BAD_VALUE,
               "failed to find HAL stream");

    // index of stream in mHALStream
    int halStreamIndex = 0;
    // first:stream index in requestStreams[], second:HAL stream index in mHALStream[]
    std::map<int, int> streamToHALIndex;
    for (int i = 0; i < requestStreamNum; i++) {
        // fill HAL stream with right requestStreams object
        if (halStreamFlag[i] == i) {
            mHALStream[halStreamIndex] = requestStreams[i];
            streamToHALIndex[i] = halStreamIndex;
            halStreamIndex++;
        }
    }

    icamera::stream_config_t streamCfg = {
        halStreamNum, mHALStream,
        icamera::camera_stream_configuration_mode_t::CAMERA_STREAM_CONFIGURATION_MODE_AUTO};

    for (int i = 0; i < requestStreamNum; i++) {
        const icamera::stream_t& s = requestStreams[i];
        LOG1("@%s, requestStreams[%d]: w:%d, h:%d, f:%d, u:%d", __func__, i, s.width, s.height,
             s.format, s.usage);
    }

    for (int i = 0; i < halStreamNum; i++) {
        const icamera::stream_t& s = mHALStream[i];
        LOG1("@%s, configured mHALStream[%d]: w:%d, h:%d, f:%d, u:%d", __func__, i, s.width,
             s.height, s.format, s.usage);
    }

    // Mark all streams as NOT active
    for (auto& stream : mCamera3StreamVector) {
        stream->setActive(false);
    }

    int enableFDStreamNum = -1;
    Camera3Stream* faceDetectionOwner = nullptr;
    // Mark one camera3Stream run face detection
    if (icamera::PlatformData::isFaceAeEnabled(mCameraId)) {
        LOG1("Face detection is enable");
        chooseStreamForFaceDetection(streamsNum, stream_list->streams, &enableFDStreamNum);
    }

    ret = icamera::camera_device_config_streams(mCameraId, &streamCfg);
    CheckError(ret != icamera::OK, ret, "failed to configure stream, ret %d", ret);

    // Create Stream for new streams
    requestStreamNum = 0;
    for (uint32_t i = 0; i < streamsNum; i++) {
        camera3_stream_t* stream = stream_list->streams[i];
        if (stream->stream_type == CAMERA3_STREAM_INPUT) {
            continue;
        }

        /* use halStreamFlag[] to find it's HAL stream index in requestStreams
        ** streamToHALIndex to find it's HAL Stream index in mHALStream[]*/
        int halStreamIndex = streamToHALIndex[halStreamFlag[requestStreamNum]];
        bool isHALStream = halStreamFlag[requestStreamNum] == requestStreamNum;
        CheckError(halStreamIndex < 0 || halStreamIndex >= halStreamNum, icamera::BAD_VALUE,
                   "failed to find hal stream %d", halStreamIndex);
        Camera3Stream* s =
            new Camera3Stream(mCameraId, mResultProcessor, mHALStream[halStreamIndex].max_buffers,
                              mHALStream[halStreamIndex], *stream, inputStream, isHALStream);
        s->setActive(true);
        stream->priv = s;
        stream->max_buffers = mHALStream[halStreamIndex].max_buffers;
        stream->usage |= GRALLOC_USAGE_HW_CAMERA_WRITE | GRALLOC_USAGE_SW_READ_OFTEN |
                         GRALLOC_USAGE_SW_WRITE_NEVER;
        mCamera3StreamVector.push_back(s);

        requestStreamNum++;
        LOGI("OUTPUT max buffer %d, usage %x, format %x", stream->max_buffers, stream->usage,
             stream->format);

        if (enableFDStreamNum == i) {
            faceDetectionOwner = static_cast<Camera3Stream*>(stream->priv);
        }
    }

    // Remove useless Camera3Stream
    deleteStreams(true);

    // bind streams to HAL streams
    for (int i = 0; i < mCamera3StreamVector.size(); i++) {
        if (halStreamFlag[i] != i)
            mCamera3StreamVector[halStreamFlag[i]]->addListener(mCamera3StreamVector[i]);
    }

    if (faceDetectionOwner != nullptr) {
        unsigned int maxFacesNum = icamera::PlatformData::getMaxFaceDetectionNumber(mCameraId);
        faceDetectionOwner->activateFaceDetection(maxFacesNum);
    }

    return icamera::OK;
}

void RequestManager::chooseStreamForFaceDetection(uint32_t streamsNum, camera3_stream_t** streams,
                                                  int* enableFDStreamNum) {
    LOG1("@%s", __func__);
    camera3_stream_t* preStream = nullptr;
    camera3_stream_t* yuvStream = nullptr;
    int maxWidth = MAX_FACE_FRAME_WIDTH;
    int maxHeight = MAX_FACE_FRAME_HEIGHT;
    int preStreamNum = -1;
    int yuvStreamNum = -1;

    for (uint32_t i = 0; i < streamsNum; i++) {
        camera3_stream_t* s = streams[i];
        if (!s || s->stream_type != CAMERA3_STREAM_OUTPUT || s->width > maxWidth ||
            s->height > maxHeight) {
            continue;
        }

        LOG1("stream information:format=%d, width=%d, height=%d", s->format, s->width, s->height);
        // We assume HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED stream is the
        // preview stream and it's requested in every capture request.
        // If there are two HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED streams,
        // We pick the smaller stream due to performance concern.
        if (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED && !IS_ZSL_USAGE(s->usage)) {
            if (preStream && preStream->width * preStream->height <= s->width * s->height) {
                continue;
            }
            preStream = s;
            preStreamNum = i;
        }

        if (s->format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
            if (yuvStream && yuvStream->width * yuvStream->height <= s->width * s->height) {
                continue;
            }
            yuvStream = s;
            yuvStreamNum = i;
        }
    }

    *enableFDStreamNum = -1;
    if (preStreamNum >= 0) {
        *enableFDStreamNum = preStreamNum;
    } else if (yuvStreamNum >= 0) {
        *enableFDStreamNum = yuvStreamNum;
    }
    LOG1("enableFDStreamNum %d", *enableFDStreamNum);
}

int RequestManager::constructDefaultRequestSettings(int type, const camera_metadata_t** meta) {
    LOG1("@%s, type %d", __func__, type);

    if (mDefaultRequestSettings.count(type) == 0) {
        mDefaultRequestSettings[type] = mDefaultRequestSettings[CAMERA3_TEMPLATE_PREVIEW];
        MetadataConvert::updateDefaultRequestSettings(mCameraId, type,
                                                      &mDefaultRequestSettings[type]);
    }
    const camera_metadata_t* setting = mDefaultRequestSettings[type].getAndLock();
    *meta = setting;
    mDefaultRequestSettings[type].unlock(setting);

    return icamera::OK;
}

int RequestManager::processCaptureRequest(camera3_capture_request_t* request) {
    CheckError(!request, icamera::UNKNOWN_ERROR, "@%s, request is nullptr", __func__);
    LOG1("@%s, frame_number:%d, input_buffer:%d, num_output_buffers:%d", __func__,
         request->frame_number, request->input_buffer ? 1 : 0, request->num_output_buffers);

    TRACE_LOG_POINT("RequestManager", __func__, MAKE_COLOR(request->frame_number),
                    request->frame_number);

    // Valid buffer and request
    CheckError(request->num_output_buffers > kMaxStreamNum, icamera::BAD_VALUE,
               "@%s, num_output_buffers:%d", __func__, request->num_output_buffers);

    int ret = icamera::OK;

    waitProcessRequest();

    int index = -1;
    for (int i = 0; i < kMaxProcessRequestNum; i++) {
        if (!mCameraBufferInfo[i].frameInProcessing) {
            index = i;
            break;
        }
    }
    CheckError(index < 0, icamera::UNKNOWN_ERROR, "no empty CameraBufferInfo!");
    CLEAR(mCameraBufferInfo[index]);

    if (request->settings) {
        MetadataConvert::dumpMetadata(request->settings);
        mLastSettings = request->settings;
    } else if (mLastSettings.isEmpty()) {
        LOGE("nullptr settings for the first reqeust!");
        return icamera::BAD_VALUE;
    }

    std::shared_ptr<Camera3Buffer> inputCam3Buf = nullptr;
    icamera::sensor_raw_info_t opaqueRawInfo = {-1, 0};
    if (request->input_buffer) {
        inputCam3Buf = std::make_shared<Camera3Buffer>();
        icamera::status_t status = inputCam3Buf->init(request->input_buffer, mCameraId);
        CheckError(status != icamera::OK, icamera::BAD_VALUE, "Failed to init CameraBuffer");
        status = inputCam3Buf->waitOnAcquireFence();
        CheckError(status != icamera::OK, icamera::BAD_VALUE, "Failed to sync CameraBuffer");
        status = inputCam3Buf->lock();
        CheckError(status != icamera::OK, icamera::BAD_VALUE, "Failed to lock buffer");

        camera_metadata_entry entry = mLastSettings.find(ANDROID_SENSOR_TIMESTAMP);
        if (entry.count == 1) {
            inputCam3Buf->setTimeStamp(entry.data.i64[0]);
        }

        if (IS_ZSL_USAGE(request->input_buffer->stream->usage)) {
            MEMCPY_S(&opaqueRawInfo, sizeof(opaqueRawInfo), inputCam3Buf->data(),
                     inputCam3Buf->size());
            mResultProcessor->checkAndChangeRawbufferInfo(&opaqueRawInfo.sequence,
                                                          &opaqueRawInfo.timestamp);
            LOG2("%s, sequence id %ld, timestamp %ld", __func__, opaqueRawInfo.sequence,
                 opaqueRawInfo.timestamp);
        }
    }

    icamera::Parameters param;
    param.setMakernoteMode(icamera::MAKERNOTE_MODE_OFF);
    param.setUserRequestId(static_cast<int32_t>(request->frame_number));

    for (uint32_t i = 0; i < request->num_output_buffers; i++) {
        camera3_stream_t* aStream = request->output_buffers[i].stream;        // app stream
        Camera3Stream* lStream = static_cast<Camera3Stream*>(aStream->priv);  // local stream
        if (mInputStreamConfigured || aStream->format == HAL_PIXEL_FORMAT_BLOB) {
            param.setMakernoteMode(icamera::MAKERNOTE_MODE_JPEG);
        }

        ret = lStream->processRequest(opaqueRawInfo.sequence >= 0 ? nullptr : inputCam3Buf,
                                      request->output_buffers[i], request->frame_number);
        CheckError(ret != icamera::OK, ret, "Failed to process request, ret:%d", ret);
    }

    // Convert metadata to Parameters
    bool forceConvert = inputCam3Buf ? true : false;
    MetadataConvert::requestMetadataToHALMetadata(mLastSettings, &param, forceConvert);

    mResultProcessor->registerRequest(request, inputCam3Buf);

    if (!inputCam3Buf || opaqueRawInfo.sequence >= 0) {
        icamera::camera_buffer_t* buffer[kMaxStreamNum] = {nullptr};
        int numBuffers = 0;
        for (auto& stream : mCamera3StreamVector) {
            if (stream->fetchRequestBuffers(&mCameraBufferInfo[index].halBuffer[numBuffers],
                                            request->frame_number)) {
                mCameraBufferInfo[index].halBuffer[numBuffers].sequence = opaqueRawInfo.sequence;
                mCameraBufferInfo[index].halBuffer[numBuffers].timestamp = opaqueRawInfo.timestamp;
                buffer[numBuffers] = &mCameraBufferInfo[index].halBuffer[numBuffers];
                numBuffers++;
            }
        }
        ret = icamera::camera_stream_qbuf(mCameraId, buffer, numBuffers, &param);
        CheckError(ret != icamera::OK, ret, "@%s, camera_stream_qbuf fails,ret:%d", __func__, ret);
    }

    increaseRequestCount();

    if (!mCameraDeviceStarted) {
        ret = icamera::camera_device_start(mCameraId);
        CheckError(ret != icamera::OK, ret, "failed to start device, ret %d", ret);

        mCameraDeviceStarted = true;
    }

    mCameraBufferInfo[index].frameInProcessing = true;
    mCameraBufferInfo[index].frameNumber = request->frame_number;

    for (uint32_t i = 0; i < request->num_output_buffers; i++) {
        Camera3Stream* s = static_cast<Camera3Stream*>(request->output_buffers[i].stream->priv);
        s->queueBufferDone(request->frame_number,
                           opaqueRawInfo.sequence >= 0 ? nullptr : inputCam3Buf,
                           request->output_buffers[i], param);
    }

    for (auto& stream : mCamera3StreamVector) {
        /* incase the HAL stream is not requested by user request, scan all the HAL
        ** streams check if any one is triggered by listener
        */
        stream->checkListenerRequest(request->frame_number);
    }

    return ret;
}

void RequestManager::dump(int fd) {
    LOG1("@%s", __func__);
}

int RequestManager::flush() {
    LOG1("@%s", __func__);

    icamera::nsecs_t startTime = icamera::CameraUtils::systemTime();
    icamera::nsecs_t interval = 0;
    const icamera::nsecs_t ONE_SECOND = 1000000000;

    // wait 1000ms at most while there are requests in the HAL
    while (mRequestInProgress > 0 && interval <= ONE_SECOND) {
        usleep(10000);  // wait 10ms
        interval = icamera::CameraUtils::systemTime() - startTime;
    }

    LOG2("@%s, line:%d, mRequestInProgress:%d, time spend:%ld us", __func__, __LINE__,
         mRequestInProgress, interval / 1000);

    // based on API, -ENODEV (NO_INIT) error should be returned.
    CheckError(interval > ONE_SECOND, icamera::NO_INIT, "flush() > 1s, timeout:%ld us",
               interval / 1000);

    return icamera::OK;
}

void RequestManager::deleteStreams(bool inactiveOnly) {
    LOG1("@%s", __func__);

    unsigned int i = 0;
    while (i < mCamera3StreamVector.size()) {
        Camera3Stream* s = mCamera3StreamVector.at(i);

        if (!inactiveOnly || !s->isActive()) {
            mCamera3StreamVector.erase(mCamera3StreamVector.begin() + i);
            delete s;
        } else {
            ++i;
        }
    }
}

int RequestManager::waitProcessRequest() {
    LOG1("@%s", __func__);
    std::unique_lock<std::mutex> lock(mRequestLock);
    // check if it is ready to process next request
    while (mRequestInProgress >= mHALStream[0].max_buffers) {
        std::cv_status ret = mRequestCondition.wait_for(
            lock, std::chrono::nanoseconds(kMaxDuration * SLOWLY_MULTIPLIER));
        if (ret == std::cv_status::timeout) {
            LOGW("%s, wait to process request time out", __func__);
        }
    }

    return icamera::OK;
}

void RequestManager::increaseRequestCount() {
    LOG1("@%s", __func__);

    std::lock_guard<std::mutex> l(mRequestLock);
    ++mRequestInProgress;
}

void RequestManager::returnRequestDone(uint32_t frameNumber) {
    LOG1("@%s  frame %d", __func__, frameNumber);

    std::lock_guard<std::mutex> l(mRequestLock);

    // Update mCameraBufferInfo based on frameNumber
    for (int i = 0; i < kMaxProcessRequestNum; i++) {
        if (mCameraBufferInfo[i].frameNumber == frameNumber) {
            CLEAR(mCameraBufferInfo[i]);
            break;
        }
    }
    mRequestInProgress--;
    mRequestCondition.notify_one();

    for (auto& stream : mCamera3StreamVector) {
        stream->requestStreamDone(frameNumber);
    }
}

int RequestManager::checkStreamRotation(camera3_stream_configuration_t* stream_list) {
    int rotationDegree0 = -1, countOutputStream = 0;

    for (size_t i = 0; i < stream_list->num_streams; i++) {
        if (stream_list->streams[i]->stream_type != CAMERA3_STREAM_OUTPUT) {
            continue;
        }
        countOutputStream++;

        int rotationDegree = HalV3Utils::getRotationDegrees(*(stream_list->streams[i]));
        CheckError(rotationDegree < 0, icamera::BAD_VALUE, "Unsupport rotation degree!");

        if (countOutputStream == 1) {
            rotationDegree0 = rotationDegree;
        } else {
            CheckError(rotationDegree0 != rotationDegree, icamera::BAD_VALUE,
                       "rotationDegree0:%d, stream[%lu] rotationDegree:%d, not the same",
                       rotationDegree0, i, rotationDegree);
        }
    }

    return icamera::OK;
}

int RequestManager::chooseHALStreams(const uint32_t requestStreamNum, int* halStreamFlag,
                                     icamera::stream_t* halStreamList) {
    /* save streams with their configure index, the index in this deque is
     * their priority to be HWStream, from low to high
     */
    std::list<std::pair<icamera::stream_t*, int>> videoHALStreamOrder;
    std::list<std::pair<icamera::stream_t*, int>> stillHALStreamOrder;

    // save sorted hal streams with their configure index
    std::vector<std::pair<icamera::stream_t*, int>> requestStreams;

    int videoCount = 0, stillCount = 0, opaqueCount = 0;
    for (uint32_t i = 0; i < requestStreamNum; i++) {
        // set flags to it's index, every stream is a HAL stream by default
        halStreamFlag[i] = i;
        if (halStreamList[i].usage == icamera::CAMERA_STREAM_OPAQUE_RAW) {
            opaqueCount++;
        } else if (halStreamList[i].usage == icamera::CAMERA_STREAM_STILL_CAPTURE) {
            stillCount++;
        } else {
            videoCount++;
        }
    }

    int avaVideoSlot = icamera::PlatformData::getVideoStreamNum(mCameraId);
    // if HAL stream slots are enough, make all streams as HAL stream
    if (opaqueCount <= 1 && stillCount <= 1 && videoCount <= avaVideoSlot) {
        return requestStreamNum;
    }

    for (uint32_t i = 0; i < requestStreamNum; i++) {
        // clear the flags to invalid value
        halStreamFlag[i] = -1;
        requestStreams.push_back(std::make_pair(&halStreamList[i], i));
    }

    // sort stream by resolution, easy to find largest resolution stream
    std::sort(requestStreams.begin(), requestStreams.end(),
              [](std::pair<icamera::stream_t*, int>& s1, std::pair<icamera::stream_t*, int>& s2) {
                  if (s1.first->width * s1.first->height > s2.first->width * s2.first->height)
                      return true;
                  else
                      return false;
              });

    int activeHALNum = 0;
    bool perfStillIndex = false;
    int selectedVideoNum = 0, videoMaxResStreamIndex = -1, previewStreamIndex = -1;

    // Save the video stream with different resolution
    std::vector<icamera::stream_t*> videoHALStream;
    auto anchor = videoHALStreamOrder.end();

    for (uint32_t i = 0; i < requestStreamNum; i++) {
        icamera::stream_t* s = requestStreams[i].first;
        int index = requestStreams[i].second;

        if (s->usage == icamera::CAMERA_STREAM_OPAQUE_RAW) {
            // Choose hal stream for opaque stream, only 1 opaque stream
            halStreamFlag[index] = index;
            activeHALNum++;
        } else if (s->usage == icamera::CAMERA_STREAM_STILL_CAPTURE) {
            // Choose hal stream for still streams: same ratio is higher priority
            if (!perfStillIndex && HalV3Utils::isSameRatioWithSensor(s, mCameraId)) {
                perfStillIndex = true;
                stillHALStreamOrder.push_back(std::make_pair(s, index));
            } else {
                stillHALStreamOrder.push_front(std::make_pair(s, index));
            }
        } else {
            // Mark the user preview stream
            if (s->usage == icamera::CAMERA_STREAM_PREVIEW) {
                previewStreamIndex = i;
            }
            /*
             * Make the stream priority list: low to high
             * {[same resolution], [others(ascending)], [biggest resolution], [same ratio]}
             */
            // The max stream is the base node
            if (videoMaxResStreamIndex == -1) {
                anchor = videoHALStreamOrder.insert(anchor, std::make_pair(s, index));
                videoMaxResStreamIndex = index;
                videoHALStream.push_back(s);

                // Mark it as one same ratio stream
                if (HalV3Utils::isSameRatioWithSensor(s, mCameraId)) {
                    selectedVideoNum++;
                }
            } else {
                bool found = false;
                for (auto& vs : videoHALStream) {
                    if (vs && s->width == vs->width && s->height == vs->height) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    // Same resolution stream put in the front of list, has lowest priority
                    videoHALStreamOrder.push_front(std::make_pair(s, index));
                } else {
                    if (selectedVideoNum < avaVideoSlot &&
                        HalV3Utils::isSameRatioWithSensor(s, mCameraId)) {
                        // Same ratio is the highest priority, push_back of the list
                        videoHALStreamOrder.push_back(std::make_pair(s, index));
                    } else {
                        // Other small streams are second priority,
                        // insert them before the biggest stream by ascending
                        anchor = videoHALStreamOrder.insert(anchor, std::make_pair(s, index));
                    }
                    videoHALStream.push_back(s);
                }
            }
        }
    }

    /* Re-process the videoHALStreamOrder, then configure all items to HAL
     * 1. Remove the extra items
     */
    bool hasPreviewStream = false;
    for (auto it = videoHALStreamOrder.begin(); it != videoHALStreamOrder.end(); ) {
        if (videoHALStreamOrder.size() > avaVideoSlot) {
            it = videoHALStreamOrder.erase(it);
            continue;
        }

        if (it->first->usage == icamera::CAMERA_STREAM_PREVIEW) {
            hasPreviewStream = true;;
        }
        ++it;
    }
    LOG2("%s, videoHALStreamOrder size: %zu, stillHALStreamOrder: %zu", __func__,
         videoHALStreamOrder.size(), stillHALStreamOrder.size());

    // 2: set the flags to itself
    bool needReplace = (!hasPreviewStream && previewStreamIndex != -1) ? true : false;
    for (auto it = videoHALStreamOrder.begin(); it != videoHALStreamOrder.end(); ++it) {
        // Possiblely put preview stream to HAL
        if (needReplace && (requestStreams[previewStreamIndex].first->width * it->first->height ==
            requestStreams[previewStreamIndex].first->height * it->first->width)) {
            it->first = requestStreams[previewStreamIndex].first;
            it->second = requestStreams[previewStreamIndex].second;
            needReplace = false;
        }

        LOG2("%s, bind itself for video stream index: %d", __func__, it->second);
        halStreamFlag[it->second] = it->second;
        activeHALNum++;
    }

    // 3: Need to sort them if there are multi streams configured to HAL
    if (videoHALStreamOrder.size() > 1) {
        videoHALStreamOrder.sort([](std::pair<icamera::stream_t*, int>& s1,
                                    std::pair<icamera::stream_t*, int>& s2) {
            if (s1.first->width * s1.first->height > s2.first->width * s2.first->height)
                return true;
            else
                return false;
        });

        float baseRatio = static_cast<float>(videoHALStreamOrder.front().first->width) /
                          videoHALStreamOrder.front().first->height;
        for (auto& vs : videoHALStreamOrder) {
            float streamRatio = static_cast<float>(vs.first->width) / vs.first->height;
            if (streamRatio != baseRatio) {
                LOG2("%s, baseRatio: %f, streamRatio: %f, there is FOV lose", __func__,
                     baseRatio, streamRatio);
            }
        }
    }

    // Select the still stream and its listener
    if (!stillHALStreamOrder.empty()) {
        activeHALNum++;
        for (uint32_t i = 0; i < requestStreamNum; i++) {
            // has only 1 Still HAL stream, other still stream should be listeners
            if (halStreamList[i].usage == icamera::CAMERA_STREAM_STILL_CAPTURE) {
                halStreamFlag[i] = stillHALStreamOrder.back().second;
                LOG2("%s, bind still stream %d, to index: %d", __func__, i,
                     stillHALStreamOrder.back().second);
            }
        }
    }

    // Select other video streams not been put into HAL slot, bind to the HAL stream
    for (uint32_t i = 0; i < requestStreamNum; i++) {
        /* skip the streams which have been selected to HAL
         * still, opaque and avaVideoSlot video streams
         */
        if (halStreamFlag[i] != -1) continue;

        // 1: same resolution
        auto it = videoHALStreamOrder.begin();
        for (; it != videoHALStreamOrder.end(); ++it) {
            if (halStreamList[i].width == it->first->width &&
                halStreamList[i].height == it->first->height) {
                halStreamFlag[i] = it->second;
                break;
            }
        }
        if (it != videoHALStreamOrder.end())
            continue;

        // 2: same ratio
        it = videoHALStreamOrder.begin();
        for (; it != videoHALStreamOrder.end(); ++it) {
            if (halStreamList[i].width * it->first->height ==
                halStreamList[i].height * it->first->width) {
                halStreamFlag[i] = it->second;
                break;
            }
        }
        if (it != videoHALStreamOrder.end())
            continue;

        // 3: bind to the biggest stream
        halStreamFlag[i] = videoHALStreamOrder.front().second;
    }

    LOG1("has %d HAL Streams", activeHALNum);
    for (int i = 0; i < requestStreamNum; i++)
        LOG1("user Stream %d bind to HAL Stream %d", i, halStreamFlag[i]);

    return activeHALNum;
}

}  // namespace camera3
