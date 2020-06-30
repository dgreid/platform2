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

#define LOG_TAG "mtkcam-RequestSettingPolicyMediator"

#include <memory>
#include <mtkcam/pipeline/policy/InterfaceTableDef.h>
#include <mtkcam/pipeline/policy/IRequestSettingPolicyMediator.h>

#include "MyUtils.h"
#include <vector>
/******************************************************************************
 *
 ******************************************************************************/

/******************************************************************************
 *
 ******************************************************************************/
class RequestSettingPolicyMediator_Default
    : public NSCam::v3::pipeline::policy::pipelinesetting::
          IRequestSettingPolicyMediator {
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  Data Members.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 protected:  ////    Static data (unchangable)
  std::shared_ptr<NSCam::v3::pipeline::policy::PipelineStaticInfo const>
      mPipelineStaticInfo;
  std::shared_ptr<NSCam::v3::pipeline::policy::PipelineUserConfiguration const>
      mPipelineUserConfiguration;
  std::shared_ptr<
      NSCam::v3::pipeline::policy::pipelinesetting::PolicyTable const>
      mPolicyTable;

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  Operations.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////    Instantiation.
  explicit RequestSettingPolicyMediator_Default(
      NSCam::v3::pipeline::policy::pipelinesetting::
          MediatorCreationParams const& params);

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //  IRequestSettingPolicyMediator Interfaces.
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 public:  ////    Interfaces.
  auto evaluateRequest(
      NSCam::v3::pipeline::policy::pipelinesetting::RequestOutputParams* out,
      NSCam::v3::pipeline::policy::pipelinesetting::RequestInputParams const&
          in) -> int override;

 private:
  bool misFdEnabled = false;
};

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<
    NSCam::v3::pipeline::policy::pipelinesetting::IRequestSettingPolicyMediator>
makeRequestSettingPolicyMediator_Default(
    NSCam::v3::pipeline::policy::pipelinesetting::MediatorCreationParams const&
        params) {
  return std::make_shared<RequestSettingPolicyMediator_Default>(params);
}

/******************************************************************************
 *
 ******************************************************************************/
RequestSettingPolicyMediator_Default::RequestSettingPolicyMediator_Default(
    NSCam::v3::pipeline::policy::pipelinesetting::MediatorCreationParams const&
        params)
    : IRequestSettingPolicyMediator(),
      mPipelineStaticInfo(params.pPipelineStaticInfo),
      mPipelineUserConfiguration(params.pPipelineUserConfiguration),
      mPolicyTable(params.pPolicyTable) {}

/******************************************************************************
 *
 ******************************************************************************/
