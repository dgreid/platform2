/*
 * Copyright (C) 2014-2017 Intel Corporation
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
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

#define LOG_TAG "RKISP1CameraHw"

#include "RKISP1CameraHw.h"
#include "LogHelper.h"
#include "CameraMetadataHelper.h"
#include "RequestThread.h"
#include "HwStreamBase.h"
#include "PlatformData.h"
#include "ImguUnit.h"
#include "ControlUnit.h"
#include "PSLConfParser.h"

namespace android {
namespace camera2 {

static const uint8_t DEFAULT_PIPELINE_DEPTH = 4;
#define CONFIGURE_LARGE_SIZE (1920*1080)

// Camera factory
ICameraHw *CreatePSLCamera(int cameraId) {
    return new RKISP1CameraHw(cameraId);
}

RKISP1CameraHw::RKISP1CameraHw(int cameraId):
        mCameraId(cameraId),
        mStaticMeta(nullptr),
        mPipelineDepth(-1),
        mImguUnit(nullptr),
        mControlUnit(nullptr),
        mGCM(cameraId),
        mUseCase(USECASE_VIDEO),
        mOperationMode(0),
        mTestPatternMode(ANDROID_SENSOR_TEST_PATTERN_MODE_OFF)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
}

RKISP1CameraHw::~RKISP1CameraHw()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    deInit();
}

status_t
RKISP1CameraHw::init()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    status_t status = NO_ERROR;

    std::string sensorMediaDevice = PSLConfParser::getSensorMediaDevice();
    mMediaCtl = std::make_shared<MediaController>(sensorMediaDevice.c_str());
    status = mMediaCtl->init();
    if (status != NO_ERROR) {
        LOGE("Error initializing Media Controller");
        return status;
    }

    std::string imguMediaDevice = PSLConfParser::getImguMediaDevice();
    if (sensorMediaDevice == imguMediaDevice) {
        LOGI("Using sensor media device as imgu media device");
        mImguMediaCtl = mMediaCtl;
    } else {
        mImguMediaCtl = std::make_shared<MediaController>(imguMediaDevice.c_str());

        status = mImguMediaCtl->init();
        if (status != NO_ERROR) {
            LOGE("Error initializing Media Controller");
            return status;
        }
    }

    mGCM.setMediaCtl(mMediaCtl);

    mImguUnit = new ImguUnit(mCameraId, mGCM, mImguMediaCtl);

    mControlUnit = new ControlUnit(mImguUnit,
                                   mCameraId,
                                   mGCM,
                                   mMediaCtl);
    status = mControlUnit->init();
    if (status != NO_ERROR) {
        LOGE("Error initializing ControlUnit. ret code:%x", status);
        return status;
    }

    // Register ControlUnit as a listener to capture events
    status = mImguUnit->attachListener(mControlUnit);
    status = initStaticMetadata();
    if (status != NO_ERROR) {
        LOGE("Error call initStaticMetadata, status:%d", status);
        return status;
    }

    return status;
}

void
RKISP1CameraHw::deInit()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    if (mImguUnit) {
       mImguUnit->cleanListener();
       mImguUnit->flush();
    }

    if (mControlUnit) {
        mControlUnit->flush();
        delete mControlUnit;
        mControlUnit = nullptr;
    }

    if (mImguUnit) {
        delete mImguUnit;
        mImguUnit = nullptr;
    }

    if(mStaticMeta) {
        /**
         * The metadata buffer this object uses belongs to PlatformData
         * we release it before we destroy the object.
         */
        mStaticMeta->release();
        delete mStaticMeta;
        mStaticMeta = nullptr;
    }
}

const camera_metadata_t *
RKISP1CameraHw::getDefaultRequestSettings(int type)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    return PlatformData::getDefaultMetadata(mCameraId, type);
}

