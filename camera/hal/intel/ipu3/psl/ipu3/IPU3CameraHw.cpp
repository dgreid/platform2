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

#define LOG_TAG "IPU3CameraHw"

#include <algorithm>
#include "IPU3CameraHw.h"
#include "LogHelper.h"
#include "CameraMetadataHelper.h"
#include "RequestThread.h"
#include "PlatformData.h"
#include "ImguUnit.h"
#include "ControlUnit.h"
#include "CaptureUnit.h"
#include "CameraProfiles.h"
#include "FaceEngine.h"

namespace cros {
namespace intel {

// Camera factory
ICameraHw *CreatePSLCamera(int cameraId) {
    return new IPU3CameraHw(cameraId);
}

IPU3CameraHw::IPU3CameraHw(int cameraId):
        mCameraId(cameraId),
        mStaticMeta(nullptr),
        mPipelineDepth(-1),
        mImguUnit(nullptr),
        mControlUnit(nullptr),
        mCaptureUnit(nullptr),
        mGCM(cameraId),
        mOperationMode(0),
        mTestPatternMode(ANDROID_SENSOR_TEST_PATTERN_MODE_OFF),
        mFDMode(FD_MODE_OFF),
        mMaxFaceNum(MAX_FACES_DETECTABLE)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
}

IPU3CameraHw::~IPU3CameraHw()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    deInit();
}

status_t
IPU3CameraHw::init()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    status_t status = NO_ERROR;

    status = initStaticMetadata();
    CheckError(status != NO_ERROR, status, "initStaticMetadata fails, status:%d", status);

    if (PlatformData::isFaceAeEnabled(mCameraId)) {
        mFDMode = FD_MODE_SIMPLE;
    }
    mFaceEngine = std::unique_ptr<FaceEngine>(new FaceEngine(mCameraId,
                                                             MAX_FACES_DETECTABLE,
                                                             MAX_FACE_FRAME_WIDTH,
                                                             MAX_FACE_FRAME_HEIGHT,
                                                             mFDMode));

    std::string sensorMediaDevice = CameraProfiles::getSensorMediaDevice();
    mMediaCtl = std::make_shared<MediaController>(sensorMediaDevice.c_str());
    status = mMediaCtl->init();
    if (status != NO_ERROR) {
        LOGE("Error initializing Media Controller");
        return status;
    }

    std::string imguMediaDevice = CameraProfiles::getImguMediaDevice();
    mImguMediaCtl = std::make_shared<MediaController>(imguMediaDevice.c_str());
    status = mImguMediaCtl->init();
    if (status != NO_ERROR) {
        LOGE("Error initializing Media Controller");
        return status;
    }

    mGCM.setMediaCtl(mMediaCtl);

    mCaptureUnit = new CaptureUnit(mCameraId, mGCM, mMediaCtl);
    status = mCaptureUnit->init();
    if (status != NO_ERROR) {
        LOGE("Error initializing CaptureUnit, ret code: %x", status);
        return status;
    }

    mImguUnit = new ImguUnit(mCameraId, mGCM, mImguMediaCtl, mFaceEngine.get());

    mControlUnit = new ControlUnit(mImguUnit,
                                   mCaptureUnit,
                                   mCameraId,
                                   mGCM, mFaceEngine.get());
    status = mControlUnit->init();
    if (status != NO_ERROR) {
        LOGE("Error initializing ControlUnit. ret code:%x", status);
        return status;
    }

    // Register ControlUnit as a listener to capture events
    status = mImguUnit->attachListener(mControlUnit);
    status |= mCaptureUnit->attachListener(mControlUnit);

    // Reset all links
    mMediaCtl->resetLinks();
    mImguMediaCtl->resetLinks();

    return status;
}

