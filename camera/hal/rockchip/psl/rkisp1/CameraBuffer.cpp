/*
 * Copyright (C) 2013-2017 Intel Corporation
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

#define LOG_TAG "CameraBuffer"
#include "LogHelper.h"
#include <sys/mman.h>
#include "linux/videodev2.h"
#include "PlatformData.h"
#include "CameraBuffer.h"
#include "CameraStream.h"
#include "Camera3GFXFormat.h"
#include "Camera3V4l2Format.h"
#include <unistd.h>
#include <sync/sync.h>

#include <sys/types.h>
#include <dirent.h>
#include <algorithm>
#include <libyuv.h>

NAMESPACE_DECLARATION {
extern int32_t gDumpInterval;
extern int32_t gDumpCount;

static bool SupportedFormat(int fmt) {
    if (fmt == V4L2_PIX_FMT_NV12 ||
        fmt == V4L2_PIX_FMT_NV12M ||
        fmt == V4L2_PIX_FMT_NV21 ||
        fmt == V4L2_PIX_FMT_NV21M ||
        fmt == V4L2_META_FMT_RK_ISP1_PARAMS ||
        fmt == V4L2_META_FMT_RK_ISP1_STAT_3A ||
        fmt == V4L2_PIX_FMT_JPEG) {  // Used for JPEG Encoder.
        return true;
    }
    return false;
}

////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
////////////////////////////////////////////////////////////////////

/**
 * CameraBuffer
 *
 * Default constructor
 * This constructor is used when we pre-allocate the CameraBuffer object
 * The initialization will be done as a second stage wit the method
 * init(), where we initialize the wrapper with the gralloc buffer provided by
 * the framework
 */
CameraBuffer::CameraBuffer() :  mWidth(0),
                                mHeight(0),
                                mSize(0),
                                mSizeY(0),
                                mSizeUV(0),
                                mFormat(0),
                                mV4L2Fmt(0),
                                mStride(0),
                                mUsage(0),
                                mInit(false),
                                mLocked(false),
                                mRegistered(false),
                                mType(BUF_TYPE_HANDLE),
                                mGbmBufferManager(nullptr),
                                mHandle(nullptr),
                                mHandlePtr(nullptr),
                                mOwner(nullptr),
                                mDataPtr(nullptr),
                                mDataPtrUV(nullptr),
                                mRequestID(0),
                                mCameraId(0),
                                mNonContiguousYandUV(false)
{
    LOG1("%s default constructor for buf %p", __FUNCTION__, this);
    CLEAR(mUserBuffer);
    CLEAR(mTimestamp);
    mUserBuffer.release_fence = -1;
    mUserBuffer.acquire_fence = -1;
}

/**
 * CameraBuffer
 *
 * Constructor for buffers allocated using MemoryUtils::allocateHeapBuffer
 *
 * \param w [IN] width
 * \param h [IN] height
 * \param s [IN] stride
 * \param v4l2fmt [IN] V4l2 format
 * \param usrPtr [IN] Data pointer
 * \param cameraId [IN] id of camera being used
 * \param dataSizeOverride [IN] buffer size input. Default is 0 and frameSize()
                                is used in that case.
 */
CameraBuffer::CameraBuffer(int w,
                           int h,
                           int s,
                           int v4l2fmt,
                           void* usrPtr,
                           int cameraId,
                           int dataSizeOverride):
        mWidth(w),
        mHeight(h),
        mSize(0),
        mSizeY(0),
        mSizeUV(0),
        mFormat(0),
        mV4L2Fmt(v4l2fmt),
        mStride(s),
        mUsage(0),
        mInit(false),
        mLocked(true),
        mRegistered(false),
        mType(BUF_TYPE_MALLOC),
        mGbmBufferManager(nullptr),
        mHandle(nullptr),
        mHandlePtr(nullptr),
        mOwner(nullptr),
        mDataPtr(nullptr),
        mDataPtrUV(nullptr),
        mRequestID(0),
        mCameraId(cameraId)
{
    LOG1("%s create malloc camera buffer %p", __FUNCTION__, this);
    if (usrPtr == nullptr) {
        LOGE("Tried to initialize a buffer with nullptr ptr!!");
        return;
    }

    mInit = true;
    mDataPtr = usrPtr;
    mSize = dataSizeOverride ? dataSizeOverride : frameSize(mV4L2Fmt, mStride, mHeight);
    mFormat = v4L2Fmt2GFXFmt(v4l2fmt);

    mDataPtrUV = static_cast<uint8_t*>(usrPtr) + mHeight * mStride;
    mSizeY = mHeight * mStride;
    mSizeUV = mHeight * mStride / 2;

    mNonContiguousYandUV = false;
    if (v4l2fmt == V4L2_PIX_FMT_NV12M || v4l2fmt == V4L2_PIX_FMT_NV21M) {
        mNonContiguousYandUV = true;
    }

    CLEAR(mUserBuffer);
    CLEAR(mTimestamp);
    mUserBuffer.release_fence = -1;
    mUserBuffer.acquire_fence = -1;
}