status_t RKISP1CameraHw::checkStreamSizes(std::vector<camera3_stream_t*> &activeStreams)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    int32_t count;

    const camera_metadata_t *meta = PlatformData::getStaticMetadata(mCameraId);
    if (!meta) {
        LOGE("Cannot get static metadata.");
        return BAD_VALUE;
    }

    int32_t *availStreamConfig = (int32_t*)MetadataHelper::getMetadataValues(meta,
                                 ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                                 TYPE_INT32,
                                 &count);

    if (availStreamConfig == nullptr) {
        LOGE("Cannot get stream configuration from static metadata.");
        return BAD_VALUE;
    }

    // check the stream sizes are reasonable. Note the array of integers in
    // availStreamConfig needs to be jumped by four, W & H are at +1 & +2
    for (uint32_t i = 0; i < activeStreams.size(); i++) {
        bool matchFound = false;
        for (uint32_t j = 0; j < (uint32_t)count; j += 4) {
            if ((activeStreams[i]->width  == (uint32_t)availStreamConfig[j + 1]) &&
                (activeStreams[i]->height == (uint32_t)availStreamConfig[j + 2])) {
                matchFound = true;
                break;
            }
        }
        if (!matchFound) {
            LOGE("Camera stream config had unsupported dimension %dx%d.",
                 activeStreams[i]->width,
                 activeStreams[i]->height);
            return BAD_VALUE;
        }
    }

    return OK;
}

status_t RKISP1CameraHw::checkStreamRotation(const std::vector<camera3_stream_t*> activeStreams)
{
    int stream_crop_rotate_scale_degrees = -1;

    for (const auto* stream : activeStreams) {
        if (stream->stream_type != CAMERA3_STREAM_OUTPUT)
            continue;

        if (stream->crop_rotate_scale_degrees != CAMERA3_STREAM_ROTATION_0
            && stream->crop_rotate_scale_degrees != CAMERA3_STREAM_ROTATION_90
            && stream->crop_rotate_scale_degrees != CAMERA3_STREAM_ROTATION_270) {
            LOGE("Invalid rotation value %d", stream->crop_rotate_scale_degrees);
            return BAD_VALUE;
        }

        if ((stream_crop_rotate_scale_degrees != -1) &&
           (stream->crop_rotate_scale_degrees != stream_crop_rotate_scale_degrees))
            return BAD_VALUE;

        stream_crop_rotate_scale_degrees = stream->crop_rotate_scale_degrees;
    }

    return OK;
}

camera3_stream_t*
RKISP1CameraHw::findStreamForStillCapture(const std::vector<camera3_stream_t*>& streams)
{
    camera3_stream_t* jpegStream = nullptr;
    std::vector<camera3_stream_t*> yuvStreams;
    for (auto* s : streams) {
        if (s->width * s->height > CONFIGURE_LARGE_SIZE) {
            return s;
        }

        if (s->format == HAL_PIXEL_FORMAT_BLOB) {
            jpegStream = s;
        } else if (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED
            || s->format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
            yuvStreams.push_back(s);
        }
    }

    if (jpegStream && yuvStreams.size() >= 2) {
        // Check if it is video snapshot case
        int jpegSize = jpegStream->width * jpegStream->height;
        if (jpegSize == yuvStreams[0]->width * yuvStreams[0]->height
            || jpegSize == yuvStreams[1]->width * yuvStreams[1]->height)
            return nullptr;
    }
    return jpegStream;
}

