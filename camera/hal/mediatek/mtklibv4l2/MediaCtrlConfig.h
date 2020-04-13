/*
 * Copyright (C) 2019 MediaTek Inc.
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

#ifndef CAMERA_HAL_MEDIATEK_MTKLIBV4L2_MEDIACTRLCONFIG_H_
#define CAMERA_HAL_MEDIATEK_MTKLIBV4L2_MEDIACTRLCONFIG_H_

#include <map>
#include <string>
#include <vector>

#include "Errors.h"
#include "CommonUtilMacros.h"

#include <linux/media.h>

using std::map;
using std::size_t;
using std::string;
using std::vector;

#define MAX_P1_MEDIADEVICETAG_NUM (4)
#define MAX_P2_MEDIADEVICETAG_NUM (8)
#define MAX_NewP2_MEDIADEVICETAG_NUM (3)

#define MEDIA_CONTROLLER_TAG 0xF0000
#define MEDIA_CONTROLLER_P1_TAG 0x10000
#define MEDIA_CONTROLLER_P2_TAG 0x20000

enum MediaDeviceTag {
  MEDIA_CONTROLLER_P1_Unused = MEDIA_CONTROLLER_P1_TAG,
  MEDIA_CONTROLLER_P1_OUT1,
  MEDIA_CONTROLLER_P1_OUT2,
  MEDIA_CONTROLLER_P2_Unused = MEDIA_CONTROLLER_P2_TAG,
  MEDIA_CONTROLLER_P2_Preview_OUT1,
  MEDIA_CONTROLLER_P2_Preview_OUT2,
  MEDIA_CONTROLLER_P2_Capture_OUT1,
  MEDIA_CONTROLLER_P2_Capture_OUT2,
  MEDIA_CONTROLLER_P2_Record_OUT1,
  MEDIA_CONTROLLER_P2_Record_OUT2,
  MEDIA_CONTROLLER_P2_Reprocessing_OUT1,
  MEDIA_CONTROLLER_P2_Reprocessing_OUT2,
  MEDIA_CONTROLLER_P2New_Preview_FD_3DNR_IN4OUT4,
  MEDIA_CONTROLLER_P2New_Capture_FD_3DNR_IN4OUT4,
  MEDIA_CONTROLLER_P2New_Reprocessing_FD_3DNR_IN4OUT4,
  MEDIA_CONTROLLER_END = 0xFFFFFFFF
};

typedef struct MediaCtrl_P1Topology {
  MediaDeviceTag tag;
  string mdev_name;
  string hub_name;
  string tunig_source_name;
  string tunig_sink1_name;
  string tunig_sink2_name;
  string tunig_sink3_name;
  string tunig_sink4_name;
  string imgo_sink_name;
  string rrzo_sink_name;
} MediaCtrl_P1Topology;

typedef struct MediaCtrl_P2Topology {
  MediaDeviceTag tag;
  string mdev_name;
  string hub_name;
  string raw_source_name;
  string tunig_source_name;
  string sink1_name;
  string sink2_name;
} MediaCtrl_P2Topology;

typedef struct MediaCtrl_P2NewTopology {
  MediaDeviceTag tag;
  string mdev_name;
  string hub_name;
  string raw_source_name;
  string tunig_source_name;
  string raw_source2_name;
  string raw_source3_name;
  string sink1_name;
  string sink2_name;
  string sink3_name;
  string sink4_name;
} MediaCtrl_P2NewTopology;

typedef struct MediaCtlElement {
  string name;
} MediaCtlElement;

typedef struct MediaCtlLinkParams {
  string srcName;
  int srcPad;
  string sinkName;
  int sinkPad;
  bool enable;
  int flags;
} MediaCtlLinkParams;

typedef struct MediaCtlConfig {
  vector<MediaCtlLinkParams> mLinkParams;
  vector<MediaCtlElement> mVideoNodes;
} MediaCtlConfig;

typedef struct EntityNameMap {
  string entityName;
  string devName;
} EntityNameMap;

class MediaCtrlConfig {
 public:
  static string getMediaDeviceNameByTag(MediaDeviceTag mdevTag);
  static string getMediaDevicePathByName(const string& driverName);
  static void addVideoNodes(string name, MediaCtlConfig* config);
  static void addLinkParams(const string& srcName,
                            int srcPad,
                            const string& sinkName,
                            int sinkPad,
                            int enable,
                            int flags,
                            MediaCtlConfig* config);

  static void CreateMediaCtlGraph(MediaDeviceTag mdevTag,
                                  bool hasTuning,
                                  MediaCtlConfig* mediaCtlConfig);

 private:
  MediaCtrlConfig() = default;
  ~MediaCtrlConfig() = default;
};

#endif  // CAMERA_HAL_MEDIATEK_MTKLIBV4L2_MEDIACTRLCONFIG_H_
