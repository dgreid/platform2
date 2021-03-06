/*
 * Copyright (C) 2014-2019 Intel Corporation
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
#include "Camera3Request.h"
#include "RequestThread.h"
#include "PlatformData.h"
#include "PerformanceTraces.h"

namespace cros {
namespace intel {

ResultProcessor::ResultProcessor(RequestThread * aReqThread,
                                 const camera3_callback_ops_t * cbOps) :
    mRequestThread(aReqThread),
    mCameraThread("ResultProcessor"),
    mCallbackOps(cbOps),
    mPartialResultCount(0)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    mReqStatePool.init(MAX_REQUEST_IN_TRANSIT);
    if (!mCameraThread.Start()) {
        LOGE("Camera thread failed to start");
    }
}

ResultProcessor::~ResultProcessor()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    mCameraThread.Stop();
    mRequestsPendingMetaReturn.clear();
    mRequestsInTransit.clear();
}

/**********************************************************************
 * Public methods
 */
/**********************************************************************
 * Thread methods
 */
status_t ResultProcessor::requestExitAndWait(void)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    status_t status = NO_ERROR;
    base::Callback<status_t()> closure =
            base::Bind(&ResultProcessor::handleExit, base::Unretained(this));
    mCameraThread.PostTaskSync<status_t>(FROM_HERE, closure, &status);
    return status;
}

status_t ResultProcessor::handleExit(void)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    while ((mRequestsInTransit.size()) != 0) {
        recycleRequest((mRequestsInTransit.begin()->second)->request);
    }
    return OK;
}

/**
 * registerRequest
 *
 * Present a request to the ResultProcessor.
 * This call is used to inform the result processor that a new request
 * has been sent to the PSL. RequestThread uses this method
 * ResultProcessor will store its state in an internal vector to track
 * the different events during the lifetime of the request.
 *
 * Once the request has been completed ResultProcessor returns the request
 * to the RequestThread for recycling, using the method:
 * RequestThread::returnRequest();
 *
 * \param request [IN] item to register
 * \return NO_ERROR
 */
status_t ResultProcessor::registerRequest(Camera3Request *request)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    MessageRegisterRequest msg;
    msg.request = request;
    status_t status = NO_ERROR;
    base::Callback<status_t()> closure =
            base::Bind(&ResultProcessor::handleRegisterRequest,
                       base::Unretained(this), base::Passed(std::move(msg)));
    mCameraThread.PostTaskSync<status_t>(FROM_HERE, closure, &status);
    return status;
}

status_t ResultProcessor::handleRegisterRequest(MessageRegisterRequest msg)
{
    status_t status = NO_ERROR;
    RequestState_t* reqState;
    int reqId = msg.request->getId();
    /**
     * check if the request was not already register. we may receive registration
     * request duplicated in case of request that are held by the PSL
     */
    if (getRequestsInTransit(&reqState, reqId) == NO_ERROR) {
        return NO_ERROR;
    }

    status = mReqStatePool.acquireItem(&reqState);
    if (status != NO_ERROR) {
        LOGE("Could not acquire an empty reqState from the pool");
        return status;
    }

    reqState->init(msg.request);
    mRequestsInTransit.insert(RequestsInTransitPair(reqState->reqId, reqState));
    LOG2("<Request %d> camera id %d registered", reqState->reqId, msg.request->getCameraId());
    /**
     * get the number of partial results the request may return, this is not
     *  going to change once the camera is open, so do it only once.
     *  We initialize the value to 0, the minimum value should be 1
     */
    if (CC_UNLIKELY(mPartialResultCount == 0)) {
        mPartialResultCount = msg.request->getpartialResultCount();
    }
    return status;
}

status_t ResultProcessor::shutterDone(Camera3Request* request,
                                      int64_t timestamp)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    MessageShutterDone msg;
    msg.request = request;
    msg.time = timestamp;

    base::Callback<status_t()> closure =
            base::Bind(&ResultProcessor::handleShutterDone,
                       base::Unretained(this), base::Passed(std::move(msg)));
    mCameraThread.PostTaskAsync<status_t>(FROM_HERE, closure);
    return OK;
}

