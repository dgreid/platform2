/*
 * Copyright (C) 2014-2017 Intel Corporation
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
#ifndef _CAMERA3_HAL_ICAMERAHW_H_
#define _CAMERA3_HAL_ICAMERAHW_H_

#include <vector>
#include "hardware/camera3.h"
#include "Camera3Request.h"
#include "IErrorCallback.h"

namespace cros {
namespace intel {

class ICameraHw {
public:
    static ICameraHw * createCameraHW(int cameraId);

    virtual ~ICameraHw(){};

    virtual status_t init(void) = 0;

    /*
     * Register error callback function
     *
     * notify the message to framework when error happens in hal
     */
    virtual void registerErrorCallback(IErrorCallback *errCb) = 0;

    virtual const camera_metadata_t * getDefaultRequestSettings(int type) = 0;
    /*
      1. check if ISP mode need be changed or streams need be re-bound
      2. configure ISP and configure HW streams.
      3. if settings is not nullptr,
          check if additional streams need be bound
          set parameters
          send to AAAprocessor/....
    */
    virtual status_t processRequest(Camera3Request* request,
                                    int inFlightCount) = 0;
    virtual status_t flush() = 0;

    /**
     *  Configure the streams that framework expects :
     *  - gralloc usage flags
     *  - max buffers per stream
     */
    virtual status_t configStreams(std::vector<camera3_stream_t*> &activeStreams,
                                   uint32_t operation_mode = 0) = 0;

    /**
     * For debugging
     */
    virtual void dump(int fd) = 0;
};

} /* namespace intel */
} /* namespace cros */
#endif /* _CAMERA3_HAL_ICAMERAHW_H_ */
