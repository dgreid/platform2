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

#pragma once

#include <hardware/camera3.h>
#include <sys/time.h>

#include <map>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

#include "Camera3AMetadata.h"
#include "Camera3Buffer.h"
#include "HALv3Header.h"
#include "HALv3Interface.h"
#include "Parameters.h"
#include "Thread.h"

namespace camera3 {

// Store metadata that are created by the AAL
// To avoid continuous allocation/de-allocation of metadata buffers
class MetadataMemory {
 public:
    MetadataMemory();
    ~MetadataMemory();

    // Don't access to metadata and memory in parellel
    // because metadata may reallocate memory when new entries are added.
    android::CameraMetadata* getMetadata();  // For entries update
    camera_metadata_t* getMemory();          // For metadata copy

    // Helper function to avoid memory reallocation
    void copyMetadata(const camera_metadata_t* src);

 private:
    android::CameraMetadata mMeta;  // May reallocate buffer if entries are added
    camera_metadata_t* mMemory;
};

struct RequestState {
    uint32_t frameNumber;

    bool isShutterDone;

    unsigned int partialResultReturned;
    unsigned int partialResultCount;

    unsigned int buffersReturned;
    unsigned int buffersToReturn;

    MetadataMemory* metaResult;

    RequestState() {
        frameNumber = 0;
        isShutterDone = false;
        partialResultReturned = 0;
        partialResultCount = 0;
        buffersReturned = 0;
        buffersToReturn = 0;
        metaResult = nullptr;
    }
};

struct MetadataEvent {
    uint32_t frameNumber;
    const icamera::Parameters* parameter;
};

struct ShutterEvent {
    uint32_t frameNumber;
    uint64_t timestamp;
};

struct BufferEvent {
    uint32_t frameNumber;
    const camera3_stream_buffer_t* outputBuffer;
    uint64_t timestamp;
    long sequence;
};

struct ReferenceParam {
    int64_t sensorExposure;
    int32_t sensorIso;
};

/**
 * \brief An interface used to callback buffer event.
 */
class CallbackEventInterface {
 public:
    CallbackEventInterface() {}
    virtual ~CallbackEventInterface() {}

    virtual int metadataDone(const MetadataEvent& event) = 0;
    virtual int bufferDone(const BufferEvent& event) = 0;
    virtual int shutterDone(const ShutterEvent& event) = 0;
};

/**
 * \class ResultProcessor
 *
 * This class is used to handle shutter done, buffer done and metadata done
 * event.
 *
 */
class ResultProcessor : public CallbackEventInterface {
 public:
    ResultProcessor(int cameraId, const camera3_callback_ops_t* callback,
                    RequestManagerCallback* requestManagerCallback);
    virtual ~ResultProcessor();

    int registerRequest(const camera3_capture_request_t* request,
                        std::shared_ptr<Camera3Buffer> inputCam3Buf);

    virtual int metadataDone(const MetadataEvent& event);
    virtual int shutterDone(const ShutterEvent& event);
    virtual int bufferDone(const BufferEvent& event);

    void callbackNotify(const icamera::camera_msg_data_t& data);

    // Used to handle Opaque raw reprocessing
    void clearRawBufferInfoMap(void);
    void checkAndChangeRawbufferInfo(long* sequence, uint64_t* timestamp);

    // Notify error to camera service
    void notifyError();

 private:
    bool checkRequestDone(const RequestState& requestState);
    void returnRequestDone(uint32_t frameNumber);

    MetadataMemory* acquireMetadataMemory();
    void releaseMetadataMemory(MetadataMemory* metaMem);

    void returnInputBuffer(uint32_t frameNumber);
    void updateMetadata(const icamera::Parameters& parameter, android::CameraMetadata* settings);

 private:
    class ResultThread : public icamera::Thread {
     public:
        ResultThread(int cameraId, ResultProcessor* resultProcessor);
        ~ResultThread();

        void sendEvent(const icamera::camera_msg_data_t& data);

     private:
        virtual bool threadLoop();

     private:
        const uint64_t kMaxDuration = 2000000000;  // 2000ms
        int mCameraId;
        ResultProcessor* mResultProcessor;

        std::condition_variable mEventCondition;
        std::mutex mEventQueueLock;
        std::queue<icamera::camera_msg_data_t> mEventQueue;
    };
    std::unique_ptr<ResultThread> mResultThread;

    int mCameraId;
    const camera3_callback_ops_t* mCallbackOps;

    // mLock is used to protect mRequestStateVector
    std::mutex mLock;
    std::vector<RequestState> mRequestStateVector;
    std::vector<MetadataMemory*> mMetadataVector;
    MetadataMemory* mLastSettings;

    RequestManagerCallback* mRequestManagerCallback;

    Camera3AMetadata* mCamera3AMetadata;

    std::unordered_map<int, std::shared_ptr<Camera3Buffer>> mInputCam3Bufs;
    timeval mRequestTime;
    ReferenceParam mLastParams;

    // first key is sequence from HAL, second key is timestamp of RAW buffer
    std::map<int64_t, uint64_t> mOpaqueRawInfoMap;
    std::mutex mmOpaqueRawInfoMapLock;  // used to protect mOpaqueRawInfoMap
};

}  // namespace camera3
