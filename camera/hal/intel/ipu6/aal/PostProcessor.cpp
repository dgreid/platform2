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

#define LOG_TAG "PostProcessor"

#include "PostProcessor.h"

#include "HALv3Utils.h"

namespace camera3 {

PostProcessor::PostProcessor(int cameraId, const camera3_stream_t& stream)
        : mCameraId(cameraId),
          mPostProcessType(icamera::POST_PROCESS_NONE),
          mPostProcessorCore(std::unique_ptr<icamera::PostProcessorCore>(
              new icamera::PostProcessorCore(cameraId))) {
    LOG1("@%s", __func__);
}

PostProcessor::~PostProcessor() {
    LOG1("@%s", __func__);
}

icamera::status_t PostProcessor::configure(const camera3_stream_t& stream,
                                           const camera3_stream_t& srcStream) {
    LOG1("@%s, stream: w:%d, h:%d, f:%d", __func__, stream.width, stream.height, stream.format);
    LOG1("@%s, srcStream: w:%d, h:%d, f:%d", __func__, srcStream.width, srcStream.height,
         srcStream.format);

    icamera::stream_t halStream;
    int ret = camera3::HalV3Utils::fillHALStreams(mCameraId, srcStream, &halStream);
    LOG1("@%s, halStream: w:%d, h:%d, f:%d, size:%d, stride:%d, ret:%d", __func__, halStream.width,
         halStream.height, halStream.format, halStream.size, halStream.stride, ret);
    CheckError(ret != icamera::OK, ret, "fillHALStreams fails, ret %d", ret);

    return configure(stream, halStream);
}

/* configure
 *
 * Decide post-processing is needed based on user stream and hal stream.
 * The default processing order is rotate -> crop -> scale -> convert -> encode.
 */
icamera::status_t PostProcessor::configure(const camera3_stream_t& stream,
                                           const icamera::stream_t& halStream) {
    LOG1("@%s, stream: w:%d, h:%d, f:%d", __func__, stream.width, stream.height, stream.format);
    LOG1("@%s, halStream: w:%d, h:%d, f:%d, size:%d, stride:%d", __func__, halStream.width,
         halStream.height, halStream.format, halStream.size, halStream.stride);

    icamera::PostProcessInfo info;
    mPostProcessType = icamera::POST_PROCESS_NONE;
    std::vector<icamera::PostProcessInfo> processingOrder;
    int angle = HalV3Utils::getRotationDegrees(stream);

    /* Fill the input/output information for the post processing unit.
     * The input info of processing unit is the output info of last unit.
     */
    icamera::stream_t inputStreamInfo = halStream;
    if (angle > 0 && mPostProcessorCore->isPostProcessTypeSupported(icamera::POST_PROCESS_ROTATE)) {
        mPostProcessType |= icamera::POST_PROCESS_ROTATE;
        info.angle = angle;
        info.type = icamera::POST_PROCESS_ROTATE;
        info.inputInfo = inputStreamInfo;
        info.outputInfo = inputStreamInfo;
        info.outputInfo.width = inputStreamInfo.height;
        info.outputInfo.height = inputStreamInfo.width;
        info.outputInfo.stride = inputStreamInfo.height;
        info.outputInfo.format = inputStreamInfo.format;
        info.outputInfo.size = icamera::CameraUtils::getFrameSize(
            info.outputInfo.format, info.outputInfo.width, info.outputInfo.height);
        LOG2("%s, Rotate: input %dx%d, output: %dx%d, angle: %d", __func__, inputStreamInfo.width,
             inputStreamInfo.height, info.outputInfo.width, info.outputInfo.height, angle);

        inputStreamInfo = info.outputInfo;
        processingOrder.push_back(info);
    }

    // Crop
    if (inputStreamInfo.width * stream.height != inputStreamInfo.height * stream.width &&
        mPostProcessorCore->isPostProcessTypeSupported(icamera::POST_PROCESS_CROP)) {
        mPostProcessType |= icamera::POST_PROCESS_CROP;
        info.type = icamera::POST_PROCESS_CROP;
        info.inputInfo = inputStreamInfo;

        // Caclulate the best crop size with same aspect ratio
        if (inputStreamInfo.width * stream.height < inputStreamInfo.height * stream.width) {
            info.outputInfo.width = info.inputInfo.width;
            info.outputInfo.height = ALIGN(info.inputInfo.width * stream.height / stream.width, 2);
        } else {
            info.outputInfo.width = ALIGN(info.inputInfo.height * stream.width / stream.height, 2);
            info.outputInfo.height = info.inputInfo.height;
        }
        info.outputInfo.format = inputStreamInfo.format;
        info.outputInfo.stride = info.outputInfo.width;
        info.outputInfo.size = icamera::CameraUtils::getFrameSize(
            info.outputInfo.format, info.outputInfo.width, info.outputInfo.height);
        LOG2("%s, Crop: input %dx%d, output: %dx%d", __func__, inputStreamInfo.width,
             inputStreamInfo.height, info.outputInfo.width, info.outputInfo.height);

        inputStreamInfo = info.outputInfo;
        processingOrder.push_back(info);
    }

    // Scale
    if ((uint32_t)inputStreamInfo.width * inputStreamInfo.height != stream.width * stream.height &&
        mPostProcessorCore->isPostProcessTypeSupported(icamera::POST_PROCESS_SCALING)) {
        mPostProcessType |= icamera::POST_PROCESS_SCALING;
        info.type = icamera::POST_PROCESS_SCALING;
        info.inputInfo = inputStreamInfo;
        info.outputInfo.width = stream.width;
        info.outputInfo.height = stream.height;
        info.outputInfo.stride = stream.width;
        info.outputInfo.format = inputStreamInfo.format;
        info.outputInfo.size = icamera::CameraUtils::getFrameSize(
            info.outputInfo.format, info.outputInfo.width, info.outputInfo.height);
        LOG2("%s, Scale: input %dx%d, output: %dx%d", __func__, inputStreamInfo.width,
             inputStreamInfo.height, info.outputInfo.width, info.outputInfo.height);

        inputStreamInfo = info.outputInfo;
        processingOrder.push_back(info);
    }

    // Convert
    if (inputStreamInfo.format !=
            HalV3Utils::HALFormatToV4l2Format(mCameraId, stream.format, stream.usage) &&
        mPostProcessorCore->isPostProcessTypeSupported(icamera::POST_PROCESS_CONVERT)) {
        mPostProcessType |= icamera::POST_PROCESS_CONVERT;
        info.type = icamera::POST_PROCESS_CONVERT;
        info.inputInfo = inputStreamInfo;
        info.outputInfo.width = stream.width;
        info.outputInfo.height = stream.height;
        info.outputInfo.stride = stream.width;
        info.outputInfo.format =
            HalV3Utils::HALFormatToV4l2Format(mCameraId, stream.format, stream.usage);
        info.outputInfo.size = icamera::CameraUtils::getFrameSize(
            info.outputInfo.format, info.outputInfo.width, info.outputInfo.height);
        LOG2("%s, Convert: input %dx%d, output: %dx%d", __func__, inputStreamInfo.width,
             inputStreamInfo.height, info.outputInfo.width, info.outputInfo.height);

        inputStreamInfo = info.outputInfo;
        processingOrder.push_back(info);
    }

    // Encode
    if (stream.format == HAL_PIXEL_FORMAT_BLOB &&
        mPostProcessorCore->isPostProcessTypeSupported(icamera::POST_PROCESS_JPEG_ENCODING)) {
        mPostProcessType |= icamera::POST_PROCESS_JPEG_ENCODING;
        info.type = icamera::POST_PROCESS_JPEG_ENCODING;
        info.inputInfo = inputStreamInfo;
        info.outputInfo.width = stream.width;
        info.outputInfo.height = stream.height;
        info.outputInfo.stride = stream.width;
        info.outputInfo.format =
            HalV3Utils::HALFormatToV4l2Format(mCameraId, stream.format, stream.usage);
        info.outputInfo.size = icamera::CameraUtils::getFrameSize(
            info.outputInfo.format, info.outputInfo.width, info.outputInfo.height);
        inputStreamInfo = info.outputInfo;
        processingOrder.push_back(info);
    }

    if ((uint32_t)inputStreamInfo.width != stream.width ||
        (uint32_t)inputStreamInfo.height != stream.height ||
        inputStreamInfo.format !=
            HalV3Utils::HALFormatToV4l2Format(mCameraId, stream.format, stream.usage)) {
        LOGE("%s, stream info doesn't match between input and output stream.", __func__);
        return icamera::UNKNOWN_ERROR;
    }
    LOG1("@%s, camera id %d, mPostProcessType %d, processing unit number: %zu", __func__, mCameraId,
         mPostProcessType, processingOrder.size());
    mPostProcessorCore->configure(processingOrder);

    return icamera::OK;
}

icamera::status_t PostProcessor::doPostProcessing(const std::shared_ptr<Camera3Buffer>& inBuf,
                                                  const icamera::Parameters& parameter,
                                                  std::shared_ptr<Camera3Buffer> outBuf) {
    return mPostProcessorCore->doPostProcessing(inBuf, parameter, outBuf);
}
}  // namespace camera3
