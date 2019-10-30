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

#include <memory>

#include "Camera3Buffer.h"
#include "Errors.h"
#include "ICamera.h"
#include "PostProcessorCore.h"
#include "Utils.h"

namespace camera3 {
/**
 * \class PostProcessor
 *
 * A wrapper based on PostProcessorCore for handling post-processing sequence,
 * there are two main purposes of this class.
 * 1. Provide the wrapper to implement post-processing feature.
 * 2. Parsing the processing type and formulate the processing sequence
 */
class PostProcessor {
 public:
    PostProcessor(int cameraId, const camera3_stream_t& stream);
    virtual ~PostProcessor();

    // srcStream will convert to stream_t and call the other configure
    icamera::status_t configure(const camera3_stream_t& stream, const camera3_stream_t& srcStream);
    icamera::status_t configure(const camera3_stream_t& stream, const icamera::stream_t& halStream);
    int getPostProcessType() { return mPostProcessType; }
    icamera::status_t doPostProcessing(const std::shared_ptr<Camera3Buffer>& inBuf,
                                       const icamera::Parameters& parameter,
                                       std::shared_ptr<Camera3Buffer> outBuf);

 private:
    DISALLOW_COPY_AND_ASSIGN(PostProcessor);

 private:
    int mCameraId;
    int mPostProcessType;
    std::unique_ptr<icamera::PostProcessorCore> mPostProcessorCore;
};

}  // namespace camera3