void
IPU3CameraHw::deInit()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    if (mImguUnit) {
        mImguUnit->cleanListener();
        mImguUnit->flush();
    }

    if (mCaptureUnit) {
        mCaptureUnit->cleanListeners();
        mCaptureUnit->flush();
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

    if (mCaptureUnit) {
        delete mCaptureUnit;
        mCaptureUnit = nullptr;
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

void
IPU3CameraHw::registerErrorCallback(IErrorCallback *errCb)
{
    LOG2("@%s, errCb:%p", __FUNCTION__, errCb);
    PlatformData::getIntel3AClient()->registerErrorCallback(errCb);
    mCaptureUnit->registerErrorCallback(errCb);
    mImguUnit->registerErrorCallback(errCb);
    mControlUnit->registerErrorCallback(errCb);
}

const camera_metadata_t *
IPU3CameraHw::getDefaultRequestSettings(int type)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    return PlatformData::getDefaultMetadata(mCameraId, type);
}

status_t IPU3CameraHw::checkStreamSizes(std::vector<camera3_stream_t*> &activeStreams)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

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

status_t IPU3CameraHw::checkStreamRotation(const std::vector<camera3_stream_t*> activeStreams)
{
    int s0Degree = CAMERA3_STREAM_ROTATION_0;
    for (size_t i = 0; i < activeStreams.size(); i++) {
        if (activeStreams[i]->stream_type != CAMERA3_STREAM_OUTPUT) {
            continue;
        }

        if (activeStreams[i]->crop_rotate_scale_degrees != CAMERA3_STREAM_ROTATION_0 &&
            activeStreams[i]->crop_rotate_scale_degrees != CAMERA3_STREAM_ROTATION_90 &&
            activeStreams[i]->crop_rotate_scale_degrees != CAMERA3_STREAM_ROTATION_270) {
            LOGE("@%s, Invalid rotation value %d",
            __FUNCTION__, activeStreams[i]->crop_rotate_scale_degrees);
            return BAD_VALUE;
        }

        if (!i) {
            s0Degree = activeStreams[i]->crop_rotate_scale_degrees;
        } else {
            if (s0Degree != activeStreams[i]->crop_rotate_scale_degrees) {
                LOGE("@%s, s0Degree:%d, stream[%lu] degree:%d, not the same",
                __FUNCTION__, s0Degree, i, activeStreams[i]->crop_rotate_scale_degrees);
                return BAD_VALUE;
            }
        }
    }

    return OK;
}

static bool compFun(camera3_stream_t* s1, camera3_stream_t* s2)
{
    // Use streamSizeGT since compare function requires strictly weak order.
    return streamSizeGT(s1, s2);
}

// 1.sort the steams by resolution from big to small.
// 2.if it has HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED in one of the output
// streams, the stream must be one of the first two streams, because the
// HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED is thought to be output for all request.
void IPU3CameraHw::sortActiveStreams(std::vector<camera3_stream_t*> &activeStreams)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    std::sort(activeStreams.begin(), activeStreams.end(), compFun);

    const int maxHWSupportStreamNum = 2;

    if ((activeStreams.size() <= maxHWSupportStreamNum) ||
        (activeStreams[0]->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) ||
        (activeStreams[1]->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED)) {
            return;
    }
}

status_t
IPU3CameraHw::configStreams(std::vector<camera3_stream_t*> &activeStreams,
                            uint32_t operation_mode)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    uint32_t maxBufs, usage;

    if (checkStreamSizes(activeStreams) != OK)
        return BAD_VALUE;

    if (checkStreamRotation(activeStreams) != OK)
        return BAD_VALUE;

    mOperationMode = operation_mode;

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

    mActiveStreams.clear();
    for (unsigned int i = 0; i < activeStreams.size(); i++) {
        activeStreams[i]->max_buffers = maxBufs;
        activeStreams[i]->usage |= usage;

        if (activeStreams[i]->stream_type == CAMERA3_STREAM_INPUT) {
            continue;
        }

        mActiveStreams.push_back(activeStreams[i]);
    }

    // let the mActiveStreams to be from big to small
    sortActiveStreams(mActiveStreams);

    return configStreamsPrivate();
}