/**
 * CameraBuffer
 *
 * Constructor for buffers allocated using mmap with two non-contiguous planes.
 *
 * \param w [IN] width
 * \param h [IN] height
 * \param s [IN] stride
 * \param fd [IN] File descriptor to map
 * \param length [vector<int>] amount of data to map for Y and UV plane
 * \param v4l2fmt [IN] Pixel format in V4L2 enum
 * \param offset [vector<int>] offsetY from the beginning of the file (mmap param) for Y and UV
 * \param prot [IN] memory protection (mmap param)
 * \param flags [IN] flags (mmap param)
 *
 * Success of the mmap can be queried by checking the size of the resulting
 * buffer
 */
CameraBuffer::CameraBuffer(int w, int h, int s, int fd, int v4l2fmt,
                           std::vector<int> length, std::vector<int> offset,
                           int prot, int flags):
        mWidth(w),
        mHeight(h),
        mSize(0),
        mSizeY(0),
        mSizeUV(0),
        mFormat(0),
        mV4L2Fmt(v4l2fmt),
        mStride(s),
        mUsage(0),
        mInit(false),
        mLocked(false),
        mRegistered(false),
        mType(BUF_TYPE_MMAP),
        mGbmBufferManager(nullptr),
        mHandle(nullptr),
        mHandlePtr(nullptr),
        mOwner(nullptr),
        mDataPtr(nullptr),
        mDataPtrUV(nullptr),
        mRequestID(0),
        mCameraId(-1),
        mNonContiguousYandUV(false)
{
    LOG1("%s create mmap camera buffer %p", __FUNCTION__, this);

    mLocked = true;
    mInit = true;
    CLEAR(mUserBuffer);
    CLEAR(mTimestamp);
    CLEAR(mHandle);
    mUserBuffer.release_fence = -1;
    mUserBuffer.acquire_fence = -1;

    mDataPtr = mmap(nullptr, length[0], prot, flags, fd, offset[0]);
    if (CC_UNLIKELY(mDataPtr == MAP_FAILED)) {
        LOGE("Failed to MMAP the buffer %s", strerror(errno));
        mDataPtr = nullptr;
        return;
    }
    mSizeY = length[0];
    LOG1("mmaped Y address for %p length %d", mDataPtr, mSizeY);

    if (length.size() == 1) {
        mDataPtrUV = static_cast<uint8_t*>(mDataPtr) + mHeight * mStride;
        mSize = length[0];
        mSizeUV = mHeight * mStride / 2;
        mNonContiguousYandUV = false;
    } else {
        mDataPtrUV = mmap(nullptr, length[1], prot, flags, fd, offset[1]);
        if (CC_UNLIKELY(mDataPtrUV == MAP_FAILED)) {
            LOGE("Failed to MMAP the buffer %s", strerror(errno));
            mDataPtrUV = nullptr;
            munmap(mDataPtr, length[0]);
            mDataPtr = nullptr;
            return;
        }
        mSize = length[0] + length[1];
        mSizeUV = length[1];
        mNonContiguousYandUV = true;
        LOG1("mmaped UV address for %p length %d", mDataPtrUV, mSizeUV);
    }
}

/**
 * init
 *
 * Constructor to wrap a camera3_stream_buffer
 *
 * \param aBuffer [IN] camera3_stream_buffer buffer
 */
