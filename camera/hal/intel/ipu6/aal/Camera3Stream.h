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

#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Camera3BufferPool.h"
#include "FaceDetection.h"
#include "PostProcessor.h"
#include "ResultProcessor.h"
#include "Thread.h"

namespace camera3 {

struct CaptureResult {
    uint32_t frameNumber;
    camera3_stream_buffer_t outputBuffer;
    buffer_handle_t handle;
    std::shared_ptr<Camera3Buffer> inputCam3Buf;
    icamera::Parameters param;
};

struct StreamComInfo {
    std::shared_ptr<Camera3Buffer> cam3Buf;
    icamera::Parameters parameter;
};

/**
 * \class InternalBufferPool
 *
 * This class is used to manage a local memory pool for still and post
 * processing stream It needs to follow the calling sequence: allocBuffers ->
 * acquireBuffer -> findBuffer -> returnBuffer
 */
class InternalBufferPool {
 public:
    InternalBufferPool();
    ~InternalBufferPool();

    icamera::status_t allocBuffers(const icamera::stream_t& stream, uint32_t numBuffers,
                                   int cameraId);
    void destroyBuffers();
    std::shared_ptr<Camera3Buffer> acquireBuffer();
    void returnBuffer(std::shared_ptr<Camera3Buffer> buffer);
    std::shared_ptr<Camera3Buffer> findBuffer(void* memAddr);

 private:
    std::unordered_map<std::shared_ptr<Camera3Buffer>, bool>
        mInterBuf;  // first: camera3Buffer, second: busy
    std::mutex mLock;
};

/**
 * \class Camera3Stream
 *
 * This class is used to handle requests. It has the following
 * roles:
 * - It instantiates PostProcessor.
 */
class Camera3Stream : public icamera::Thread {
 public:
    Camera3Stream(int cameraId, CallbackEventInterface* callback, uint32_t maxNumReqInProc,
                  const icamera::stream_t& halStream, const camera3_stream_t& stream,
                  const camera3_stream_t* inputStream = nullptr, bool isHWStream = false);
    virtual ~Camera3Stream();

    virtual bool threadLoop();
    virtual void requestExit();

    int processRequest(const std::shared_ptr<Camera3Buffer>& inputCam3Buf,
                       const camera3_stream_buffer_t& outputBuffer, uint32_t frameNumber);

    void queueBufferDone(uint32_t frameNumber, const std::shared_ptr<Camera3Buffer>& inputCam3Buf,
                         const camera3_stream_buffer_t& outputBuffer,
                         const icamera::Parameters& param);
    int setActive(bool state);
    bool isActive() { return mStreamState; }
    void activateFaceDetection(unsigned int maxFaceNum);
    int getPostProcessType() { return mPostProcessType; }
    void sendEvent(const icamera::camera_msg_data_t& data);
    void addListener(Camera3Stream* listener);
    // fetch the buffers will be queued to Hal, HAL stream only
    bool fetchRequestBuffers(icamera::camera_buffer_t* buffer, uint32_t frameNumber);
    // check if the HW stream should be enabled by listener request
    void checkListenerRequest(uint32_t frameNumber);
    // called by RequestManager indicates the frame is done, release buffers
    void requestStreamDone(uint32_t frameNumber);

 private:
    void handleSofAlignment();
    /* get the request status anf Camera3Buf of this stream
    ** return nullptr if stream not requested the frame
    */
    std::shared_ptr<StreamComInfo> getCaptureRequest(uint32_t frameNumber);
    void notifyListenerBufferReady(uint32_t frameNumber,
                                   const std::shared_ptr<StreamComInfo>& halOutput);

    /* HAL stream or listener stream to wait capture buffer result ready,
     ** called in ThreadLoop, return false if need to wait,
     ** return true to continue the threadloop.
     */
    bool waitCaptureResultReady();

 private:
    const uint64_t kMaxDuration = 2000000000;  // 2000ms

    int mCameraId;
    std::condition_variable mBufferDoneCondition;
    std::mutex mLock;

    std::condition_variable mSofCondition;
    std::mutex mSofLock;

    CallbackEventInterface* mEventCallback;

    int mPostProcessType;
    std::unique_ptr<PostProcessor> mPostProcessor;

    bool mStreamState;
    icamera::stream_t mHALStream;
    uint32_t mMaxNumReqInProc;
    std::unique_ptr<Camera3BufferPool> mBufferPool;

    camera3_stream_t mStream;

    /* key is frame number, value is CaptureResult */
    std::map<uint32_t, std::shared_ptr<CaptureResult>> mCaptureResultMap;
    std::map<buffer_handle_t, std::shared_ptr<Camera3Buffer>> mBuffers;

    icamera::FaceDetection* mFaceDetection;
    unsigned int mFDRunDefaultInterval;  // FD running's interval frames.
    unsigned int mFDRunIntervalNoFace;   // FD running's interval frames without face.
    unsigned int mFDRunInterval;         // run 1 frame every mFDRunInterval frames.
    unsigned int mFrameCnt;              // from 0 to (mFDRunInterval - 1).

    int mInputPostProcessType;
    std::unique_ptr<PostProcessor> mInputPostProcessor;
    std::unique_ptr<camera3_stream_t> mInputStream;

    bool mIsHALStream;

    /* save output info, each stream can accept maxNumReqInProc
     ** requests. used by HAL stream to get listener request status
     */
    std::unordered_map<uint32_t, std::shared_ptr<StreamComInfo>> mCaptureRequest;
    std::vector<Camera3Stream*> mListeners;
    // HAL streams output result, listener stream will wait on it before process
    std::unordered_map<uint32_t, std::shared_ptr<StreamComInfo>> mHALStreamOutput;

    // save buffer obj when HAL choose buffer from pool to do qbuf/dqbuf
    std::unordered_map<uint32_t, std::shared_ptr<Camera3Buffer>> mQueuedBuffer;

    void faceRunningByCondition(const icamera::camera_buffer_t& buffer);
};

}  // namespace camera3