status_t ResultProcessor::handleShutterDone(MessageShutterDone msg)
{
    status_t status = NO_ERROR;
    int reqId = 0;
    Camera3Request* request = msg.request;

    reqId = request->getId();
    LOG2("%s for <Request %d>", __func__, reqId);
    PERFORMANCE_HAL_ATRACE_PARAM1("reqId", reqId);

    RequestState_t *reqState = nullptr;
    if (getRequestsInTransit(&reqState, reqId) == BAD_VALUE) {
        LOGE("Request %d was not registered find the bug", reqId);
        return BAD_VALUE;
    }

    reqState->shutterTime = msg.time;
    returnShutterDone(reqState);

    if (!reqState->pendingOutputBuffers.empty() || reqState->pendingInputBuffer) {
        returnPendingBuffers(reqState);
    }

    unsigned int resultsReceived = reqState->pendingPartialResults.size();
    bool allMetaReceived = (resultsReceived == mPartialResultCount);

    if (allMetaReceived) {
        returnPendingPartials(reqState);
    }

    bool allMetaDone = (reqState->partialResultReturned == mPartialResultCount);
    bool allBuffersDone = (reqState->buffersReturned == reqState->buffersToReturn);
    if (allBuffersDone && allMetaDone) {
        status = recycleRequest(request);
    }

    return status;
}

/**
 * returnShutterDone
 * Signal to the client that shutter event was received
 * \param reqState [IN/OUT] state of the request
 */
void ResultProcessor::returnShutterDone(RequestState_t* reqState)
{
    if (reqState->isShutterDone)
        return;

    camera3_notify_msg shutter;
    shutter.type = CAMERA3_MSG_SHUTTER;
    shutter.message.shutter.frame_number = reqState->reqId;
    shutter.message.shutter.timestamp =reqState->shutterTime;
    mCallbackOps->notify(mCallbackOps, &shutter);
    reqState->isShutterDone = true;
    LOG2("<Request %d> camera id %d shutter done", reqState->reqId, reqState->request->getCameraId());
}

status_t ResultProcessor::metadataDone(Camera3Request* request,
                                       int resultIndex)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);
    MessageMetadataDone msg;
    msg.request = request;
    msg.resultIndex = resultIndex;

    base::Callback<status_t()> closure =
            base::Bind(&ResultProcessor::handleMetadataDone,
                       base::Unretained(this), base::Passed(std::move(msg)));
    mCameraThread.PostTaskAsync<status_t>(FROM_HERE, closure);
    return OK;
}

status_t ResultProcessor::handleMetadataDone(MessageMetadataDone msg)
{
    status_t status = NO_ERROR;
    Camera3Request* request = msg.request;
    int reqId = request->getId();
    LOG2("%s for <Request %d>", __func__, reqId);
    PERFORMANCE_HAL_ATRACE_PARAM1("reqId", reqId);

    RequestState_t *reqState = nullptr;
    if (getRequestsInTransit(&reqState, reqId) == BAD_VALUE) {
        LOGE("Request %d was not registered:find the bug", reqId);
        return BAD_VALUE;
    }

    if (msg.resultIndex >= 0) {
        /**
         * New Partial metadata result path. The result buffer is not the
         * settings but a separate buffer stored in the request.
         * The resultIndex indicates which one.
         * This can be returned straight away now that we have declared 3.2
         * device version. No need to enforce the order between shutter events
         * result and buffers. We do not need to store the partials either.
         * we can return them directly
         */
        status = returnResult(reqState, msg.resultIndex);

        bool allMetadataDone = (reqState->partialResultReturned == mPartialResultCount);
        bool allBuffersDone = (reqState->buffersReturned == reqState->buffersToReturn);
        if (allBuffersDone && allMetadataDone) {
           status = recycleRequest(request);
        }
        return status;
    }

    reqState->pendingPartialResults.emplace_back(request->getSettings());
    LOG2("<Request %d> camera id %d Metadata arrived %zu/%d", reqId, reqState->request->getCameraId(),
         reqState->pendingPartialResults.size(),mPartialResultCount);

    if (!reqState->isShutterDone) {
        LOG2("@%s metadata arrived before shutter, storing", __func__);
        return NO_ERROR;
    }

    unsigned int resultsReceived = reqState->pendingPartialResults.size();
    bool allMetaReceived = (resultsReceived == mPartialResultCount);

    if (allMetaReceived) {
        returnPendingPartials(reqState);
    }

    bool allMetadataDone = (reqState->partialResultReturned == mPartialResultCount);
    bool allBuffersDone = (reqState->buffersReturned == reqState->buffersToReturn);
    if (allBuffersDone && allMetadataDone) {
        status = recycleRequest(request);
    }

    /**
     * if the metadata done for the next request is available then send it.
     *
     */
    if (allMetadataDone) {
        returnStoredPartials();
    }

    return status;
}