status_t CameraBuffer::init(const camera3_stream_buffer *aBuffer, int cameraId)
{
    mHandle = *aBuffer->buffer;
    mV4L2Fmt = mGbmBufferManager->GetV4L2PixelFormat(mHandle);
    if (!SupportedFormat(mV4L2Fmt)) {
        LOGE("Failed to init unsupported handle camera buffer with format %s",
             v4l2Fmt2Str(mV4L2Fmt));
        return BAD_VALUE;
    }

    mType = BUF_TYPE_HANDLE;
    mGbmBufferManager = cros::CameraBufferManager::GetInstance();
    mHandlePtr = aBuffer->buffer;
    mWidth = aBuffer->stream->width;
    mHeight = aBuffer->stream->height;
    mFormat = aBuffer->stream->format;
    // Use actual width from platform native handle for stride
    mStride = mGbmBufferManager->GetPlaneStride(*aBuffer->buffer, 0);
    mNonContiguousYandUV = (mGbmBufferManager->GetNumPlanes(mHandle) > 1);
    mSize = 0;
    mSizeY = 0,
    mSizeUV = 0;
    mLocked = false;
    mOwner = static_cast<CameraStream*>(aBuffer->stream->priv);
    mUsage = mOwner->usage();
    mInit = true;
    mDataPtr = nullptr;
    mDataPtrUV = nullptr;
    mUserBuffer = *aBuffer;
    mUserBuffer.release_fence = -1;
    mCameraId = cameraId;
    LOG2("@%s, mHandle:%p, mFormat:%d, mWidth:%d, mHeight:%d, mStride:%d",
        __FUNCTION__, mHandle, mFormat, mWidth, mHeight, mStride);

    if (mHandle == nullptr) {
        LOGE("@%s: invalid buffer handle", __FUNCTION__);
        mUserBuffer.status = CAMERA3_BUFFER_STATUS_ERROR;
        return BAD_VALUE;
    }

    int ret = registerBuffer();
    if (ret) {
        mUserBuffer.status = CAMERA3_BUFFER_STATUS_ERROR;
        return UNKNOWN_ERROR;
    }

    /* TODO: add some consistency checks here and return an error */
    return NO_ERROR;
}

status_t CameraBuffer::init(const camera3_stream_t* stream,
                            buffer_handle_t handle,
                            int cameraId)
{
    mV4L2Fmt = mGbmBufferManager->GetV4L2PixelFormat(handle);
    if (!SupportedFormat(mV4L2Fmt)) {
        LOGE("Failed to init unsupported handle camera buffer with format %s",
             v4l2Fmt2Str(mV4L2Fmt));
        return BAD_VALUE;
    }

    mType = BUF_TYPE_HANDLE;
    mGbmBufferManager = cros::CameraBufferManager::GetInstance();
    mHandle = handle;
    mWidth = stream->width;
    mHeight = stream->height;
    mFormat = stream->format;
    // Use actual width from platform native handle for stride
    mStride = mGbmBufferManager->GetPlaneStride(handle, 0);
    mNonContiguousYandUV = (mGbmBufferManager->GetNumPlanes(mHandle) > 1);
    mSize = 0;
    mSizeY = 0;
    mSizeUV = 0;
    mLocked = false;
    mOwner = nullptr;
    mUsage = stream->usage;
    mInit = true;
    mDataPtr = nullptr;
    mDataPtrUV = nullptr;
    CLEAR(mUserBuffer);
    mUserBuffer.acquire_fence = -1;
    mUserBuffer.release_fence = -1;
    mCameraId = cameraId;
    LOG2("@%s, mHandle:%p, mFormat:%d, mWidth:%d, mHeight:%d, mStride:%d",
        __FUNCTION__, mHandle, mFormat, mWidth, mHeight, mStride);

    return NO_ERROR;
}

status_t CameraBuffer::deinit()
{
    return deregisterBuffer();
}

