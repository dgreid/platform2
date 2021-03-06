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

#ifndef _CAMERA3_HAL_CAMERA_BUFFER_H_
#define _CAMERA3_HAL_CAMERA_BUFFER_H_

#include <utils/Errors.h>
#include <hardware/camera3.h>
#include "Camera3V4l2Format.h"
#include "UtilityMacros.h"
#include <camera_buffer_manager.h>
#include <memory>
#include <vector>

NAMESPACE_DECLARATION {

// Forward declaration to  avoid extra include
class CameraStream;

/**
 * \class CameraBuffer
 *
 * This class is the buffer abstraction in the HAL. It can store buffers
 * provided by the framework or buffers allocated by the HAL.
 * Allocation in the HAL can be done via gralloc, malloc or mmap
 * in case of mmap the memory cannot be freed
 */
class CameraBuffer {
public:
    enum BufferType {
        BUF_TYPE_HANDLE,
        BUF_TYPE_MALLOC,
        BUF_TYPE_MMAP,
    };

public:
    /* Convert NV12M/NV21M buffer to NV12/NV21 of heap buffer. Debug only. */
    static std::shared_ptr<CameraBuffer>
    ConvertNVXXMToNVXXAsHeapBuffer(std::shared_ptr<CameraBuffer> input);

    static std::shared_ptr<CameraBuffer>
    allocateHeapBuffer(int w,
                       int h,
                       int s,
                       int v4l2Fmt,
                       int cameraId,
                       int dataSizeOverride = 0);

    static std::shared_ptr<CameraBuffer>
    allocateHandleBuffer(int w,
                         int h,
                         int gfxFmt,
                         int usage,
                         int cameraId);

    static std::shared_ptr<CameraBuffer>
    createMMapBuffer(int w, int h, int s, int fd,
                     int lengthY, int lengthUV, int v4l2fmt, int offsetY,
                     int offsetUV, int prot, int flags);

public:
    /**
     * default constructor
     * Used for buffers coming from the framework. The wrapper is initialized
     * using the method init
     */
    CameraBuffer();

    /**
     * initialization for the wrapper around the framework buffers
     */
    status_t init(const camera3_stream_buffer *aBuffer, int cameraId);

    /**
     * initialization for the fake framework buffer (allocated by the HAL)
     */
    status_t init(const camera3_stream_t* stream, buffer_handle_t buffer,
                  int cameraId);

    /**
     * deinitialization for the wrapper around the framework buffers
     */
    status_t deinit();

    /**
     * no need to delete a buffer since it is RefBase'd. Buffer will be deleted
     * when no reference to it exist.
     */
    ~CameraBuffer();


    void* data() { return mDataPtr; };
    void* dataY() { return mDataPtr; };
    void* dataUV() { return mDataPtrUV; };

    status_t lock();
    status_t lock(int flags);
    status_t unlock();

    bool isRegistered() const { return mRegistered; };
    bool isLocked() const { return mLocked; };
    buffer_handle_t * getBufferHandle() { return &mHandle; };
    status_t waitOnAcquireFence();

    void dump();
    void dumpImage(const int type, const char *name);
    void dumpImage(const char *name);
    void dumpImage(const void *data, const void* dataUV,
                             const int size, int sizeUV, int width, int height,
                             const char *name);
    CameraStream * getOwner() const { return mOwner; }
    int width() {return mWidth; }
    int height() {return mHeight; }
    int stride() {return mStride; }
    unsigned int size() {return mSize; }
    unsigned int sizeY() {return mSizeY; }
    unsigned int sizeUV() {return mSizeUV; }
    int format() {return mFormat; }
    int v4l2Fmt() {return mV4L2Fmt; }
    struct timeval timeStamp() {return mTimestamp; }
    int64_t timeStampNano() { return TIMEVAL2NSECS(&mTimestamp); }
    void setTimeStamp(struct timeval timestamp) {mTimestamp = timestamp; }
    void setRequestId(int requestId) {mRequestID = requestId; }
    int requestId() {return mRequestID; }
    status_t getFence(camera3_stream_buffer* buf);
    int dmaBufFd(int plane = 0);
    int dmaBufFdOffset(int plane = 0);
    int status() { return mUserBuffer.status; }
    int cameraId() { return mCameraId; }
    BufferType type() { return mType; }
    bool nonContiguousYandUV() { return mNonContiguousYandUV; }

protected:
    /**
     * constructor for the HAL-allocated buffer
     * These are used via the utility methods.
     */
    CameraBuffer(int w, int h, int s, int v4l2fmt, void* usrPtr, int cameraId, int dataSizeOverride = 0);
    CameraBuffer(int w, int h, int s, int fd, int v4l2fmt,
                               std::vector<int> length, std::vector<int> offset,
                               int prot, int flags);

private:
    status_t registerBuffer();
    status_t deregisterBuffer();

private:
    camera3_stream_buffer_t mUserBuffer; /*!< Original structure passed by request */
    int             mWidth;
    int             mHeight;
    unsigned int    mSize;           /*!< size in bytes, this is filled when we
                                           lock the buffer */
    unsigned int    mSizeY;           /*!< size Y plane in bytes, this is filled when we
                                           lock the buffer */
    unsigned int    mSizeUV;           /*!< size UV plane in bytes, this is filled when we
                                           lock the buffer */
    int             mFormat;         /*!<  HAL PIXEL fmt */
    int             mV4L2Fmt;        /*!< V4L2 fourcc format code */
    int             mStride;
    int             mUsage;
    struct timeval  mTimestamp;
    bool            mInit;           /*!< Boolean to check the integrity of the
                                          buffer when it is created*/
    bool            mLocked;         /*!< Use to track the lock status */
    bool            mRegistered;     /*!< Use to track the buffer register status */

    BufferType mType;
    cros::CameraBufferManager* mGbmBufferManager;
    buffer_handle_t mHandle;
    buffer_handle_t* mHandlePtr;
    CameraStream *mOwner;             /*!< Stream this buffer belongs to */
    void*         mDataPtr;           /*!< if locked, here is the vaddr of Y */
    void*         mDataPtrUV;           /*!< if locked, here is the vaddr of UV */
    int           mRequestID;         /*!< this is filled by hw streams after
                                          calling putframe */
    int mCameraId;
    bool mNonContiguousYandUV;     /*!< Whether Y and UV are non contiguous planes */
};


} NAMESPACE_DECLARATION_END

#endif // _CAMERA3_HAL_CAMERA_BUFFER_H_