status_t
RKISP1CameraHw::configStreams(std::vector<camera3_stream_t*> &activeStreams,
                            uint32_t operation_mode)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    uint32_t maxBufs, usage;
    status_t status = NO_ERROR;
    bool configChanged = true;

    mOperationMode = operation_mode;
    mStreamsStill.clear();
    mStreamsVideo.clear();

    if (checkStreamSizes(activeStreams) != OK)
        return BAD_VALUE;

    if (checkStreamRotation(activeStreams) != OK)
        return BAD_VALUE;

    maxBufs = mPipelineDepth;  /* value from XML static metadata */
    if (maxBufs > MAX_REQUEST_IN_PROCESS_NUM)
        maxBufs = MAX_REQUEST_IN_PROCESS_NUM;

    /**
     * Here we could give different gralloc flags depending on the stream
     * format, at the moment we give the same to all
     */
    usage = GRALLOC_USAGE_SW_READ_OFTEN |
            GRALLOC_USAGE_SW_WRITE_NEVER |
            GRALLOC_USAGE_HW_CAMERA_WRITE;

    camera3_stream_t* stillStream = findStreamForStillCapture(activeStreams);

    for (unsigned int i = 0; i < activeStreams.size(); i++) {
        activeStreams[i]->max_buffers = maxBufs;
        activeStreams[i]->usage |= usage;

        if (activeStreams[i] != stillStream) {
            mStreamsVideo.push_back(activeStreams[i]);
            mStreamsStill.push_back(activeStreams[i]);
        } else if (stillStream) {
            // always insert BLOB as fisrt stream if exists
            LOG1("%s: find still stream %dx%d, 0x%x", __FUNCTION__, stillStream->width,
                  stillStream->height, stillStream->format);
            mStreamsStill.insert(mStreamsStill.begin(), stillStream);
        }
    }

    /* Flush to make sure we return all graph config objects to the pool before
       next stream config. */
    mImguUnit->flush();
    mControlUnit->flush();

    mUseCase = USECASE_VIDEO; // Configure video pipe by default
    if (mStreamsVideo.empty()) {
        mUseCase = USECASE_STILL;
    }

    //rk needn't reconfigstream for still case, we just use USECASE_VIDEO always.
    mStreamsVideo = mStreamsStill;

    LOG1("%s: select usecase %d, video/still stream num: %zu/%zu", __FUNCTION__,
            mUseCase, mStreamsVideo.size(), mStreamsStill.size());

    std::vector<camera3_stream_t*> &configuredStreams =
            (mUseCase == USECASE_STILL) ? mStreamsStill : mStreamsVideo;

    status = mGCM.configStreams(configuredStreams, operation_mode, mTestPatternMode);
    if (status != NO_ERROR) {
        LOGE("Unable to configure stream: No matching graph config found! BUG");
        return status;
    }

    status = mImguUnit->configStreams(configuredStreams);
    if (status != NO_ERROR) {
        LOGE("Unable to configure stream for ImguUnit");
        return status;
    }

    status = mControlUnit->configStreamsDone(configChanged);

    return status;
}

status_t
RKISP1CameraHw::bindStreams(std::vector<CameraStreamNode *> activeStreams)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    status_t status = NO_ERROR;

    mDummyHwStreamsVector.clear();
    // Need to have a producer stream as it is required by common code
    // bind all streams to dummy HW stream class.
    for (unsigned int i = 0; i < activeStreams.size(); i++) {
        CameraStreamNode *stream = activeStreams.at(i);
        HwStreamBase *hwStream = new HwStreamBase(*stream);
        CameraStream::bind(stream, hwStream);
        mDummyHwStreamsVector.push_back(hwStream);
    }

    return status;
}

status_t
RKISP1CameraHw::processRequest(Camera3Request* request, int inFlightCount)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    if (inFlightCount > mPipelineDepth) {
        LOG2("@%s:blocking request %d", __FUNCTION__, request->getId());
        return RequestThread::REQBLK_WAIT_ONE_REQUEST_COMPLETED;
    }

    status_t status = NO_ERROR;
    // Check reconfiguration
    UseCase newUseCase = checkUseCase(request);
    int32_t testPatternMode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
    status = getTestPatternMode(request, &testPatternMode);
    CheckAndLogError(status != NO_ERROR, status,
                     "@%s: failed to get test pattern mode", __FUNCTION__);

    if (newUseCase != mUseCase || testPatternMode != mTestPatternMode) {
        LOG1("%s: request %d need reconfigure, infilght %d, usecase %d -> %d", __FUNCTION__,
                request->getId(), inFlightCount, mUseCase, newUseCase);
        if (inFlightCount > 1) {
            return RequestThread::REQBLK_WAIT_ALL_PREVIOUS_COMPLETED;
        }

        status = reconfigureStreams(newUseCase, mOperationMode, testPatternMode);
    }

    if (status == NO_ERROR) {
        mControlUnit->processRequest(request,
                                     mGCM.getGraphConfig(*request));
    }
    return status;
}