CameraBuffer::~CameraBuffer()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    if (mInit) {
        switch(mType) {
        case BUF_TYPE_MALLOC:
            free(mDataPtr);
            mDataPtr = nullptr;
            break;
        case BUF_TYPE_MMAP:
            if (mDataPtr != nullptr)
                munmap(mDataPtr, mSizeY);
            if (mDataPtrUV != nullptr)
                munmap(mDataPtrUV, mSizeUV);
            break;
        case BUF_TYPE_HANDLE:
            // Allocated by the HAL
            if (!(mUserBuffer.stream)) {
                LOG1("release internal buffer");
                mGbmBufferManager->Free(mHandle);
            }
            break;
        default:
            break;
        }
    }
    LOG1("%s destroying buf %p", __FUNCTION__, this);
}

status_t CameraBuffer::waitOnAcquireFence()
{
    const int WAIT_TIME_OUT_MS = 300;
    const int BUFFER_READY = -1;

    if (mUserBuffer.acquire_fence != BUFFER_READY) {
        LOG2("%s: Fence in HAL is %d", __FUNCTION__, mUserBuffer.acquire_fence);
        int ret = sync_wait(mUserBuffer.acquire_fence, WAIT_TIME_OUT_MS);
        if (ret) {
            mUserBuffer.release_fence = mUserBuffer.acquire_fence;
            mUserBuffer.acquire_fence = -1;
            mUserBuffer.status = CAMERA3_BUFFER_STATUS_ERROR;
            LOGE("Buffer sync_wait fail!");
            return TIMED_OUT;
        } else {
            close(mUserBuffer.acquire_fence);
        }
        mUserBuffer.acquire_fence = BUFFER_READY;
    }

    return NO_ERROR;
}

/**
 * getFence
 *
 * return the fecne to request result
 */
status_t CameraBuffer::getFence(camera3_stream_buffer* buf)
{
    if (!buf)
        return BAD_VALUE;

    buf->acquire_fence = mUserBuffer.acquire_fence;
    buf->release_fence = mUserBuffer.release_fence;

    return NO_ERROR;
}

int CameraBuffer::dmaBufFd(int plane)
{
    if (mType != BUF_TYPE_HANDLE) {
        LOGE("@%s: Try to get dmaBuffer for plane %d > 0 from a non HANDLE type buffer:", __FUNCTION__, plane);
        return -1;
    }

    if (0 > plane || mHandle->numFds <= plane) {
        LOGE("@%s: Invalide plane number, mHandle:%p, plane:%d", __FUNCTION__, mHandle, plane);
        return mHandle->data[0];
    }

    return mHandle->data[plane];
}

int CameraBuffer::dmaBufFdOffset(int plane)
{
    if (mType != BUF_TYPE_HANDLE) {
            LOGE("@%s: Try to get offset for plane %d > 0 from a non HANDLE type buffer:", __FUNCTION__, plane);
        return 0;
    }
    int ret = mGbmBufferManager->GetPlaneOffset(mHandle, plane);
    if (ret < 0) {
        LOGE("@%s: failed to get offset for plane, mHandle:%p, plane:%d", __FUNCTION__, mHandle, plane);
        return 0;
    }
    return ret;
}

status_t CameraBuffer::registerBuffer()
{
    int ret = mGbmBufferManager->Register(mHandle);
    if (ret) {
        LOGE("@%s: call Register fail, mHandle:%p, ret:%d", __FUNCTION__, mHandle, ret);
        return UNKNOWN_ERROR;
    }

    mRegistered = true;
    return NO_ERROR;
}

status_t CameraBuffer::deregisterBuffer()
{
    if (mRegistered) {
        int ret = mGbmBufferManager->Deregister(mHandle);
        if (ret) {
            LOGE("@%s: call Deregister fail, mHandle:%p, ret:%d", __FUNCTION__, mHandle, ret);
            return UNKNOWN_ERROR;
        }
        mRegistered = false;
    }

    return NO_ERROR;
}

/**
 * lock
 *
 * lock the gralloc buffer with specified flags
 *
 * \param aBuffer [IN] int flags
 */