status_t
IPU3CameraHw::processRequest(Camera3Request* request, int inFlightCount)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    if (inFlightCount > mPipelineDepth) {
        LOG2("@%s:blocking request %d", __FUNCTION__, request->getId());
        return RequestThread::REQBLK_WAIT_ONE_REQUEST_COMPLETED;
    }

    int32_t testPatternMode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
    status_t status = getTestPatternMode(request, &testPatternMode);
    CheckError(status != NO_ERROR, status, "@%s: failed to get test pattern mode", __FUNCTION__);

    if (testPatternMode != mTestPatternMode) {
        LOG1("%s: request %d need reconfigure, infilght %d", __FUNCTION__,
                request->getId(), inFlightCount);
        if (inFlightCount > 1) {
            return RequestThread::REQBLK_WAIT_ALL_PREVIOUS_COMPLETED;
        }

        mTestPatternMode = testPatternMode;
        status = configStreamsPrivate();
    }

    if (status == NO_ERROR) {
        mControlUnit->processRequest(request);
    }
    return status;
}

status_t IPU3CameraHw::getTestPatternMode(Camera3Request* request, int32_t* testPatternMode)
{
    const android::CameraMetadata *reqSetting = request->getSettings();
    CheckError(reqSetting == nullptr, UNKNOWN_ERROR, "no settings in request - BUG");

    const camera_metadata_t *meta = PlatformData::getStaticMetadata(mCameraId);
    camera_metadata_ro_entry entry, availableTestPatternModes;

    availableTestPatternModes = MetadataHelper::getMetadataEntry(meta,
        ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES);

    entry = reqSetting->find(ANDROID_SENSOR_TEST_PATTERN_MODE);
    MetadataHelper::getSetting(availableTestPatternModes, entry, testPatternMode);
    CheckError(*testPatternMode < 0, BAD_VALUE, "@%s: invalid test pattern mode: %d",
        __FUNCTION__, *testPatternMode);

    LOG2("@%s: current test pattern mode: %d", __FUNCTION__, *testPatternMode);
    return NO_ERROR;
}

status_t IPU3CameraHw::configStreamsPrivate()
{
    /* Flush to make sure we return all graph config objects to the pool before
       next stream config. */
    mCaptureUnit->flush();
    mImguUnit->flush();
    mControlUnit->flush();

    status_t status = mGCM.configStreams(mActiveStreams, mOperationMode, mTestPatternMode);
    if (status != NO_ERROR) {
        LOGE("Unable to configure stream: No matching graph config found! BUG");
        return status;
    }

    status = mCaptureUnit->configStreams(mActiveStreams, true);
    CheckError(status != NO_ERROR, status, "Unable to configure stream for CaptureUnit");

    status = mImguUnit->configStreams(mActiveStreams);
    CheckError(status != NO_ERROR, status, "Unable to configure stream for ImguUnit");

    return mControlUnit->configStreamsDone(true);
}

status_t
IPU3CameraHw::flush()
{
    return NO_ERROR;
}

void
IPU3CameraHw::dump(int fd)
{
    UNUSED(fd);
}

/**
 * initStaticMetadata
 *
 * Create android::CameraMetadata object to retrieve the static tags used in this class
 * we cache them as members so that we do not need to query android::CameraMetadata class
 * every time we need them. This is more efficient since find() is not cheap
 */
status_t
IPU3CameraHw::initStaticMetadata(void)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

    /**
    * Initialize the android::CameraMetadata object with the static metadata tags
    */
    camera_metadata_t* staticMeta = const_cast<camera_metadata_t*>(PlatformData::getStaticMetadata(mCameraId));
    mStaticMeta = new android::CameraMetadata(staticMeta);

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
            return NO_INIT;
    }
    return NO_ERROR;
}

}  // namespace intel
}  // namespace cros
