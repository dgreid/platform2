/*
 * Copyright (C) 2013-2020 Intel Corporation
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

#ifndef  _CAMERA3_HAL_RESULT_PROCESSOR_H_
#define  _CAMERA3_HAL_RESULT_PROCESSOR_H_

#include <Utils.h>
#include <hardware/camera3.h>
#include <map>
#include <list>

#include "LogHelper.h"
#include "CameraStream.h"
#include "ItemPool.h"

#include "IErrorCallback.h"

#include <cros-camera/camera_thread.h>

namespace cros {
namespace intel {

/**
 * Forward declarations to avoid circular references of  header files
 */
class RequestThread;
class Camera3Request;


/**
 * \class ResultProcessor
 * This class is responsible for managing the return of completed requests to
 * the HAL client.
 *
 * PSL implementations may return shutter notification, buffers and metadata
 * in any order. The ResultProcessor is in charge of ensuring the
 * callbacks follow the correct order or return the corresponding error.
 *
 * The result processor tracks the relevant events in the life-cycle of a
 * request, these are:
 * - shutter notification
 * - buffer return
 * - partial metadata return
 */
class ResultProcessor : public IErrorCallback, public IRequestCallback {
public:
    ResultProcessor(RequestThread * aReqThread,
                    const camera3_callback_ops_t * cbOps);
    virtual ~ResultProcessor();
    status_t requestExitAndWait();
    status_t registerRequest(Camera3Request *request);

    //IRequestCallback implementation
    virtual status_t shutterDone(Camera3Request* request, int64_t timestamp);
    virtual status_t metadataDone(Camera3Request* request,
                                  int resultIndex = -1);
    virtual status_t bufferDone(Camera3Request* request,
                               std::shared_ptr<CameraBuffer> buffer);
    virtual status_t deviceError(void);

private:  /* types  and constants */
    /**
     * \struct RequestState_t
     * This struct is used to track the life cycle of a request.
     * ResultProcessor keeps a Vector with the states of the requests currently
     * in the PSL.
     * As the PSL reports partial completion using the IRequestCallback interface
     * the values in this structure are updated.
     * Always in the context of the ResultProcesssor Thread, avoiding the need
     * of mutex locking
     **/
    typedef struct RequestState_t {
        int reqId;
        Camera3Request *request;

        /**
         * Shutter control variables
         */
        bool isShutterDone;     /*!> from AAL to client */
        int64_t shutterTime;
        /**
         * Metadata result control variables
         */
        unsigned int partialResultReturned;  /*!> from AAL to client */
        std::vector<const android::CameraMetadata*> pendingPartialResults;
        /**
         * Output buffers control variables
         */
        unsigned int buffersReturned;  /*!> from AAL to client */
        unsigned int buffersToReturn;  /*!> total output and input buffer count of request */
        std::shared_ptr<CameraBuffer> pendingInputBuffer; /*!> where we store the intput buffers
                                                             received from PSL*/
        std::vector<std::shared_ptr<CameraBuffer> > pendingOutputBuffers; /*!> where we store the
                                                             output buffers received from PSL*/

        void init(Camera3Request* req) {
            pendingOutputBuffers.clear();
            pendingPartialResults.clear();
            reqId = req->getId();
            shutterTime = 0;
            isShutterDone = false;
            partialResultReturned = 0;
            buffersReturned = 0;
            buffersToReturn = req->getNumberOutputBufs() + (req->hasInputBuf() ? 1 : 0);
            request = req;
            pendingInputBuffer = nullptr;
        }
    } RequestState_t;

    static const int MAX_REQUEST_IN_TRANSIT = 10;    /*!> worst case number used
                                                         for pool allocation */

    struct MessageMetadataDone {
        Camera3Request* request;  // any sent request
        int resultIndex;    /*!> Index from the partial result array that is
                                 being returned */
     };

    struct MessageShutterDone {
        Camera3Request* request;  // any sent request
        int64_t time;
     };

    struct MessageBufferDone {
        Camera3Request* request;  // any sent request
        std::shared_ptr<CameraBuffer> buffer;
     };

    struct MessageRegisterRequest {
        Camera3Request* request;  // any sent request
     };

private:  /* methods */
    status_t handleExit();
    status_t handleShutterDone(MessageShutterDone msg);
    status_t handleMetadataDone(MessageMetadataDone msg);
    status_t handleBufferDone(MessageBufferDone msg);
    status_t handleRegisterRequest(MessageRegisterRequest msg);
    void handleDeviceError(void);
    status_t recycleRequest(Camera3Request *req);
    void returnPendingBuffers(RequestState_t *reqState);
    void processCaptureResult(RequestState_t* reqState,std::shared_ptr<CameraBuffer> resultBuf);
    void returnPendingPartials(RequestState_t *reqState);
    void returnShutterDone(RequestState_t *reqState);
    status_t returnStoredPartials(void);
    status_t returnResult(RequestState_t* reqState, int returnIndex);
    status_t getRequestsInTransit(RequestState_t** reqState, int index);

private:  /* members */
    RequestThread *mRequestThread;
    cros::CameraThread mCameraThread;

    const camera3_callback_ops_t *mCallbackOps;
    ItemPool<RequestState_t> mReqStatePool;

    /* New request id and RequestState stroe in mReqStatePool.
       Will be clear once the request has been completed */
    std::map<int, RequestState_t*> mRequestsInTransit;
    typedef std::pair<int, RequestState_t*> RequestsInTransitPair;
    unsigned int mPartialResultCount;
    /*!> Sorted List of request id's that have metadata ready for return.
         The metadata for that request id should be present in the mRequestInTransit vector. */
    std::list<int> mRequestsPendingMetaReturn;
};

} /* namespace intel */
} /* namespace cros */

#endif //  _CAMERA3_HAL_RESULT_PROCESSOR_H_