status_t CameraBuffer::lock(int flags)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    mDataPtr = nullptr;
    mDataPtrUV = nullptr;

    mSize = 0;
    mSizeY = 0;
    mSizeUV = 0;
    int ret = 0;
    uint32_t planeNum = mGbmBufferManager->GetNumPlanes(mHandle);
    LOG2("@%s, planeNum:%d, mHandle:%p, mFormat:%d", __FUNCTION__, planeNum, mHandle, mFormat);

    if (planeNum == 1) {
        void* data = nullptr;
        ret = (mFormat == HAL_PIXEL_FORMAT_BLOB)
                ? mGbmBufferManager->Lock(mHandle, 0, 0, 0, mStride, 1, &data)
                : mGbmBufferManager->Lock(mHandle, 0, 0, 0, mWidth, mHeight, &data);
        if (ret) {
            LOGE("@%s: call Lock fail, mHandle:%p", __FUNCTION__, mHandle);
            return UNKNOWN_ERROR;
        }
        mSize = mGbmBufferManager->GetPlaneSize(mHandle, 0);
        mDataPtr = data;
    } else if (planeNum == 2) {
        struct android_ycbcr ycbrData;
        ret = mGbmBufferManager->LockYCbCr(mHandle, 0, 0, 0, mWidth, mHeight, &ycbrData);
        if (ret) {
            LOGE("@%s: call LockYCbCr fail, mHandle:%p", __FUNCTION__, mHandle);
            return UNKNOWN_ERROR;
        }
        mDataPtr = ycbrData.y;
        mDataPtrUV = ycbrData.cb;
        mSizeY = mGbmBufferManager->GetPlaneSize(mHandle, 0);
        mSizeUV = mGbmBufferManager->GetPlaneSize(mHandle, 1);
        mSize = mSizeY + mSizeUV;
    } else {
        LOGE("ERROR @%s: Invalid planeNum %d", __FUNCTION__, planeNum);
        return UNKNOWN_ERROR;
    }
    if (ret) {
        LOGE("ERROR @%s: Failed to lock buffer! %d", __FUNCTION__, ret);
        return UNKNOWN_ERROR;
    }

    LOG2("@%s, mDataPtr:%p, mSize:%d", __FUNCTION__, mDataPtr, mSize);
    if (!mSize) {
        LOGE("ERROR @%s: Failed to GetPlaneSize, it's 0", __FUNCTION__);
        return UNKNOWN_ERROR;
    }

    mLocked = true;

    return NO_ERROR;
}

status_t CameraBuffer::lock()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    status_t status;
    int lockMode;

    if (!mInit) {
        LOGE("@%s: Error: Cannot lock now this buffer, not initialized", __FUNCTION__);
        return INVALID_OPERATION;
    }

    if (mType != BUF_TYPE_HANDLE) {
         mLocked = true;
         return NO_ERROR;
    }

    if (mLocked) {
        LOGE("@%s: Error: Cannot lock buffer from stream(%d), already locked",
             __FUNCTION__,mOwner->seqNo());
        return INVALID_OPERATION;
    }

    lockMode = mUsage & (GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK |
        GRALLOC_USAGE_HW_CAMERA_MASK);
    if (!lockMode) {
        LOGW("@%s:trying to lock a buffer with no flags", __FUNCTION__);
        return INVALID_OPERATION;
    }

    status = lock(lockMode);
    if (status != NO_ERROR) {
        mUserBuffer.status = CAMERA3_BUFFER_STATUS_ERROR;
    }

    return status;
}

status_t CameraBuffer::unlock()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    if (mLocked && mType != BUF_TYPE_HANDLE) {
         mLocked = false;
         return NO_ERROR;
    }

    if (mLocked) {
        LOG2("@%s, mHandle:%p, mFormat:%d", __FUNCTION__, mHandle, mFormat);
        int ret = mGbmBufferManager->Unlock(mHandle);
        if (ret) {
            LOGE("@%s: call Unlock fail, mHandle:%p, ret:%d", __FUNCTION__, mHandle, ret);
            return UNKNOWN_ERROR;
        }

        mLocked = false;
        return NO_ERROR;
    }
    LOGW("@%s:trying to unlock a buffer that is not locked", __FUNCTION__);
    return INVALID_OPERATION;
}

void CameraBuffer::dump()
{
    if (mInit) {
        LOG1("Buffer dump: handle %p: locked :%d: dataPtr:%p",
            (void*)&mHandle, mLocked, mDataPtr);
    } else {
        LOG1("Buffer dump: Buffer not initialized");
    }
}

