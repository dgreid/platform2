/*
 * Copyright (C) 2015-2020 Intel Corporation
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

#ifndef _CAMERA3_HAL_MEDIACTLPIPECONFIG_H_
#define _CAMERA3_HAL_MEDIACTLPIPECONFIG_H_

#include <vector>
#include <string>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

using std::string;

namespace cros {
namespace intel {
typedef struct MediaCtlElement {
    string name;
    string type;
    int isysNodeName;
} MediaCtlElement;

typedef struct ConfigProperties {
    int outputWidth;
    int outputHeight;
    string name;
    int id;
} ConfigProperties;

typedef struct FrameTimingCalcSize {
    int Width;
    int Height;
} FrameTimingCalcSize;

typedef struct MediaCtlLinkParams {
    string srcName;
    int srcPad;
    string sinkName;
    int sinkPad;
    bool enable;
    int flags;
} MediaCtlLinkParams;

typedef struct MediaCtlFormatParams {
    string entityName;
    int pad;
    int width;
    int height;
    int formatCode;
    int stride;
    int field;
} MediaCtlFormatParams;

typedef struct MediaCtlSelectionParams {
    string entityName;
    int pad;
    int target;
    int top;
    int left;
    int width;
    int height;
} MediaCtlSelectionParams;

typedef struct MediaCtlSelectionVideoParams {
    string entityName;
    struct v4l2_subdev_selection select;
} MediaCtlSelectionVideoParams;

typedef struct MediaCtlControlParams {
    string entityName;
    int controlId;
    int value;
    string controlName;
} MediaCtlControlParams;

/**
 * \struct MediaCtlSingleConfig
 *
 * This struct is holding all the possible
 * media ctl pipe configurations for the
 * camera in use.
 * It is holding the commands parameters for
 * setting up a camera pipe.
 *
 */
typedef struct MediaCtlConfig {
    ConfigProperties mCameraProps;
    FrameTimingCalcSize mFTCSize;
    std::vector<MediaCtlLinkParams> mLinkParams;
    std::vector<MediaCtlFormatParams> mFormatParams;
    std::vector<MediaCtlSelectionParams> mSelectionParams;
    std::vector<MediaCtlSelectionVideoParams> mSelectionVideoParams;
    std::vector<MediaCtlControlParams> mControlParams;
    std::vector<MediaCtlElement> mVideoNodes;
} MediaCtlConfig;

} /* namespace intel */
} /* namespace cros */
#endif  // _CAMERA3_HAL_MEDIACTLPIPECONFIG_H_
