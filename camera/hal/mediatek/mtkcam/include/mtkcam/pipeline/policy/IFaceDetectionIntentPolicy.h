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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_PIPELINE_POLICY_IFACEDETECTIONINTENTPOLICY_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_PIPELINE_POLICY_IFACEDETECTIONINTENTPOLICY_H_
//
#include "./types.h"

#include <functional>

/******************************************************************************
 *
 ******************************************************************************/
namespace NSCam {
namespace v3 {
namespace pipeline {
namespace policy {
namespace fdintent {

/**
 * A structure definition for output parameters
 */
struct RequestOutputParams {
  /**
   * true indicates it intents to enable the face detection.
   */
  bool isFdEnabled = false;
  bool isFDMetaEn = false;
  bool hasFDMeta = false;
};

/**
 * A structure definition for input parameters
 */
struct RequestInputParams {
  /**************************************************************************
   * Request parameters
   *
   * The parameters related to this capture request is shown as below.
   *
   **************************************************************************/

  /**
   * Request App metadata control, sent at the request stage.
   *
   * pRequest_ParsedAppMetaControl is a partial parsed result from
   * pRequest_AppControl, just for the purpose of a quick reference.
   */
  IMetadata const* pRequest_AppControl = nullptr;
  ParsedMetaControl const* pRequest_ParsedAppMetaControl = nullptr;

  /**
   * true indicates that face detection is enabled at the last frame.
   */
  bool isFdEnabled_LastFrame = false;

  /*************************************************************************
   * Configuration info.
   *
   * The final configuration information of the pipeline decided at the
   * configuration stage are as below.
   *
   **************************************************************************/

  /**
   * true indicates FDNode is configured at the configuration stage.
   */
  bool hasFDNodeConfigured = false;
};

};  // namespace fdintent

////////////////////////////////////////////////////////////////////////////////

/**
 * The function type definition.
 * It is used to decide whether or not it intents to enable the face detection.
 *
 * @param[out] out: input parameters
 *
 * @param[in] in: input parameters
 *
 * @return
 *      0 indicates success; otherwise failure.
 */
using FunctionType_FaceDetectionIntentPolicy =
    std::function<int(fdintent::RequestOutputParams& /*out*/,
                      fdintent::RequestInputParams const& /*in*/)>;

//==============================================================================

/**
 * Policy instance makers
 *
 */

// default version
FunctionType_FaceDetectionIntentPolicy makePolicy_FDIntent_Default();

/******************************************************************************
 *
 ******************************************************************************/
};      // namespace policy
};      // namespace pipeline
};      // namespace v3
};      // namespace NSCam
#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_PIPELINE_POLICY_IFACEDETECTIONINTENTPOLICY_H_
