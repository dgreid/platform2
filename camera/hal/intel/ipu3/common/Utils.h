/*
 * Copyright (C) 2014-2019 Intel Corporation.
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

#ifndef CAMERA3_HAL_UTILS_H
#define CAMERA3_HAL_UTILS_H

#include <cros-camera/camera_thread.h>
#include <queue>
#include <string>
#include <utils/Errors.h>
#include <vector>

namespace cros {
namespace intel {
// Parse string like "640x480" or "10000,20000"
// copy from android CameraParameters.cpp
int parsePair(const char *str, int *first, int *second, char delim, char **endptr = nullptr);

//Resize a 2D array with linear interpolation
//For some cases, we need to upscale or downscale a 2D array.
//For example, Android requests lensShadingMapSize must be smaller than 64*64,
//but for some sensors, the lens shading map is bigger than this, so need to do resize.
template <typename T> int resize2dArray(
    const T* a_src, int a_src_w, int a_src_h,
    T* a_dst, int a_dst_w, int a_dst_h);

// Splits a string into substrings and stores them into vector.
void getTokens(const char *s, const char delim, std::vector<std::string> &tokens);

typedef int64_t nsecs_t;       // nano-seconds

// return the system-time according to the specified clock
nsecs_t systemTime();

void dumpToFile(const void* data, int size, int width, int height, int reqId, const std::string& name);

// The class is used to dump file in new thread for not blocking performance
class CameraDumpAsync
{
public:
    CameraDumpAsync(std::string& pipeType, size_t pipelineDepth, int width, int heigt, uint32_t size);
    virtual ~CameraDumpAsync();
    void dumpImageToFile(const void *data, uint32_t size, int reqId, const std::string &name);

    struct MessageConfig {
        int reqId;
        uint32_t size;
        std::string name;
        std::unique_ptr<char[]> data;

        MessageConfig() :
            reqId(-1),
            size(0) {}
    };

private:
    void handleDumpImageToFile(MessageConfig msg);
    int mWidth;
    int mHeight;
    uint32_t mSize;

    bool mInitialized;
    cros::CameraThread mCameraThread;
    std::mutex mDumpRawImageLock;
    std::queue<std::unique_ptr<char[]>> mDumpRawImageQueue;
};

} /* namespace intel */
} /* namespace cros */
#endif // CAMERA3_HAL_UTILS_H