/**
 * returnStoredPartials
 * return the all stored pending metadata
 */
status_t ResultProcessor::returnStoredPartials()
{
    status_t status = NO_ERROR;

    while (mRequestsPendingMetaReturn.size() > 0) {
        int reqId = mRequestsPendingMetaReturn.front();
        LOG2("stored metadata req size:%zu, first reqid:%d", mRequestsPendingMetaReturn.size(), reqId);
        RequestState_t *reqState = nullptr;

        if (getRequestsInTransit(&reqState, reqId) == BAD_VALUE) {
            LOGE("Request %d was not registered:find the bug", reqId);
            mRequestsPendingMetaReturn.pop_front();
            return BAD_VALUE;
        }

        returnPendingPartials(reqState);
        bool allMetadataDone = (reqState->partialResultReturned == mPartialResultCount);
        bool allBuffersDone = (reqState->buffersReturned == reqState->buffersToReturn);
        if (allBuffersDone && allMetadataDone) {
            status = recycleRequest(reqState->request);
        }

        mRequestsPendingMetaReturn.pop_front();
    }
    return status;
}


status_t ResultProcessor::bufferDone(Camera3Request* request,
                                     std::shared_ptr<CameraBuffer> buffer)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);
    MessageBufferDone msg;
    msg.request = request;
    msg.buffer = buffer;

    base::Callback<status_t()> closure =
            base::Bind(&ResultProcessor::handleBufferDone,
                       base::Unretained(this), base::Passed(std::move(msg)));
    mCameraThread.PostTaskAsync<status_t>(FROM_HERE, closure);
    return OK;
}

/**
 * handleBufferDone
 *
 * Try to return the buffer provided by PSL to client
 * This method checks whether we can return the buffer straight to client or
 * we need to hold it until shutter event has been received.
 * \param msg [IN] Contains the buffer produced by PSL
 */
status_t ResultProcessor::handleBufferDone(MessageBufferDone msg)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);
    status_t status = NO_ERROR;
    Camera3Request* request = msg.request;
    std::shared_ptr<CameraBuffer> buffer = msg.buffer;

    if (buffer.get() && buffer->isLocked())
        buffer->unlock();

    int reqId = request->getId();
    if (buffer.get() && buffer->getOwner()) {
        PERFORMANCE_HAL_ATRACE_PARAM1("streamAndReqId",
            reqId | ((buffer->getOwner()->seqNo() & 0x0f) << 28));
    } else {
        PERFORMANCE_HAL_ATRACE_PARAM1("reqId", reqId);
    }

    if (buffer.get()) {
        buffer->deinit();
    }

    RequestState_t *reqState = nullptr;
    if (getRequestsInTransit(&reqState, reqId) == BAD_VALUE) {
        LOGE("Request %d was not registered find the bug", reqId);
        return BAD_VALUE;
    }

    LOG2("<Request %d> camera id %d buffer received from PSL", reqId, reqState->request->getCameraId());
    if (reqState->request->isInputBuffer(buffer)) {
        reqState->pendingInputBuffer = buffer;
    } else {
        reqState->pendingOutputBuffers.emplace_back(buffer);
    }
    if (!reqState->isShutterDone) {
        LOG2("@%s Buffer arrived before shutter req %d, queue it", __func__, reqId);
        return NO_ERROR;
    }

    returnPendingBuffers(reqState);

    if (!reqState->pendingPartialResults.empty()) {
        returnPendingPartials(reqState);
    }

    bool allMetaDone = (reqState->partialResultReturned == mPartialResultCount);
    bool allBuffersDone = (reqState->buffersReturned == reqState->buffersToReturn);
    if (allBuffersDone && allMetaDone) {
        status = recycleRequest(request);
    }
    return status;
}