void CameraBuffer::dumpImage(const int type, const char *name)
{
    if (CC_UNLIKELY(LogHelper::isDumpTypeEnable(type))) {
        dumpImage(name);
    }
}

void CameraBuffer::dumpImage(const char *name)
{
#ifdef DUMP_IMAGE
    status_t status = lock();
    CheckAndLogError((status != OK), VOID_VALUE, "failed to lock dump buffer");

    if (!nonContiguousYandUV()) {
        dumpImage(mDataPtr, nullptr, mSize, 0, mWidth, mHeight, name);
    } else {
        dumpImage(mDataPtr, mDataPtrUV, mSizeY, mSizeUV, mWidth, mHeight, name);
    }

    unlock();
#endif
}

void CameraBuffer::dumpImage(const void *data, const void* dataUV,
                             const int size, int sizeUV, int width, int height,
                             const char *name)
{
#ifdef DUMP_IMAGE
    static unsigned int count = 0;
    count++;

    if (gDumpInterval > 1) {
        if (count % gDumpInterval != 0) {
            return;
        }
    }

    // one example for the file name: /tmp/dump_1920x1080_00000346_PREVIEW_0
    std::string fileName;
    std::string dumpPrefix = "dump_";
    char dumpSuffix[100] = {};
    snprintf(dumpSuffix, sizeof(dumpSuffix),
        "%dx%d_%08u_%s_%d", width, height, count, name, mRequestID);
    fileName = std::string(gDumpPath) + dumpPrefix + std::string(dumpSuffix);

    LOG2("%s filename is %s", __FUNCTION__, fileName.data());

    FILE *fp = fopen (fileName.data(), "w+");
    if (fp == nullptr) {
        LOGE("open file failed");
        return;
    }
    LOG1("Begin write image %s", fileName.data());

    if ((fwrite(data, size, 1, fp)) != 1)
        LOGW("Error or short count writing %d bytes to %s", size, fileName.data());

    if (dataUV) {
        if ((fwrite(dataUV, sizeUV, 1, fp)) != 1)
            LOGW("Error or short count writing %d bytes to %s", size, fileName.data());
    }

    fclose (fp);
    // always leave the latest gDumpCount "dump_xxx" files
    if (gDumpCount <= 0) {
        return;
    }
    // read the "dump_xxx" files name into vector
    std::vector<std::string> fileNames;
    DIR* dir = opendir(gDumpPath);
    CheckAndLogError(dir == nullptr, VOID_VALUE, "@%s, call opendir() fail",
                     __FUNCTION__);
    struct dirent* dp = nullptr;
    while ((dp = readdir(dir)) != nullptr) {
        char* ret = strstr(dp->d_name, dumpPrefix.c_str());
        if (ret) {
            fileNames.push_back(dp->d_name);
        }
    }
    closedir(dir);

    // remove the old files when the file number is > gDumpCount
    if (fileNames.size() > gDumpCount) {
        std::sort(fileNames.begin(), fileNames.end());
        for (size_t i = 0; i < (fileNames.size() - gDumpCount); ++i) {
            std::string fullName = gDumpPath + fileNames[i];
            remove(fullName.c_str());
        }
    }
#endif
}

/**
 * Convert NV12M/NV21M buffer to NV12/NV21 of heap buffer. Debug only.
 */
std::shared_ptr<CameraBuffer>
CameraBuffer::ConvertNVXXMToNVXXAsHeapBuffer(std::shared_ptr<CameraBuffer> input)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    if (input->v4l2Fmt() != V4L2_PIX_FMT_NV12M &&
        input->v4l2Fmt() != V4L2_PIX_FMT_NV21M
        ) {
        LOGE("%s unsupported format %s",
             __FUNCTION__, v4l2Fmt2Str(input->v4l2Fmt()));
        return nullptr;
    }

    int targetFormat = (input->v4l2Fmt() == V4L2_PIX_FMT_NV12M) ?
        V4L2_PIX_FMT_NV12 : V4L2_PIX_FMT_NV21;

    uint32_t width = input->width();
    uint32_t height = input->height();

    if (input->lock() != NO_ERROR) {
        LOGE("Failed to lock CameraBuffer, buffer type %d", input->type());
        return nullptr;
    }

    auto output = allocateHeapBuffer(
             width,
             height,
             width,
             targetFormat,
             input->cameraId(),
             PAGE_ALIGN(input->size()));

    // Copy y plane.
    uint8_t* output_y = static_cast<uint8_t*>(output->data());
    libyuv::CopyPlane(static_cast<const uint8_t*>(input->dataY()),
                      input->stride(),
                      output_y,
                      output->stride(), width, height);

    // Copy uv plane after y.
    uint8_t* output_c = output_y + (height * output->stride());
    libyuv::CopyPlane(static_cast<const uint8_t*>(input->dataUV()),
                      input->stride(),
                      output_c,
                      output->stride(), width, height/2);

    input->unlock();
    return output;
}