auto RequestSettingPolicyMediator_Default::evaluateRequest(
    NSCam::v3::pipeline::policy::pipelinesetting::RequestOutputParams* out,
    NSCam::v3::pipeline::policy::pipelinesetting::RequestInputParams const& in)
    -> int {
  NSCam::v3::pipeline::policy::fdintent::RequestOutputParams fdOut;
  NSCam::v3::pipeline::policy::fdintent::RequestInputParams fdIn;
  NSCam::v3::pipeline::policy::p2nodedecision::RequestOutputParams
      p2DecisionOut;
  NSCam::v3::pipeline::policy::p2nodedecision::RequestInputParams p2DecisionIn;
  NSCam::v3::pipeline::policy::featuresetting::RequestOutputParams featureOut;
  NSCam::v3::pipeline::policy::featuresetting::RequestInputParams featureIn;
  NSCam::v3::pipeline::policy::capturestreamupdater::RequestOutputParams
      capUpdaterOut;
  NSCam::v3::pipeline::policy::capturestreamupdater::RequestInputParams
      capUpdaterIn;
  std::shared_ptr<NSCam::v3::IImageStreamInfo> pJpeg_YUV = nullptr;
  std::shared_ptr<NSCam::v3::IImageStreamInfo> pThumbnail_YUV = nullptr;

  bool noP2Node = !in.pConfiguration_PipelineNodesNeed->needP2CaptureNode &&
                  !in.pConfiguration_PipelineNodesNeed->needP2StreamNode;
  // (1) is face detection intent?
  if (CC_LIKELY(mPolicyTable->fFaceDetectionIntent != nullptr)) {
    fdIn.hasFDNodeConfigured = in.pConfiguration_PipelineNodesNeed->needFDNode;
    fdIn.isFdEnabled_LastFrame = misFdEnabled;
    fdIn.pRequest_AppControl = in.pRequest_AppControl;
    fdIn.pRequest_ParsedAppMetaControl = in.pRequest_ParsedAppMetaControl;
    mPolicyTable->fFaceDetectionIntent(fdOut, fdIn);
    misFdEnabled = fdOut.hasFDMeta;
  }

  // (2.1) are any capture streams updated?
  if (in.pRequest_AppImageStreamInfo->pAppImage_Jpeg != nullptr &&
      CC_LIKELY(mPolicyTable->fCaptureStreamUpdater != nullptr)) {
    capUpdaterIn.sensorID = mPipelineStaticInfo->sensorIds[0];
    capUpdaterIn.pRequest_AppControl = in.pRequest_AppControl;
    capUpdaterIn.pRequest_ParsedAppMetaControl =
        in.pRequest_ParsedAppMetaControl;
    capUpdaterIn.isJpegRotationSupported =
        true;  // use property to control is support or not?
    capUpdaterIn.pConfiguration_HalImage_Jpeg_YUV =
        &(in.pConfiguration_StreamInfo_NonP1->pHalImage_Jpeg_YUV);
    capUpdaterIn.pConfiguration_HalImage_Thumbnail_YUV =
        &(in.pConfiguration_StreamInfo_NonP1->pHalImage_Thumbnail_YUV);
    // prepare out buffer
    capUpdaterOut.pHalImage_Jpeg_YUV = &pJpeg_YUV;
    capUpdaterOut.pHalImage_Thumbnail_YUV = &pThumbnail_YUV;
    mPolicyTable->fCaptureStreamUpdater(capUpdaterOut, capUpdaterIn);
  }

  // (2.2) P2Node decision: the responsibility of P2StreamNode and P2CaptureNode
  if (CC_LIKELY(mPolicyTable->fP2NodeDecision != nullptr)) {
    p2DecisionIn.requestNo = in.requestNo;
    p2DecisionIn.hasP2CaptureNode =
        in.pConfiguration_PipelineNodesNeed->needP2CaptureNode;
    p2DecisionIn.hasP2StreamNode =
        in.pConfiguration_PipelineNodesNeed->needP2StreamNode;
    p2DecisionIn.isFdEnabled = fdOut.isFDMetaEn;
    p2DecisionIn.pConfiguration_StreamInfo_NonP1 =
        in.pConfiguration_StreamInfo_NonP1;
    p2DecisionIn.pConfiguration_StreamInfo_P1 =
        &((*(in.pConfiguration_StreamInfo_P1))[0]);  // use main1 info
    p2DecisionIn.pRequest_AppControl = in.pRequest_AppControl;
    p2DecisionIn.pRequest_AppImageStreamInfo = in.pRequest_AppImageStreamInfo;
    p2DecisionIn.pRequest_ParsedAppMetaControl =
        in.pRequest_ParsedAppMetaControl;
    p2DecisionIn.needThumbnail = (pThumbnail_YUV != nullptr);
    mPolicyTable->fP2NodeDecision(&p2DecisionOut, p2DecisionIn);
  }

  // (2.3) feature setting
  if (CC_LIKELY(mPolicyTable->mFeaturePolicy != nullptr) && (!noP2Node)) {
    featureIn.requestNo = in.requestNo;
    featureIn.Configuration_HasRecording =
        mPipelineUserConfiguration->pParsedAppImageStreamInfo->hasVideoConsumer;
    featureIn.maxP2CaptureSize = p2DecisionOut.maxP2CaptureSize;
    featureIn.maxP2StreamSize = p2DecisionOut.maxP2StreamSize;
    featureIn.needP2CaptureNode = p2DecisionOut.needP2CaptureNode;
    featureIn.needP2StreamNode = p2DecisionOut.needP2StreamNode;
    featureIn.pConfiguration_StreamInfo_P1 = in.pConfiguration_StreamInfo_P1;
    featureIn.pRequest_AppControl = in.pRequest_AppControl;
    featureIn.pRequest_AppImageStreamInfo = in.pRequest_AppImageStreamInfo;
    featureIn.pRequest_ParsedAppMetaControl = in.pRequest_ParsedAppMetaControl;
    for (size_t i = 0; i < mPipelineStaticInfo->sensorIds.size(); i++) {
      featureIn.sensorModes.push_back((*in.pSensorMode)[i]);
    }
    mPolicyTable->mFeaturePolicy->evaluateRequest(&featureOut, &featureIn);

    (*out).needZslFlow = featureOut.needZslFlow;
    (*out).zslPolicyParams = featureOut.zslPolicyParams;
    (*out).needReconfiguration = featureOut.needReconfiguration;
    (*out).sensorModes = featureOut.sensorModes;
    (*out).reconfigCategory = featureOut.reconfigCategory;
    (*out).boostScenario = featureOut.boostScenario;
    (*out).featureFlag = featureOut.featureFlag;
  }

  // (3) build every frames out param
#define NOT_DUMMY (0)
#define POST_DUMMY (1)
#define PRE_DUMMY (2)
  auto buildOutParam =
      [&](std::shared_ptr<NSCam::v3::pipeline::policy::featuresetting ::
                              RequestResultParams> const setting,
          int dummy, bool isMain) -> int {
    MY_LOGD("build out frame param +");
    std::shared_ptr<
        NSCam::v3::pipeline::policy::pipelinesetting::RequestResultParams>
        result = std::make_shared<NSCam::v3::pipeline::policy::pipelinesetting::
                                      RequestResultParams>();
    // build topology
    NSCam::v3::pipeline::policy::topology::RequestOutputParams topologyOut;
    NSCam::v3::pipeline::policy::topology::RequestInputParams topologyIn;
    if (CC_LIKELY(mPolicyTable->fTopology != nullptr)) {
      topologyIn.isDummyFrame = dummy != 0;
      topologyIn.isFdEnabled = (isMain) ? fdOut.isFdEnabled : false;
      topologyIn.needP2CaptureNode = p2DecisionOut.needP2CaptureNode;
      topologyIn.needP2StreamNode =
          (isMain) ? p2DecisionOut.needP2StreamNode
                   : false;  // p2 stream node don't need process sub frame
      topologyIn.pConfiguration_PipelineNodesNeed =
          in.pConfiguration_PipelineNodesNeed;  // topology no need to ref?
      topologyIn.pConfiguration_StreamInfo_NonP1 =
          in.pConfiguration_StreamInfo_NonP1;
      topologyIn.pPipelineStaticInfo = mPipelineStaticInfo.get();
      topologyIn.pRequest_AppImageStreamInfo =
          (isMain) ? in.pRequest_AppImageStreamInfo : nullptr;
      topologyIn.pvImageStreamId_from_CaptureNode =
          &(p2DecisionOut.vImageStreamId_from_CaptureNode);
      topologyIn.pvImageStreamId_from_StreamNode =
          &(p2DecisionOut.vImageStreamId_from_StreamNode);
      topologyIn.pvMetaStreamId_from_CaptureNode =
          &(p2DecisionOut.vMetaStreamId_from_CaptureNode);
      topologyIn.pvMetaStreamId_from_StreamNode =
          &(p2DecisionOut.vMetaStreamId_from_StreamNode);
      // prepare output buffer
      topologyOut.pNodesNeed = &(result->nodesNeed);
      topologyOut.pNodeSet = &(result->nodeSet);
      topologyOut.pRootNodes = &(result->roots);
      topologyOut.pEdges = &(result->edges);
      mPolicyTable->fTopology(topologyOut, topologyIn);
    }
    // build P2 IO map
    NSCam::v3::pipeline::policy::iomap::RequestOutputParams iomapOut;
    NSCam::v3::pipeline::policy::iomap::RequestInputParams iomapIn;
    if (CC_LIKELY(mPolicyTable->fIOMap_P2Node != nullptr) &&
        CC_LIKELY(mPolicyTable->fIOMap_NonP2Node != nullptr)) {
      std::vector<uint32_t> needP1Dma;
      iomapIn.pConfiguration_StreamInfo_NonP1 =
          in.pConfiguration_StreamInfo_NonP1;
      iomapIn.pConfiguration_StreamInfo_P1 = in.pConfiguration_StreamInfo_P1;
      iomapIn.pRequest_HalImage_Thumbnail_YUV = pThumbnail_YUV.get();
      iomapIn.pRequest_AppImageStreamInfo = in.pRequest_AppImageStreamInfo;
      if (setting != nullptr) {
        iomapIn.pRequest_NeedP1Dma = &(setting->needP1Dma);
      } else {
        for (int i = 0; i < topologyOut.pNodesNeed->needP1Node.size(); i++) {
          uint32_t P1Dma = 0;
          if (topologyOut.pNodesNeed->needP1Node[i] == true) {
            P1Dma = NSCam::v3::pipeline::policy::P1_IMGO |
                    NSCam::v3::pipeline::policy::P1_LCSO;
          }
          needP1Dma.push_back(P1Dma);
        }
        iomapIn.pRequest_NeedP1Dma = &(needP1Dma);
      }
      iomapIn.pRequest_PipelineNodesNeed = topologyOut.pNodesNeed;
      iomapIn.pvImageStreamId_from_CaptureNode =
          &(p2DecisionOut.vImageStreamId_from_CaptureNode);
      iomapIn.pvImageStreamId_from_StreamNode =
          &(p2DecisionOut.vImageStreamId_from_StreamNode);
      iomapIn.pvMetaStreamId_from_CaptureNode =
          &(p2DecisionOut.vMetaStreamId_from_CaptureNode);
      iomapIn.pvMetaStreamId_from_StreamNode =
          &(p2DecisionOut.vMetaStreamId_from_StreamNode);
      iomapIn.isMainFrame = isMain;
      iomapIn.isDummyFrame = dummy != 0;
      // prepare output buffer
      iomapOut.pNodeIOMapImage = &(result->nodeIOMapImage);
      iomapOut.pNodeIOMapMeta = &(result->nodeIOMapMeta);
      if (dummy == NOT_DUMMY) {
        mPolicyTable->fIOMap_P2Node(iomapOut, iomapIn);
      }
      mPolicyTable->fIOMap_NonP2Node(iomapOut, iomapIn);
    }
    //
    if (isMain) {
      if (pJpeg_YUV != nullptr) {
        result->vUpdatedImageStreamInfo[pJpeg_YUV->getStreamId()] = pJpeg_YUV;
      }
      if (pThumbnail_YUV != nullptr) {
        result->vUpdatedImageStreamInfo[pThumbnail_YUV->getStreamId()] =
            pThumbnail_YUV;
      }
    }
    if (setting != nullptr) {
      result->additionalApp = setting->additionalApp;
      result->additionalHal = setting->vAdditionalHal;
    } else {
      result->additionalApp = std::make_shared<NSCam::IMetadata>();
      for (int i = 0; i < topologyOut.pNodesNeed->needP1Node.size(); i++) {
        result->additionalHal.push_back(std::make_shared<NSCam::IMetadata>());
      }
    }

    // update Metdata
    if (CC_LIKELY(mPolicyTable->pRequestMetadataPolicy != nullptr)) {
      NSCam::v3::pipeline::policy::requestmetadata::EvaluateRequestParams
          metdataParams;
      metdataParams.requestNo = in.requestNo;
      metdataParams.isZSLMode = in.isZSLMode;
      metdataParams.pRequest_AppImageStreamInfo =
          in.pRequest_AppImageStreamInfo;
      metdataParams.pRequest_ParsedAppMetaControl =
          in.pRequest_ParsedAppMetaControl;
      metdataParams.pSensorSize = in.pSensorSize;
      metdataParams.pAdditionalApp = result->additionalApp;
      metdataParams.pvAdditionalHal = &result->additionalHal;
      metdataParams.pRequest_AppControl = in.pRequest_AppControl;
      for (size_t i = 0; i < in.pConfiguration_StreamInfo_P1->size(); i++) {
        if ((*(in.pConfiguration_StreamInfo_P1))[i].pHalImage_P1_Rrzo !=
            nullptr) {
          metdataParams.RrzoSize.push_back(
              (*(in.pConfiguration_StreamInfo_P1))[i]
                  .pHalImage_P1_Rrzo->getImgSize());
        }
      }
      mPolicyTable->pRequestMetadataPolicy->evaluateRequest(metdataParams);
    }

    if (isMain) {
      (*out).mainFrame = result;
      MY_LOGD("build mainFrame -");
    } else {
      switch (dummy) {
        case NOT_DUMMY:
          (*out).subFrames.push_back(result);
          break;
        case POST_DUMMY:
          (*out).postDummyFrames.push_back(result);
          break;
        case PRE_DUMMY:
          (*out).preDummyFrames.push_back(result);
          break;
        default:
          MY_LOGW("Cannot be here");
          break;
      }
    }

    MY_LOGD("build out frame param -");
    return NSCam::OK;
  };
  if (noP2Node) {
    buildOutParam(nullptr, NOT_DUMMY, true);
    MY_LOGD("no p2 node process done");
    return NSCam::OK;
  }

  buildOutParam(featureOut.mainFrame, NOT_DUMMY, true);
  for (auto const& it : featureOut.subFrames) {
    buildOutParam(it, NOT_DUMMY, false);
  }
  for (auto const& it : featureOut.postDummyFrames) {
    buildOutParam(it, POST_DUMMY, false);
  }
  for (auto const& it : featureOut.preDummyFrames) {
    buildOutParam(it, PRE_DUMMY, false);
  }

  return NSCam::OK;
}