void ResultProcessor::returnPendingBuffers(RequestState_t* reqState)
{
    LOG2("@%s Request %d %u buffs", __func__, reqState->reqId, reqState->buffersToReturn);
    unsigned int i;
    std::shared_ptr<CameraBuffer> pendingBuf;

    /**
     * protection against duplicated calls when all buffers have been returned
     */
    if (reqState->buffersReturned == reqState->buffersToReturn) {
        LOGW("trying to return buffers twice. Check PSL implementation");
        return;
    }

    for (i = 0; i < reqState->pendingOutputBuffers.size(); i++) {
        pendingBuf = reqState->pendingOutputBuffers[i];
        CheckError(pendingBuf == nullptr, VOID_VALUE, "@%s: pendingBuf is nullptr, index: %d",
            __func__, i);
        processCaptureResult(reqState,pendingBuf);
    }
    reqState->pendingOutputBuffers.clear();

    //The input buffer is returned when all output buffers have been returned.
    if (reqState->buffersReturned + 1 == reqState->buffersToReturn && reqState->pendingInputBuffer) {
        processCaptureResult(reqState,reqState->pendingInputBuffer);
        reqState->pendingInputBuffer = nullptr;
    }
}

void ResultProcessor::processCaptureResult(RequestState_t* reqState,std::shared_ptr<CameraBuffer> resultBuf)
{
    camera3_capture_result_t result = {};
    camera3_stream_buffer_t buf = {};
    Camera3Request* request = reqState->request;
    CheckError(request == nullptr, VOID_VALUE, "@%s: request is nullptr", __FUNCTION__);

    if (!request->isInputBuffer(resultBuf)) {
        result.num_output_buffers = 1;
    }
    result.frame_number = reqState->reqId;

    buf.status = resultBuf->status();
    buf.stream = resultBuf->getOwner()->getStream();
    if (buf.stream) {
        LOG2("<Request %d> width:%d, height:%d, fmt:%d", reqState->reqId, buf.stream->width, buf.stream->height, buf.stream->format);
    }
    buf.buffer = resultBuf->getBufferHandlePtr();
    resultBuf->getFence(&buf);
    result.result = nullptr;
    if (request->isInputBuffer(resultBuf)) {
        result.input_buffer = &buf;
        LOG2("<Request %d> return an input buffer", reqState->reqId);
    } else {
        LOG2("<Request %d> return an output buffer", reqState->reqId);
        result.output_buffers = &buf;
    }

    mCallbackOps->process_capture_result(mCallbackOps, &result);
    resultBuf->getOwner()->decOutBuffersInHal();
    reqState->buffersReturned += 1;
    LOG2("<Request %d> camera id %d buffer done %d/%d ", reqState->reqId,
        reqState->request->getCameraId(), reqState->buffersReturned, reqState->buffersToReturn);
}

/**
 * Returns the single partial result stored in the vector.
 * In the future we will have more than one.
 */
void ResultProcessor::returnPendingPartials(RequestState_t* reqState)
{
    camera3_capture_result result;
    CLEAR(result);

    // it must be 1 for >= CAMERA_DEVICE_API_VERSION_3_2 if we don't support partial metadata
    result.partial_result = mPartialResultCount;

    //TODO: combine them all in one metadata buffer and return
    result.frame_number = reqState->reqId;

    // check if metadata result of the previous request is returned
    int pre_reqId = reqState->reqId - 1;
    RequestState_t *pre_reqState = nullptr;

    if (getRequestsInTransit(&pre_reqState, pre_reqId) == NO_ERROR) {
        if (pre_reqState->partialResultReturned == 0) {
            LOG2("%s add reqId %d into pending list, wait the metadata of the previous request to return",
                __func__, reqState->reqId);
            std::list<int>::iterator it;
            for (it = mRequestsPendingMetaReturn.begin();
                  (it != mRequestsPendingMetaReturn.end()) && (*it < reqState->reqId);
                  it++);
            mRequestsPendingMetaReturn.insert(it, reqState->reqId);
            return;
        }
    }

    const android::CameraMetadata * settings = reqState->pendingPartialResults[0];

    result.result = settings->getAndLock();
    result.num_output_buffers = 0;

    mCallbackOps->process_capture_result(mCallbackOps, &result);

    settings->unlock(result.result);

    reqState->partialResultReturned += 1;
    LOG2("<Request %d> camera id %d result cb done",reqState->reqId, reqState->request->getCameraId());
    reqState->pendingPartialResults.clear();
}