/**
 * Allocates the memory needed to store the image described by the parameters
 * passed during construction
 */
std::shared_ptr<CameraBuffer>
CameraBuffer::allocateHeapBuffer(int w,
                   int h,
                   int s,
                   int v4l2Fmt,
                   int cameraId,
                   int dataSizeOverride)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    if (!SupportedFormat(v4l2Fmt)) {
        LOGE("Could not allocate unsupported heap camera buffer of format %s",
             v4l2Fmt2Str(v4l2Fmt));
        return nullptr;
    }

    void *dataPtr;
    int dataSize = dataSizeOverride ? dataSizeOverride : frameSize(v4l2Fmt, s, h);
    LOG1("@%s, dataSize:%d", __FUNCTION__, dataSize);

    int ret = posix_memalign(&dataPtr, sysconf(_SC_PAGESIZE), dataSize);
    if (dataPtr == nullptr || ret != 0) {
        LOGE("Could not allocate heap camera buffer of size %d", dataSize);
        return nullptr;
    }

    return std::shared_ptr<CameraBuffer>(new CameraBuffer(w, h, s, v4l2Fmt, dataPtr, cameraId, dataSizeOverride));
}

/**
 * Allocates internal GBM buffer
 */
std::shared_ptr<CameraBuffer>
CameraBuffer::allocateHandleBuffer(int w,
                     int h,
                     int gfxFmt,
                     int usage,
                     int cameraId)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    cros::CameraBufferManager* bufManager = cros::CameraBufferManager::GetInstance();
    buffer_handle_t handle;
    uint32_t stride = 0;

    LOG1("%s, [wxh] = [%dx%d], format 0x%x, usage 0x%x",
          __FUNCTION__, w, h, gfxFmt, usage);
    int ret = bufManager->Allocate(w, h, gfxFmt, usage, cros::GRALLOC, &handle, &stride);
    if (ret != 0) {
        LOGE("Allocate handle failed! %d", ret);
        return nullptr;
    }

    std::shared_ptr<CameraBuffer> buffer(new CameraBuffer());
    camera3_stream_t stream;
    CLEAR(stream);
    stream.width = w;
    stream.height = h;
    stream.format = gfxFmt;
    stream.usage = usage;
    ret = buffer->init(&stream, handle, cameraId);
    if (ret != NO_ERROR) {
        // buffer handle will free in CameraBuffer destructor.
        return nullptr;
    }

    return buffer;
}

/**
 * Create the MMAP camera buffer to store the image described by the parameters
 * passed during construction
 */
std::shared_ptr<CameraBuffer>
CameraBuffer::createMMapBuffer(int w, int h, int s, int fd,
                               int lengthY, int lengthUV,
                               int v4l2Fmt, int offsetY,
                               int offsetUV, int prot, int flags)
{
    if (!SupportedFormat(v4l2Fmt)) {
        LOGE("Could not create unsupported mmap camera buffer of format %s",
             v4l2Fmt2Str(v4l2Fmt));
        return nullptr;
    }

    std::vector<int> length;
    std::vector<int> offset;

    length.push_back(lengthY);
    offset.push_back(offsetY);

    if (numOfNonContiguousPlanes(v4l2Fmt) > 1) {
        length.push_back(lengthUV);
        offset.push_back(offsetUV);
    }

    return std::shared_ptr<CameraBuffer>(new CameraBuffer(
            w, h, s, fd, v4l2Fmt, length,
            offset, prot, flags));
}

} NAMESPACE_DECLARATION_END
