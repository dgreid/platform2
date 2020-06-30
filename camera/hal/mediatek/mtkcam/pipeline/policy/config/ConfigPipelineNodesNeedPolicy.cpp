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

#define LOG_TAG "mtkcam-ConfigPipelineNodesNeedPolicy"

#include <mtkcam/pipeline/policy/IConfigPipelineNodesNeedPolicy.h>
//
#include "MyUtils.h"

/******************************************************************************
 *
 ******************************************************************************/
/******************************************************************************
 *
 ******************************************************************************/
namespace NSCam {
namespace v3 {
namespace pipeline {
namespace policy {

/**
 * Make a function target as a policy for Non-P1 - default version
 */
FunctionType_Configuration_PipelineNodesNeedPolicy
makePolicy_Configuration_PipelineNodesNeed_Default() {
  return [](Configuration_PipelineNodesNeed_Params const& params) -> int {
    auto pOut = params.pOut;
    auto pPipelineUserConfiguration = params.pPipelineUserConfiguration;
    auto const& pParsedAppConfiguration =
        pPipelineUserConfiguration->pParsedAppConfiguration;
    auto const& pParsedAppImageStreamInfo =
        pPipelineUserConfiguration->pParsedAppImageStreamInfo;

    // default pipeline will contain 1 p1node.
    pOut->needP1Node.push_back(true);
    //}

    if (pParsedAppImageStreamInfo->vAppImage_Output_Proc.size() != 0) {
      pOut->needP2StreamNode = true;
    } else {
      pOut->needP2StreamNode = false;
    }
    if (pParsedAppImageStreamInfo->vAppImage_Output_Proc.size() != 0 ||
        pParsedAppImageStreamInfo->pAppImage_Jpeg != nullptr) {
      pOut->needP2CaptureNode =
          pParsedAppConfiguration->isConstrainedHighSpeedMode ? false : true;
    } else {
      pOut->needP2CaptureNode = false;
    }

    char value[PROPERTY_VALUE_MAX];
    property_get("vendor.debug.camera.fd.enable", value, "1");
    int FDEnable = atoi(value);
    if (FDEnable == 1) {
      pOut->needFDNode =
          (pParsedAppConfiguration->operationMode == 0 /*NORMAL_MODE*/ &&
           pOut->needP2StreamNode)
              ? true
              : false;
    } else {
      pOut->needFDNode = false;
    }

    pOut->needJpegNode =
        pParsedAppImageStreamInfo->pAppImage_Jpeg.get() ? true : false;

    return OK;
  };
}

};  // namespace policy
};  // namespace pipeline
};  // namespace v3
};  // namespace NSCam