RKISP1CameraHw::UseCase RKISP1CameraHw::checkUseCase(Camera3Request* request) const
{
    if (mStreamsStill.size() == mStreamsVideo.size()) {
        return USECASE_VIDEO;
    }
    const std::vector<camera3_stream_buffer>* buffers = request->getOutputBuffers();
    for (const auto & buf : *buffers) {
        if (buf.stream == mStreamsStill[0]) {
            return USECASE_STILL;
        }
    }
    return USECASE_VIDEO;
}

status_t RKISP1CameraHw::getTestPatternMode(Camera3Request* request, int32_t* testPatternMode)
{
    const CameraMetadata *reqSetting = request->getSettings();
    CheckAndLogError(reqSetting == nullptr, UNKNOWN_ERROR,
                     "no settings in request - BUG");

    const camera_metadata_t *meta = PlatformData::getStaticMetadata(mCameraId);
    camera_metadata_ro_entry entry, availableTestPatternModes;

    availableTestPatternModes = MetadataHelper::getMetadataEntry(meta,
        ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES);

    entry = reqSetting->find(ANDROID_SENSOR_TEST_PATTERN_MODE);
    MetadataHelper::getSetting(availableTestPatternModes, entry, testPatternMode);
    CheckAndLogError(*testPatternMode < 0, BAD_VALUE,
                     "@%s: invalid test pattern mode: %d", __FUNCTION__,
                     *testPatternMode);

    LOG2("@%s: current test pattern mode: %d", __FUNCTION__, *testPatternMode);
    return NO_ERROR;
}

status_t RKISP1CameraHw::reconfigureStreams(UseCase newUseCase,
                                  uint32_t operation_mode, int32_t testPatternMode)
{
    mUseCase = newUseCase;
    mTestPatternMode = testPatternMode;
    std::vector<camera3_stream_t*>& streams = (mUseCase == USECASE_STILL) ?
                                              mStreamsStill : mStreamsVideo;

    /* Flush to make sure we return all graph config objects to the pool before
       next stream config. */
    mImguUnit->flush();
    mControlUnit->flush();

    status_t status = mGCM.configStreams(streams, operation_mode, testPatternMode);
    if (status != NO_ERROR) {
        LOGE("Unable to configure stream: No matching graph config found! BUG");
        return status;
    }

    status = mImguUnit->configStreams(streams);
    if (status != NO_ERROR) {
        LOGE("Unable to configure stream for ImguUnit");
        return status;
    }

    return mControlUnit->configStreamsDone(true);
}

status_t
RKISP1CameraHw::flush()
{
    return NO_ERROR;
}

void
RKISP1CameraHw::registerErrorCallback(IErrorCallback* errCb)
{
    if (mImguUnit)
        mImguUnit->registerErrorCallback(errCb);
}

void
RKISP1CameraHw::dump(int fd)
{
    UNUSED(fd);
}

/**
 * initStaticMetadata
 *
 * Create CameraMetadata object to retrieve the static tags used in this class
 * we cache them as members so that we do not need to query CameraMetadata class
 * every time we need them. This is more efficient since find() is not cheap
 */
status_t
RKISP1CameraHw::initStaticMetadata(void)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    status_t status = NO_ERROR;
    /**
    * Initialize the CameraMetadata object with the static metadata tags
    */
    camera_metadata_t* staticMeta;
    staticMeta = (camera_metadata_t*)PlatformData::getStaticMetadata(mCameraId);
    mStaticMeta = new CameraMetadata(staticMeta);

    camera_metadata_entry entry;
    entry = mStaticMeta->find(ANDROID_REQUEST_PIPELINE_MAX_DEPTH);
    if(entry.count == 1) {
        mPipelineDepth = entry.data.u8[0];
    } else {
        mPipelineDepth = DEFAULT_PIPELINE_DEPTH;
    }

    /**
     * Check the consistency of the information we had in XML file.
     * partial result count is very tightly linked to the PSL
     * implementation
     */
    int32_t xmlPartialCount = PlatformData::getPartialMetadataCount(mCameraId);
    if (xmlPartialCount != PARTIAL_RESULT_COUNT) {
            LOGW("Partial result count does not match current implementation "
                 " got %d should be %d, fix the XML!", xmlPartialCount,
                 PARTIAL_RESULT_COUNT);
            status = NO_INIT;
    }
    return status;
}

}  // namespace camera2
}  // namespace android
