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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_FEATURE_FEATURECORE_FEATUREPIPE_STREAMING_P2AMDPNODE_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_FEATURE_FEATURECORE_FEATUREPIPE_STREAMING_P2AMDPNODE_H_

#include "StreamingFeatureNode.h"
#include "MDPWrapper.h"

namespace NSCam {
namespace NSCamFeature {
namespace NSFeaturePipe {

class P2AMDPNode : public StreamingFeatureNode {
 public:
  explicit P2AMDPNode(const char* name);
  virtual ~P2AMDPNode();

 public:
  virtual MBOOL onData(DataID id, const P2AMDPReqData& data);

 protected:
  virtual MBOOL onInit();
  virtual MBOOL onThreadLoop();

 private:
  MVOID handleRequest(const P2AMDPReqData& mMdpRequests);
  MBOOL processMDP(const P2AMDPReqData& mMdpRequests);

 private:
  WaitQueue<P2AMDPReqData> mMdpRequests;
  MDPWrapper mMDP;
};

}  // namespace NSFeaturePipe
}  // namespace NSCamFeature
}  // namespace NSCam

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_FEATURE_FEATURECORE_FEATUREPIPE_STREAMING_P2AMDPNODE_H_
