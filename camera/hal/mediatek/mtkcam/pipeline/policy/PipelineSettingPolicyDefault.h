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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_POLICY_PIPELINESETTINGPOLICYDEFAULT_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_POLICY_PIPELINESETTINGPOLICYDEFAULT_H_
//
#include "PipelineSettingPolicyBase.h"

/******************************************************************************
 *
 ******************************************************************************/
namespace NSCam {
namespace v3 {
namespace pipeline {
namespace policy {
namespace pipelinesetting {

/******************************************************************************
 *
 ******************************************************************************/
class PipelineSettingPolicyDefault : public PipelineSettingPolicyBase {
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  Data Members.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  Operations.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////    Instantiation.
  explicit PipelineSettingPolicyDefault(CreationParams const& creationParams);

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  IPipelineSettingPolicy Interfaces.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////    Interfaces.
  auto evaluateRequest(RequestOutputParams* out __unused,
                       RequestInputParams const* in __unused) -> bool override;
};

/******************************************************************************
 *
 ******************************************************************************/
};  // namespace pipelinesetting
};  // namespace policy
};  // namespace pipeline
};  // namespace v3
};  // namespace NSCam
#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_POLICY_PIPELINESETTINGPOLICYDEFAULT_H_