/**
 * returnResult
 *
 * Returns a partial result metadata buffer, just one.
 *
 * \param reqState[IN]: Request State control structure
 * \param returnIndex[IN]: index of the result buffer in the array of result
 *                         buffers stored in the request
 */
status_t ResultProcessor::returnResult(RequestState_t* reqState, int returnIndex)
{
    status_t status = NO_ERROR;
    camera3_capture_result result;
    android::CameraMetadata *resultMetadata;
    CLEAR(result);
    resultMetadata = reqState->request->getPartialResultBuffer(returnIndex);
    if (resultMetadata == nullptr) {
        LOGE("Cannot get partial result buffer");
        return UNKNOWN_ERROR;
    }

    // Swap thumbnail width/height in metadata if necessary
    camera_metadata_entry entry = resultMetadata->find(ANDROID_JPEG_THUMBNAIL_SIZE);
    if (entry.count >= 2 && reqState->request->shouldSwapWidthHeight()) {
        std::swap(entry.data.i32[0], entry.data.i32[1]);
    }

    // This value should be between 1 and android.request.partialResultCount
    // The index goes between 0-partialResultCount -1
    result.partial_result = returnIndex + 1;
    result.frame_number = reqState->reqId;
    result.result = resultMetadata->getAndLock();
    result.num_output_buffers = 0;

    mCallbackOps->process_capture_result(mCallbackOps, &result);

    resultMetadata->unlock(result.result);

    reqState->partialResultReturned += 1;
    LOG2("<Request %d> camera id %d result cb done", reqState->reqId, reqState->request->getCameraId());
    return status;
}

/**
 * getRequestsInTransit
 *
 * Returns a RequestState in the map at index.
 *
 * \param reqState[OUT]: Request State control structure
 * \param index[IN]: index of the result state, it's request Id mapped to the state
 */
status_t ResultProcessor::getRequestsInTransit(RequestState_t** reqState, int index)
{
    status_t state = NO_ERROR;
    std::map<int, RequestState_t*>::const_iterator it;

    it = mRequestsInTransit.find(index);
    if (it == mRequestsInTransit.cend()) {
        LOG2("%s, Result State not found for id %d", __func__, index);
        state = BAD_VALUE;
    } else {
        state = NO_ERROR;
        *reqState = it->second;
    }

    return state;
}

/**
 * Request is fully processed
 * send the request object back to RequestThread for recycling
 * return the request-state struct to the pool
 */
status_t ResultProcessor::recycleRequest(Camera3Request *req)
{
    status_t status = NO_ERROR;
    int id = req->getId();
    RequestState_t *reqState = mRequestsInTransit.at(id);
    status = mReqStatePool.releaseItem(reqState);
    if (status != NO_ERROR) {
        LOGE("Request State pool failure[%d] , recycling is broken!!", status);
    }

    mRequestsInTransit.erase(id);
    mRequestThread->returnRequest(req);
    LOG2("<Request %d> camera id %d OUT from ResultProcessor",id, reqState->request->getCameraId());
    return status;
}

status_t ResultProcessor::deviceError(void)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    base::Callback<void()> closure =
            base::Bind(&ResultProcessor::handleDeviceError,
                       base::Unretained(this));
    mCameraThread.PostTaskAsync<void>(FROM_HERE, closure);
    return OK;
}

void ResultProcessor::handleDeviceError(void)
{
    camera3_notify_msg msg;
    CLEAR(msg);
    msg.type = CAMERA3_MSG_ERROR;
    msg.message.error.error_code = CAMERA3_MSG_ERROR_DEVICE;
    mCallbackOps->notify(mCallbackOps, &msg);
    LOG2("@%s done", __func__);
}
//----------------------------------------------------------------------------
} /* namespace intel */
} /* namespace cros */
