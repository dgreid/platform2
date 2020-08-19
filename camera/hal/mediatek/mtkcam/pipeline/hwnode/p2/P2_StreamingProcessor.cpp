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

#include <algorithm>
#include <mtkcam/utils/debug/P2_DebugControl.h>
#define P2_CLASS_TAG StreamingProcessor
#define P2_TRACE TRACE_STREAMING_PROCESSOR
#include "P2_LogHeader.h"

#include "camera/hal/mediatek/mtkcam/pipeline/hwnode/p2/P2_StreamingProcessor.h"
#include "P2_Util.h"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#define P2_STREAMING_THREAD_NAME "p2_streaming"
#define VAR_STREAMING_PAYLOAD "p2_streaming_payload"

#define IDLE_WAIT_TIME_MS 66

using NSCam::NSCamFeature::VarMap;
using NSCam::NSCamFeature::NSFeaturePipe::FeaturePipeParam;
using NSCam::NSCamFeature::NSFeaturePipe::IStreamingFeaturePipe;
using NSCam::NSCamFeature::NSFeaturePipe::PathType;
using NSCam::NSCamFeature::NSFeaturePipe::SFPIOMap;
using NSCam::NSCamFeature::NSFeaturePipe::SFPOutput;
using NSCam::NSCamFeature::NSFeaturePipe::SFPSensorInput;
using NSCam::NSCamFeature::NSFeaturePipe::SFPSensorTuning;
using NSCam::v3::tryGetMetadata;

namespace P2 {

StreamingProcessor::StreamingProcessor()
    : Processor(P2_STREAMING_THREAD_NAME),
      m3dnrDebugLevel(0),
      mDebugDrawCropMask(0) {
  MY_LOG_FUNC_ENTER();
  this->setIdleWaitMS(IDLE_WAIT_TIME_MS);

  MY_LOG_FUNC_EXIT();
}

StreamingProcessor::~StreamingProcessor() {
  MY_LOG_S_FUNC_ENTER(mLog);
  this->uninit();
  MY_LOG_S_FUNC_EXIT(mLog);
}

MBOOL StreamingProcessor::onInit(const P2InitParam& param) {
  ILog log = param.mP2Info.mLog;
  MY_LOG_S_FUNC_ENTER(log);
  P2_CAM_TRACE_NAME(TRACE_DEFAULT, "P2_Streaming:init()");

  MBOOL ret = MFALSE;

  mP2Info = param.mP2Info;
  mLog = mP2Info.mLog;
  mDebugDrawCropMask =
      property_get_int32("vendor.debug.camera.drawcrop.mask", 0);
  ret = initFeaturePipe(mP2Info.getConfigInfo()) && init3A();
  if (ret) {
    if ((mP2Info.getConfigInfo().mUsageHint.m3DNRMode &
         (NSCam::NR3D::E3DNR_MODE_MASK_UI_SUPPORT |
          NSCam::NR3D::E3DNR_MODE_MASK_HAL_FORCE_SUPPORT)) != 0) {
      init3DNR();
    }
  } else {
    uninitFeaturePipe();
    uninit3A();
  }

  MY_LOG_S_FUNC_EXIT(log);
  return ret;
}

MVOID StreamingProcessor::onUninit() {
  MY_LOG_S_FUNC_ENTER(mLog);
  P2_CAM_TRACE_NAME(TRACE_DEFAULT, "P2_Streaming:uninit()");
  uninit3DNR();
  uninitFeaturePipe();
  uninit3A();
  MY_LOG_S_FUNC_EXIT(mLog);
}

MVOID StreamingProcessor::onThreadStart() {
  MY_LOG_S_FUNC_ENTER(mLog);
  P2_CAM_TRACE_NAME(TRACE_DEFAULT, "P2_Streaming:threadStart()");
  MY_LOG_S_FUNC_EXIT(mLog);
}

MVOID StreamingProcessor::onThreadStop() {
  MY_LOG_S_FUNC_ENTER(mLog);
  P2_CAM_TRACE_NAME(TRACE_DEFAULT, "P2_Streaming:threadStop()");
  MY_LOG_S_FUNC_EXIT(mLog);
}

MBOOL StreamingProcessor::onConfig(const P2ConfigParam& param) {
  MY_LOG_S_FUNC_ENTER(mLog);
  MBOOL ret = MTRUE;
  P2_CAM_TRACE_NAME(TRACE_DEFAULT, "P2_Streaming:config()");
  if (needReConfig(mP2Info.getConfigInfo(), param.mP2Info.getConfigInfo())) {
    std::lock_guard<std::mutex> _lock(mPayloadMutex);
    if (mPayloadList.size()) {
      MY_S_LOGE(mLog, "Config called before p2 is empty, size=%zu",
                mPayloadList.size());
      ret = MFALSE;
    } else {
      uninitFeaturePipe();
      ret = initFeaturePipe(param.mP2Info.getConfigInfo());
    }
  }
  if (ret) {
    mP2Info = param.mP2Info;
    ret = mFeaturePipe->config(mP2Info.getConfigInfo().mStreamConfigure);
  }

  MY_LOG_S_FUNC_EXIT(mLog);
  return ret;
}

MBOOL StreamingProcessor::makeRequestPacks(
    const vector<std::shared_ptr<P2Request>>& requests,
    vector<std::shared_ptr<P2RequestPack>>* rReqPacks) const {
  const ILog& log = requests.empty() ? mLog : requests.front()->mLog;
  TRACE_S_FUNC_ENTER(log);
  MBOOL debugLog = (log.getLogLevel() >= 2);
  // index by output stream
  class MergeHelper {
   public:
    MBOOL isInputSubsetOf(const ILog& log,
                          const MBOOL printLog,
                          const MergeHelper& base) {
      for (const auto& myInput : mReq->mImg) {
        if (mReq->mImg[myInput.first] == nullptr) {
          continue;
        }
        MY_S_LOGD_IF(printLog, log, "myInput(%d)(%s)", myInput.first,
                     P2Img::getName(myInput.first));
        // key: ID_IMG
        if (myInput.first == IN_RESIZED || myInput.first == IN_RESIZED_2 ||
            myInput.first == IN_FULL || myInput.first == IN_FULL_2 ||
            myInput.first == IN_LCSO || myInput.first == IN_LCSO_2) {
          if (base.mReq->mImg[myInput.first] == nullptr) {
            MY_S_LOGD_IF(printLog, log,
                         "myInput(%d)(%s) can not be found in base request !",
                         myInput.first, P2Img::getName(myInput.first));
            return MFALSE;
          }
        }
      }
      return MTRUE;
    }
    MergeHelper(const std::shared_ptr<P2Request>& pReq,
                const MUINT32 outputIndex)
        : mReq(pReq), mOutIndex(outputIndex) {}
    MergeHelper(const std::shared_ptr<P2Request>& pReq, const ID_IMG imgId)
        : mReq(pReq), mId(imgId) {}

   public:
    std::shared_ptr<P2Request> mReq = nullptr;
    MUINT32 mOutIndex = -1;
    MBOOL merged = false;
    ID_IMG mId = OUT_YUV;
  };

  vector<MergeHelper> outputMap;
  for (const auto& req : requests) {
    if (debugLog) {
      req->dump();
    }

    if (!req->isValidMeta(IN_APP) ||
        !(req->isValidMeta(IN_P1_HAL) || req->isValidMeta(IN_P1_HAL_2))) {
      MY_LOGE("Meta check failed: inApp(%d) inHal(%d) inHal2(%d)",
              req->isValidMeta(IN_APP), req->isValidMeta(IN_P1_HAL),
              req->isValidMeta(IN_P1_HAL_2));
    }
    if (!req->hasInput() || !req->hasOutput()) {
      MY_LOGE("req I/O Failed! hasI/O(%d/%d)", req->hasInput(),
              req->hasOutput());
      continue;
    }

    MUINT32 n = req->mImgOutArray.size();
    for (MUINT32 i = 0; i < n; ++i) {
      MergeHelper outReq(req, i);
      outputMap.push_back(outReq);
    }

    if (req->mImg[OUT_FD] != nullptr) {
      MergeHelper outReq(req, OUT_FD);
      outputMap.push_back(outReq);
    }
  }

  // sort by number of inputs
  std::sort(outputMap.begin(), outputMap.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.mReq->mImg.size() > rhs.mReq->mImg.size();
            });

  // merge requests
  // Current limitation : being merged request must has only 1 output buffer....
  int n = outputMap.size();
  for (int base = 0; base < n; ++base) {
    if (outputMap[base].merged) {
      continue;
    }
    MY_S_LOGD_IF(debugLog, log, "outputMap[%d] creates new pack", base);
    std::shared_ptr<P2RequestPack> reqPack = std::make_shared<P2RequestPack>(
        log, outputMap[base].mReq, mP2Info.getConfigInfo().mAllSensorID);
    for (int target = n - 1; target > base; --target) {
      MY_S_LOGD_IF(debugLog, log, "checking target outputMap[%d]", target);
      MergeHelper& output = outputMap[target];
      if (!output.merged &&
          output.isInputSubsetOf(log, debugLog, outputMap[base])) {
        MY_S_LOGD_IF(debugLog, log, "target outputMap[%d] is subset of [%d]",
                     target, base);
        reqPack->addOutput(output.mReq, output.mOutIndex);
        outputMap[target].merged = true;
      }
    }
    rReqPacks->push_back(reqPack);
  }

  MY_S_LOGD_IF(MTRUE, log, "#Requests(%zu) merged into #RequestsPacks(%zu)",
               requests.size(), rReqPacks->size());
  TRACE_S_FUNC_EXIT(log);
  return (rReqPacks->size() > 0);
}

MBOOL StreamingProcessor::prepareInputs(
    const std::shared_ptr<Payload>& payload) const {
  const ILog& log = payload->mLog;
  MBOOL res = MTRUE;
  TRACE_S_FUNC_ENTER(log);
  for (const auto& partialPayload : payload->mPartialPayloads) {
    auto& reqPack = partialPayload->mRequestPack;
    auto& inputs = reqPack->mInputs;
    for (auto& it : inputs) {
      it.setUseLMV(MTRUE);
      res = (prepare3DNR(&it, log) &&
             // ISP tuning
             prepareISPTuning(&it, log));
      if (!res) {
        return MFALSE;
      }
      // Feature Params
      prepareFeatureParam(&it, log);
    }
  }
  TRACE_S_FUNC_EXIT(log);
  return MTRUE;
}

MBOOL StreamingProcessor::prepareOutputs(
    const std::shared_ptr<Payload>& payload) const {
  P2_CAM_TRACE_CALL(TRACE_ADVANCED);
  TRACE_S_FUNC_ENTER(payload->mLog);

  for (auto& partialPayload : payload->mPartialPayloads) {
    auto& reqPack = partialPayload->mRequestPack;
    auto& inputs = reqPack->mInputs;
    float zoomRatio, mapping_ratio;
    MRect cropRect_control;
    MSize inRRZOSize;
    for (auto& in : inputs) {
      if (in.isResized()) {
        std::shared_ptr<P2Request>& request = in.mRequest;
        P2MetaSet metaSet = request->getMetaSet();
        MSize sensorSize;
        inRRZOSize = in.mIMGI->getIImageBufferPtr()->getImgSize();
        MY_LOGD("in rrzo size %dx%d", inRRZOSize.w, inRRZOSize.h);
        if (!tryGetMetadata<MRect>(&metaSet.mInApp, MTK_SCALER_CROP_REGION,
                                   &cropRect_control)) {
          MY_LOGE("p2 can't get scaler crop region");
        }
        if (!tryGetMetadata<MSize>(&metaSet.mInHal, MTK_HAL_REQUEST_SENSOR_SIZE,
                                   &sensorSize)) {
          MY_LOGE("p2 can't get sensor size");
        }
        mapping_ratio = sensorSize.w * 1.0f / inRRZOSize.w;
        zoomRatio = sensorSize.w * 1.0f / cropRect_control.s.w;
      }
    }
    for (auto& it : reqPack->mOutputs) {
      for (auto& out : it.second) {
        // Crop
        const std::shared_ptr<Cropper> cropper =
            reqPack->mMainRequest->getCropper(out.getSensorId());
        auto& in = inputs[reqPack->mSensorInputMap[out.getSensorId()]];
        MUINT32 cropFlag = 0;
        cropFlag |= in.isResized() ? Cropper::USE_RESIZED : 0;
        cropFlag |= in.useLMV() ? Cropper::USE_EIS_12 : 0;
        cropFlag |= in.useCropRatio() ? Cropper::USE_CROP_RATIO : 0;
        float cropRatio = in.useCropRatio() ? in.getCropRatio() : 0.0f;
        MINT32 dmaConstrainFlag =
            (out.isMDPOutput()) ? DMACONSTRAIN_NONE : DMACONSTRAIN_2BYTEALIGN;
        dmaConstrainFlag |= DMACONSTRAIN_NOSUBPIXEL;
        MRectF cropF;
        if (out.mImg->getTransform() != 0) {  // PortraitRotation
          MY_LOGD("p2s transform %d", out.mImg->getTransform());
          MSize noTransformSize;
          MSize transformSize = out.mImg->getTransformSize();
          noTransformSize.w = transformSize.h;
          noTransformSize.h = transformSize.w;
          cropF =
              cropper->calcViewAngleF(payload->mLog, noTransformSize, cropFlag,
                                      cropRatio, dmaConstrainFlag);
          MRectF originCrop = cropF;
          cropF.s.w = originCrop.s.h * originCrop.s.h / originCrop.s.w;
          cropF.s.h = originCrop.s.h;
          cropF.p.x = (originCrop.s.w - cropF.s.w) / 2 + originCrop.p.x;
          cropF.p.y = originCrop.p.y;
        } else {  // normal flow
          if (zoomRatio <= 1.0) {
            cropF = cropper->calcViewAngleF(
                payload->mLog, out.mImg->getTransformSize(), cropFlag,
                cropRatio, dmaConstrainFlag);
          } else {
            MRectF mapping_cropRect_control;
            mapping_cropRect_control.p.x = cropRect_control.p.x / mapping_ratio;
            mapping_cropRect_control.p.y = cropRect_control.p.y / mapping_ratio;
            mapping_cropRect_control.s.w = cropRect_control.s.w / mapping_ratio;
            mapping_cropRect_control.s.h = cropRect_control.s.h / mapping_ratio;
            cropF =
                cropper->applyViewRatio(payload->mLog, mapping_cropRect_control,
                                        out.mImg->getTransformSize());
          }
        }
        MY_LOGD("p2s crop info (%f) (%f_%f)(%fx%f)", zoomRatio, cropF.p.x,
                cropF.p.y, cropF.s.w, cropF.s.h);
        out.mCrop = cropF;
        out.mDMAConstrainFlag = dmaConstrainFlag;

        // PQ
        if (out.mP2Obj.toPtrTable().hasPQ) {
          const P2Pack& p2Pack = in.mRequest->mP2Pack;
          P2Util::xmakeDpPqParam(p2Pack, out, payload->mpFdData);
        }
        // Set FD Crop
        if (out.isFD()) {
          MRect activeCrop = cropper->toActive(cropF, in.isResized());
          in.mFeatureParam.setVar<MRect>(VAR_FD_CROP_ACTIVE_REGION, activeCrop);
        }
      }
    }
  }
  TRACE_S_FUNC_EXIT(payload->mLog);
  return MTRUE;
}

MVOID StreamingProcessor::releaseResource(
    const vector<std::shared_ptr<P2Request>>& requests, MUINT32 res) const {
  if (!requests.empty()) {
    std::shared_ptr<P2Request> firstReq = requests.front();
    firstReq->beginBatchRelease();
    for (auto&& req : requests) {
      req->releaseResource(res);
    }
    firstReq->endBatchRelease();
  }
}

MVOID StreamingProcessor::makeSFPIOOuts(const std::shared_ptr<Payload>& payload,
                                        const ERequestPath& path,
                                        FeaturePipeParam* featureParam) const {
  TRACE_S_FUNC_ENTER(payload->mLog);
  std::unordered_map<MUINT32, std::shared_ptr<P2Request>>& paths =
      payload->mReqPaths[path];
  for (MUINT32 sensorID : mP2Info.getConfigInfo().mAllSensorID) {
    if (paths.find(sensorID) == paths.end()) {
      continue;
    }
    std::shared_ptr<P2Request>& request = paths[sensorID];
    MBOOL found = MFALSE;
    for (const auto& partialPayload : payload->mPartialPayloads) {
      auto& reqPack = partialPayload->mRequestPack;
      if (reqPack->contains(request)) {
        vector<P2Util::SimpleIn>& inputs = reqPack->mInputs;
        vector<P2Util::SimpleOut>& outputs = reqPack->mOutputs[request.get()];
        SFPIOMap sfpIO;
        // input tuning
        for (const auto& in : inputs) {
          SFPSensorTuning sensorTuning;
          if (in.isResized()) {
            sensorTuning.addFlag(SFPSensorTuning::Flag::FLAG_RRZO_IN);
          } else {
            sensorTuning.addFlag(SFPSensorTuning::Flag::FLAG_IMGO_IN);
          }
          if (isValid(in.mLCEI)) {
            sensorTuning.addFlag(SFPSensorTuning::Flag::FLAG_LCSO_IN);
          }
          sfpIO.addInputTuning(in.getSensorId(), sensorTuning);
        }
        // outputs
        for (const auto& out : outputs) {
          auto getOutTargetType = [&](const P2Util::SimpleOut& out) {
            if (ERequestPath::ePhysic == path) {
              return SFPOutput::OutTargetType::OUT_TARGET_PHYSICAL;
            }
            if (ERequestPath::eLarge == path) {
              return SFPOutput::OutTargetType::OUT_TARGET_UNKNOWN;
            }
            if (out.isRecord()) {
              return SFPOutput::OutTargetType::OUT_TARGET_RECORD;
            }
            if (out.isFD()) {
              return SFPOutput::OutTargetType::OUT_TARGET_FD;
            }
            if (out.isDisplay()) {
              return SFPOutput::OutTargetType::OUT_TARGET_DISPLAY;
            }
            return SFPOutput::OutTargetType::OUT_TARGET_UNKNOWN;
          };
          SFPOutput sfpOut(out.mImg->getIImageBufferPtr(),
                           out.mImg->getTransform(), getOutTargetType(out));
          sfpOut.mCropRect = out.mCrop;
          sfpOut.mDMAConstrainFlag = out.mDMAConstrainFlag;
          sfpOut.mCropDstSize = out.mImg->getTransformSize();
          sfpOut.mpPqParam = out.mP2Obj.toPtrTable().pqParam;
#if MTK_DP_ENABLE
          sfpOut.mpDpPqParam = out.mP2Obj.toPtrTable().pqWDMA;
#endif
          sfpIO.addOutput(sfpOut);
        }

        // metadata
        sfpIO.mHalOut = request->getMetaPtr(OUT_HAL);
        sfpIO.mAppOut = request->getMetaPtr(OUT_APP);

        switch (path) {
          case ERequestPath::eGeneral:
            sfpIO.mPathType = PathType::PATH_GENERAL;
            featureParam->mSFPIOManager.addGeneral(sfpIO);
            break;
          case ERequestPath::ePhysic:
            sfpIO.mPathType = PathType::PATH_PHYSICAL;
            featureParam->mSFPIOManager.addPhysical(sensorID, sfpIO);
            break;
          case ERequestPath::eLarge:
            sfpIO.mPathType = PathType::PATH_LARGE;
            featureParam->mSFPIOManager.addLarge(sensorID, sfpIO);
            break;
          default:
            MY_S_LOGE(payload->mLog, "unknow path(%d)", path);
        }

        found = MTRUE;
        break;
      }
    }

    if (!found) {
      MY_S_LOGE(payload->mLog, "can not find path(%d) for sensor(%d) !!", path,
                sensorID);
    }
  }
  TRACE_S_FUNC_EXIT(payload->mLog);
}

MBOOL StreamingProcessor::makeSFPIOMgr(
    const std::shared_ptr<Payload>& payload) const {
  TRACE_S_FUNC_ENTER(payload->mLog);

  FeaturePipeParam& featureParam = *(payload->getMainFeaturePipeParam());

  // add sensor input
  std::unordered_map<MUINT32, SFPSensorInput> sensorInputs;
  for (const auto& partialPayload : payload->mPartialPayloads) {
    auto& reqPack = partialPayload->mRequestPack;
    auto& inputs = reqPack->mInputs;
    for (auto& in : inputs) {
      MUINT32 sID = in.getSensorId();
      if (in.isResized()) {
        sensorInputs[sID].mRRZO = in.mIMGI->getIImageBufferPtr();
      } else {
        sensorInputs[sID].mIMGO = in.mIMGI->getIImageBufferPtr();
      }
      sensorInputs[sID].mLCSO =
          (isValid(in.mLCEI)) ? in.mLCEI->getIImageBufferPtr() : nullptr;
      sensorInputs[sID].mPrvRSSO =
          (isValid(in.mPreRSSO)) ? in.mPreRSSO->getIImageBufferPtr() : nullptr;
      sensorInputs[sID].mCurRSSO =
          (isValid(in.mRSSO)) ? in.mRSSO->getIImageBufferPtr() : nullptr;

      sensorInputs[sID].mHalIn = in.mRequest->getMetaPtr(IN_P1_HAL, sID);
      sensorInputs[sID].mAppIn = in.mRequest->getMetaPtr(IN_APP, sID);
      sensorInputs[sID].mAppDynamicIn = in.mRequest->getMetaPtr(IN_P1_APP, sID);
    }
  }

  for (auto& it : sensorInputs) {
    featureParam.mSFPIOManager.addInput(it.first, it.second);
  }

  makeSFPIOOuts(payload, ERequestPath::eGeneral, &featureParam);
  makeSFPIOOuts(payload, ERequestPath::ePhysic, &featureParam);
  makeSFPIOOuts(payload, ERequestPath::eLarge, &featureParam);

  TRACE_S_FUNC_EXIT(payload->mLog);
  return MTRUE;
}

std::shared_ptr<StreamingProcessor::Payload> StreamingProcessor::makePayLoad(
    const vector<std::shared_ptr<P2Request>>& requests,
    const vector<std::shared_ptr<P2RequestPack>>& reqPacks) {
  const ILog& log = requests.empty() ? mLog : requests.front()->mLog;
  TRACE_S_FUNC_ENTER(log);
  if (requests.empty() || reqPacks.empty()) {
    MY_S_LOGE(log, "empty reqs(%d) reqPacks(%d)!!", requests.empty(),
              reqPacks.empty());
    return nullptr;
  }

  MUINT32 masterID = requests.front()->mP2Pack.getFrameData().mMasterSensorID;

  auto s = std::dynamic_pointer_cast<StreamingProcessor>(shared_from_this());
  auto payload =
      std::make_shared<StreamingProcessor::Payload>(s, log, masterID);

  payload->addRequests(requests);
  payload->addRequestPacks(reqPacks);

  TRACE_S_FUNC_EXIT(log);
  return payload;
}

MBOOL StreamingProcessor::onEnque(
    const vector<std::shared_ptr<P2Request>>& requests) {
  MY_LOGI("StreamingProcessor::onEnque");
  P2_CAM_TRACE_CALL(TRACE_DEFAULT);
  const ILog& log = requests.empty() ? mLog : requests.front()->mLog;
  TRACE_S_FUNC_ENTER(log);
  vector<std::shared_ptr<P2RequestPack>> requestPacks;
  std::shared_ptr<Payload> payload = nullptr;

  P2_CAM_TRACE_BEGIN(TRACE_ADVANCED,
                     "P2_Streaming:onEnque->makeReqPacks_PayLoad");
  MBOOL ret = makeRequestPacks(requests, &requestPacks);
  payload = makePayLoad(requests, requestPacks);
  ret &= (payload != nullptr);
  P2_CAM_TRACE_END(TRACE_ADVANCED);

  if (!ret) {
    MY_LOGE("make request pack or payload failed !!");
    releaseResource(requests, P2Request::RES_ALL);
    return MFALSE;
  }

  P2_CAM_TRACE_BEGIN(TRACE_ADVANCED,
                     "P2_Streaming:onEnque->->prepareInputsInfo");
  ret &= ret && checkFeaturePipeParamValid(payload) && prepareInputs(payload) &&
         prepareOutputs(payload);
  P2_CAM_TRACE_END(TRACE_ADVANCED);
  if (!ret) {
    MY_S_LOGE(log, "prepare inputs or rsso or output failed !!");
    return MFALSE;
  }

  releaseResource(requests, P2Request::RES_IN_IMG);

  if (!processP2(payload)) {
    MY_LOGE("processP2 failed !!");
    return MFALSE;
  }

  TRACE_S_FUNC_EXIT(log);
  return MTRUE;
}
MBOOL StreamingProcessor::checkFeaturePipeParamValid(
    const std::shared_ptr<Payload>& payload) {
  MBOOL ret = MFALSE;
  FeaturePipeParam* pFPP = payload->getMainFeaturePipeParam();
  if (pFPP != nullptr) {
    ret = MTRUE;
  } else {
    MY_S_LOGE(payload->mLog, "checkFeaturePipeParamValid return false.");
  }
  return ret;
}

MVOID StreamingProcessor::onNotifyFlush() {
  MY_LOG_S_FUNC_ENTER(mLog);
  P2_CAM_TRACE_NAME(TRACE_DEFAULT, "P2_Streaming:notifyFlush()");
  if (mFeaturePipe) {
    mFeaturePipe->flush();
  }
  MY_LOG_S_FUNC_EXIT(mLog);
}

MVOID StreamingProcessor::onWaitFlush() {
  MY_LOG_S_FUNC_ENTER(mLog);
  P2_CAM_TRACE_NAME(TRACE_DEFAULT, "P2_Streaming:waitFlush()");
  waitFeaturePipeDone();
  MY_LOG_S_FUNC_EXIT(mLog);
}

MVOID StreamingProcessor::onIdle() {
  MY_LOG_S_FUNC_ENTER(mLog);
  MY_LOG_S_FUNC_EXIT(mLog);
}

IStreamingFeaturePipe::UsageHint StreamingProcessor::getFeatureUsageHint(
    const P2ConfigInfo& config) {
  TRACE_S_FUNC_ENTER(mLog);
  IStreamingFeaturePipe::UsageHint pipeUsage;
  switch (config.mP2Type) {
    case P2_PHOTO:
    case P2_PREVIEW:
      pipeUsage.mMode = IStreamingFeaturePipe::USAGE_P2A_FEATURE;
      break;
    case P2_CAPTURE:
      pipeUsage.mMode = IStreamingFeaturePipe::USAGE_P2A_PASS_THROUGH;
      break;
    case P2_TIMESHARE_CAPTURE:
      pipeUsage.mMode =
          IStreamingFeaturePipe::USAGE_P2A_PASS_THROUGH_TIME_SHARING;
      break;
    case P2_HS_VIDEO:
      pipeUsage.mMode = IStreamingFeaturePipe::USAGE_FULL;
      MY_S_LOGE(mLog, "Slow Motion should NOT use StreamingProcessor!!");
      break;
    case P2_VIDEO:
      pipeUsage.mMode = IStreamingFeaturePipe::USAGE_FULL;
      break;
    case P2_DUMMY:
      pipeUsage.mMode = IStreamingFeaturePipe::USAGE_DUMMY;
      MY_S_LOGD(mLog, "Using Dummy streaming feature pipe");
      break;
    case P2_UNKNOWN:
    default:
      pipeUsage.mMode = IStreamingFeaturePipe::USAGE_FULL;
      break;
  }

  pipeUsage.mStreamingSize = config.mUsageHint.mStreamingSize;
  if (pipeUsage.mStreamingSize.w == 0 || pipeUsage.mStreamingSize.h == 0) {
    MY_S_LOGW(mLog, "no size in UsageHint");
  }
  // TODO(mtk): pipeUsage.mVendorCusSize = ((usage.mUsageMode ==
  // P2FeatureNode::USAGE_RECORD) && (usage.mVideoSize.size() >
  // usage.mPreviewSize.size())) ? usage.mVideoSize : usage.mPreviewSize;
  pipeUsage.mVendorCusSize.w = ((pipeUsage.mStreamingSize.w / 2) & (~1));
  pipeUsage.mVendorCusSize.h = ((pipeUsage.mStreamingSize.h / 2) & (~1));

  pipeUsage.m3DNRMode = config.mUsageHint.m3DNRMode;
  pipeUsage.mUseTSQ = config.mUsageHint.mUseTSQ;
  pipeUsage.mAllSensorIDs = config.mAllSensorID;
  pipeUsage.mDynamicTuning = config.mUsageHint.mDynamicTuning;
  pipeUsage.mResizedRawMap = config.mUsageHint.mResizedRawMap;
  pipeUsage.mSensorModule = config.mUsageHint.mSensorModule;

  pipeUsage.mOutCfg.mMaxOutNum = config.mUsageHint.mOutCfg.mMaxOutNum;
  pipeUsage.mOutCfg.mHasPhysical = config.mUsageHint.mOutCfg.mHasPhysical;
  pipeUsage.mOutCfg.mHasLarge = config.mUsageHint.mOutCfg.mHasLarge;

  TRACE_S_FUNC_EXIT(mLog);
  return pipeUsage;
}

MBOOL StreamingProcessor::needReConfig(const P2ConfigInfo& oldConfig,
                                       const P2ConfigInfo& newConfig) {
  TRACE_S_FUNC_ENTER(mLog);
  MBOOL ret = MFALSE;
  const P2UsageHint& oldHint = oldConfig.mUsageHint;
  const P2UsageHint& newHint = newConfig.mUsageHint;
  if (newHint.mStreamingSize != oldHint.mStreamingSize) {
    ret = MTRUE;
  }
  TRACE_S_FUNC_EXIT(mLog);
  return ret;
}

MBOOL StreamingProcessor::initFeaturePipe(const P2ConfigInfo& config) {
  TRACE_S_FUNC_ENTER(mLog);
  MBOOL ret = MFALSE;

  mPipeUsageHint = getFeatureUsageHint(config);
  P2_CAM_TRACE_BEGIN(TRACE_DEFAULT, "P2_Streaming:FeaturePipe create");
  mFeaturePipe = IStreamingFeaturePipe::createInstance(
      mP2Info.getConfigInfo().mMainSensorID, mPipeUsageHint);
  P2_CAM_TRACE_END(TRACE_DEFAULT);
  if (mFeaturePipe == nullptr) {
    MY_S_LOGE(mLog, "OOM: cannot create FeaturePipe");
  } else {
    P2_CAM_TRACE_BEGIN(TRACE_DEFAULT, "P2_Streaming:FeaturePipe init");
    ret = mFeaturePipe->init(getName());
    P2_CAM_TRACE_END(TRACE_DEFAULT);
    for (MUINT32 id : mP2Info.getConfigInfo().mAllSensorID) {
      if (id != mP2Info.getConfigInfo().mMainSensorID) {
        mFeaturePipe->addMultiSensorID(id);
      }
    }
    if (!ret) {
      MY_S_LOGE(mLog, "FeaturePipe init failed");
    }
  }
  TRACE_S_FUNC_EXIT(mLog);
  return ret;
}

MVOID StreamingProcessor::uninitFeaturePipe() {
  TRACE_S_FUNC_ENTER(mLog);
  if (mFeaturePipe) {
    mFeaturePipe->uninit(getName());
    mFeaturePipe = nullptr;
  }
  TRACE_S_FUNC_EXIT(mLog);
}

MBOOL StreamingProcessor::init3A() {
  TRACE_S_FUNC_ENTER(mLog);
  MBOOL ret = MTRUE;
  for (MUINT32 sensorID : mP2Info.getConfigInfo().mAllSensorID) {
    P2_CAM_TRACE_FMT_BEGIN(TRACE_DEFAULT, "P2_Streaming:3A(%u) create",
                           sensorID);
    std::shared_ptr<IHal3A> pHal3A = nullptr;
    MAKE_Hal3A(
        pHal3A, [](IHal3A* p) { p->destroyInstance(P2_STREAMING_THREAD_NAME); },
        sensorID, P2_STREAMING_THREAD_NAME);
    mHal3AMap[sensorID] = pHal3A;
    P2_CAM_TRACE_END(TRACE_DEFAULT);
    if (mHal3AMap[sensorID] == nullptr) {
      MY_S_LOGE(mLog, "OOM: cannot create Hal3A(%u)", sensorID);
      ret = MFALSE;
    }
  }
  TRACE_S_FUNC_EXIT(mLog);
  return ret;
}

MVOID StreamingProcessor::uninit3A() {
  TRACE_S_FUNC_ENTER(mLog);
  mHal3AMap.clear();
  TRACE_S_FUNC_EXIT(mLog);
}

MVOID StreamingProcessor::waitFeaturePipeDone() {
  TRACE_S_FUNC_ENTER(mLog);
  std::unique_lock<std::mutex> _lock(mPayloadMutex);
  while (mPayloadList.size()) {
    mPayloadCondition.wait(_lock);
  }
  TRACE_S_FUNC_EXIT(mLog);
}

MVOID StreamingProcessor::incPayloadCount(const ILog& log) {
  TRACE_FUNC_ENTER();
  TRACE_S_FUNC(log, "count=%d", mPayloadCount++);
  TRACE_FUNC_EXIT();
}

MVOID StreamingProcessor::decPayloadCount(const ILog& log) {
  TRACE_FUNC_ENTER();
  TRACE_S_FUNC(log, "count=%d", mPayloadCount--);
  TRACE_FUNC_EXIT();
}

MVOID StreamingProcessor::incPayload(const std::shared_ptr<Payload>& payload) {
  TRACE_S_FUNC_ENTER(payload->mLog);
  std::lock_guard<std::mutex> _lock(mPayloadMutex);
  mPayloadList.push_back(payload);
  FeaturePipeParam* sFPP = payload->getMainFeaturePipeParam();
  if (sFPP != nullptr) {
    sFPP->setVar<std::shared_ptr<Payload>>(VAR_STREAMING_PAYLOAD, payload);
  } else {
    MY_S_LOGE(payload->mLog, "Error, getMainFeaturePipeParam return null !!");
  }
  TRACE_S_FUNC_EXIT(payload->mLog);
}

MBOOL StreamingProcessor::decPayload(FeaturePipeParam* param,
                                     const std::shared_ptr<Payload>& payload,
                                     MBOOL checkOrder) {
  TRACE_S_FUNC_ENTER(payload->mLog);
  std::lock_guard<std::mutex> _lock(mPayloadMutex);
  MBOOL ret = MFALSE;
  auto it = find(mPayloadList.begin(), mPayloadList.end(), payload);
  if (it != mPayloadList.end()) {
    if (checkOrder && it != mPayloadList.begin()) {
      MY_S_LOGW(payload->mLog, "callback out of order");
    }
    mPayloadList.erase(it);
    mPayloadCondition.notify_all();
    ret = MTRUE;
  } else {
    MY_S_LOGE(payload->mLog, "Payload not released: invalid data=%p list=%zu",
              payload.get(), mPayloadList.size());
  }

  param->clearVar<std::shared_ptr<Payload>>(VAR_STREAMING_PAYLOAD);
  payload->getMainFeaturePipeParam()->clearVar<std::shared_ptr<Payload>>(
      VAR_STREAMING_PAYLOAD);

  TRACE_S_FUNC_EXIT(payload->mLog);
  return ret;
}

MVOID StreamingProcessor::prepareFeatureParam(P2Util::SimpleIn* input,
                                              const ILog& log) const {
  TRACE_S_FUNC_ENTER(log);

  prepareCommon(input, log);

  TRACE_S_FUNC_EXIT(log);
}

MBOOL StreamingProcessor::prepareCommon(P2Util::SimpleIn* input,
                                        const ILog& log) const {
  TRACE_S_FUNC_ENTER(log);
  const std::shared_ptr<P2Request>& request = input->mRequest;
  const std::shared_ptr<Cropper> cropper =
      request->getCropper(input->getSensorId());
  FeaturePipeParam& featureParam = input->mFeatureParam;
  IStreamingFeaturePipe::eAppMode mode =
      IStreamingFeaturePipe::APP_PHOTO_PREVIEW;
  featureParam.mDumpType = request->mDumpType;

  switch (request->mP2Pack.getFrameData().mAppMode) {
    case MTK_FEATUREPIPE_PHOTO_PREVIEW:
      mode = IStreamingFeaturePipe::APP_PHOTO_PREVIEW;
      break;
    case MTK_FEATUREPIPE_VIDEO_PREVIEW:
      mode = IStreamingFeaturePipe::APP_VIDEO_PREVIEW;
      break;
    case MTK_FEATUREPIPE_VIDEO_RECORD:
      mode = IStreamingFeaturePipe::APP_VIDEO_RECORD;
      break;
    case MTK_FEATUREPIPE_VIDEO_STOP:
      mode = IStreamingFeaturePipe::APP_VIDEO_STOP;
      break;
    default:
      mode = IStreamingFeaturePipe::APP_PHOTO_PREVIEW;
      break;
  }

  featureParam.setVar<IStreamingFeaturePipe::eAppMode>(VAR_APP_MODE, mode);
  featureParam.setVar<MINT64>(VAR_P1_TS,
                              request->mP2Pack.getSensorData().mP1TS);
  featureParam.setVar<MBOOL>(VAR_IMGO_2IMGI_ENABLE, !input->isResized());
  featureParam.setVar<MRect>(VAR_IMGO_2IMGI_P1CROP, cropper->getP1Crop());

  TRACE_S_FUNC_EXIT(log);
  return MTRUE;
}

MBOOL StreamingProcessor::prepareISPTuning(P2Util::SimpleIn* input,
                                           const ILog& log) const {
  TRACE_S_FUNC_ENTER(log);
  (void)log;
  if (!mP2Info.getConfigInfo().mUsageHint.mDynamicTuning) {
    std::shared_ptr<P2Request>& request = input->mRequest;
    P2MetaSet metaSet = request->getMetaSet();
    input->mTuning = P2Util::xmakeTuning(
        request->mP2Pack, *input, mHal3AMap.at(input->getSensorId()), &metaSet);
    if (!input->mTuning.pRegBuf) {
      return MFALSE;
    }
    request->updateMetaSet(metaSet);
  }
  TRACE_S_FUNC_EXIT(log);
  return MTRUE;
}

MBOOL StreamingProcessor::processP2(const std::shared_ptr<Payload>& payload) {
  MY_LOGI("StreamingProcessor::processP2");
  P2_CAM_TRACE_CALL(TRACE_ADVANCED);
  TRACE_S_FUNC_ENTER(payload->mLog);
  MBOOL ret = MFALSE;
  if (payload->mLog.getLogLevel() >= 2) {
    payload->print();
  }
  incPayload(payload);

  if (!makeSFPIOMgr(payload)) {
    MY_LOGE("make SFPIO failed !!");
    return MFALSE;
  }

  FeaturePipeParam* pFPP = payload->getMainFeaturePipeParam();
  pFPP->mCallback = sFPipeCB;
  pFPP->mP2Pack = payload->getMainRequest()->mP2Pack;

  P2_CAM_TRACE_BEGIN(TRACE_ADVANCED, "P2_Streaming:drv enq");
  ret = mFeaturePipe->enque(*pFPP);
  P2_CAM_TRACE_END(TRACE_ADVANCED);
  if (!ret) {
    MY_S_LOGW(payload->mLog, "enque failed");
    for (const auto& pp : payload->mPartialPayloads) {
      pp->mRequestPack->updateBufferResult(MFALSE);
    }
    decPayload(payload->getMainFeaturePipeParam(), payload, MFALSE);
  }
  TRACE_S_FUNC_EXIT(payload->mLog);
  return ret;
}

MVOID StreamingProcessor::onFPipeCB(FeaturePipeParam::MSG_TYPE msg,
                                    FeaturePipeParam const& param,
                                    const std::shared_ptr<Payload>& payload) {
  TRACE_S_FUNC_ENTER(payload->mLog, "callback msg: %d", msg);
  P2_CAM_TRACE_NAME(TRACE_ADVANCED, "P2_Streaming:onFPipeCB()");
  std::shared_ptr<P2Request> oneRequest =
      payload->mPartialPayloads.front()->mRequestPack->mMainRequest;
  if (oneRequest != nullptr) {
    oneRequest->beginBatchRelease();
  } else {
    MY_S_LOGE(payload->mLog, "cannot get P2Request to do batchRelease!!");
  }
  if (msg == FeaturePipeParam::MSG_FRAME_DONE) {
    for (const auto& pp : payload->mPartialPayloads) {
      pp->mRequestPack->updateBufferResult(param.mQParams.mDequeSuccess);
      if (mP2Info.getConfigInfo().mUsageHint.mDynamicTuning) {
        pp->mRequestPack->updateMetaResult(MTRUE);
      }
    }
    P2_CAM_TRACE_BEGIN(TRACE_ADVANCED, "MSG_FRAME_DONE->earlyRelease");
    for (const auto& pp : payload->mPartialPayloads) {
      pp->mRequestPack->earlyRelease(P2Util::RELEASE_ALL);
    }
    P2_CAM_TRACE_END(TRACE_ADVANCED);
  } else if (msg == FeaturePipeParam::MSG_DISPLAY_DONE) {
    for (const auto& pp : payload->mPartialPayloads) {
      pp->mRequestPack->updateBufferResult(param.mQParams.mDequeSuccess);
      pp->mRequestPack->earlyRelease(P2Util::RELEASE_DISP);
    }
  } else if (msg == FeaturePipeParam::MSG_RSSO_DONE) {
    for (const auto& pp : payload->mPartialPayloads) {
      pp->mRequestPack->updateBufferResult(param.mQParams.mDequeSuccess);
      pp->mRequestPack->earlyRelease(P2Util::RELEASE_RSSO);
    }
  } else if (msg == FeaturePipeParam::MSG_FD_DONE) {
    for (const auto& pp : payload->mPartialPayloads) {
      pp->mRequestPack->updateBufferResult(param.mQParams.mDequeSuccess);
      pp->mRequestPack->earlyRelease(P2Util::RELEASE_FD);
    }
  }
  if (oneRequest != nullptr) {
    oneRequest->endBatchRelease();
  }
  TRACE_S_FUNC_EXIT(payload->mLog);
}

MBOOL StreamingProcessor::sFPipeCB(FeaturePipeParam::MSG_TYPE msg,
                                   FeaturePipeParam* param) {
  TRACE_FUNC_ENTER();
  MBOOL ret = MTRUE;
  P2_CAM_TRACE_BEGIN(TRACE_ADVANCED, "get payload");
  std::shared_ptr<Payload> payload =
      param->getVar<std::shared_ptr<Payload>>(VAR_STREAMING_PAYLOAD, nullptr);
  P2_CAM_TRACE_END(TRACE_ADVANCED);
  if (payload == nullptr) {
    MY_LOGW("invalid payload = nullptr");
    ret = MFALSE;
  } else if (payload->mParent == nullptr) {
    MY_LOGW("invalid payload(%p), parent = nullptr", payload.get());
    payload = nullptr;
  } else {
    payload->mParent->onFPipeCB(msg, *param, payload);
  }
  if (msg == FeaturePipeParam::MSG_FRAME_DONE && payload != nullptr) {
    P2_CAM_TRACE_BEGIN(TRACE_ADVANCED, "payload->mParent->decPayload");
    payload->mParent->decPayload(param, payload, MTRUE);
    P2_CAM_TRACE_END(TRACE_ADVANCED);
  }
  TRACE_FUNC_EXIT();
  return ret;
}

StreamingProcessor::P2RequestPack::P2RequestPack(
    const ILog& log,
    const std::shared_ptr<P2Request>& pReq,
    const vector<MUINT32>& sensorIDs)
    : mLog(log) {
  mMainRequest = pReq;
  mRequests.insert(pReq);
  MUINT32 reqSensorID = pReq->getSensorID();

  std::shared_ptr<P2InIDMap> inIDMap = pReq->getIDMap();
  for (const auto& sensorID : sensorIDs) {
    if (isValid(pReq->mImg[inIDMap->getImgID(sensorID, IN_RESIZED)]) ||
        isValid(pReq->mImg[inIDMap->getImgID(sensorID, IN_FULL)])) {
      P2Util::SimpleIn in(sensorID, pReq);
      if (isValid(pReq->mImg[inIDMap->getImgID(sensorID, IN_RESIZED)])) {
        in.setISResized(MTRUE);
        in.mIMGI =
            std::move(pReq->mImg[inIDMap->getImgID(sensorID, IN_RESIZED)]);
      } else if (isValid(pReq->mImg[inIDMap->getImgID(sensorID, IN_FULL)])) {
        in.setISResized(MFALSE);
        in.mIMGI = std::move(pReq->mImg[inIDMap->getImgID(sensorID, IN_FULL)]);
      }
      if (isValid(pReq->mImg[inIDMap->getImgID(sensorID, IN_LCSO)])) {
        in.mLCEI = std::move(pReq->mImg[inIDMap->getImgID(sensorID, IN_LCSO)]);
      }
      if (isValid(pReq->mImg[inIDMap->getImgID(sensorID, IN_RSSO)])) {
        in.mRSSO = std::move(pReq->mImg[inIDMap->getImgID(sensorID, IN_RSSO)]);
      }
      mSensorInputMap[sensorID] = mInputs.size();
      mInputs.push_back(in);
    }
  }

  vector<P2Util::SimpleOut> vOut;
  // FD buffer is stored in mImg map

  if (isValid(pReq->mImg[OUT_FD])) {
    P2Util::SimpleOut out(reqSensorID, pReq, pReq->mImg[OUT_FD]);
    out.setIsFD(MTRUE);
    vOut.push_back(out);
  }
  for (auto& it : pReq->mImgOutArray) {
    P2Util::SimpleOut out(reqSensorID, pReq, it);
    vOut.push_back(out);
  }
  mOutputs[pReq.get()] = vOut;
}

StreamingProcessor::P2RequestPack::~P2RequestPack() {}

MVOID StreamingProcessor::P2RequestPack::addOutput(
    const std::shared_ptr<P2Request>& pReq, const MUINT32 outputIndex) {
  if (mRequests.find(pReq) != mRequests.end()) {
    if (pReq == mMainRequest) {
      MY_S_LOGD_IF(mLog.getLogLevel() >= 3, mLog,
                   "already containes this request, ignore");
    } else {
      MY_S_LOGE(mLog,
                "Currently not support request, with more than 1 output, "
                "merged to other request!!");
    }
  } else {
    mRequests.insert(pReq);

    if (outputIndex >= 0) {
      P2Util::SimpleOut out(pReq->getSensorID(), pReq,
                            pReq->mImgOutArray[outputIndex]);
      vector<P2Util::SimpleOut> vOut = {out};
      mOutputs[pReq.get()] = vOut;
    } else {
      MY_S_LOGE(mLog,
                "outputIndex < 0, maybe non app yuv desire merged --> Not "
                "Support currently.");
    }

    if (pReq->mImg[OUT_FD] != nullptr || pReq->mImg[OUT_JPEG_YUV] != nullptr ||
        pReq->mImg[OUT_THN_YUV] != nullptr) {
      MY_S_LOGE(mLog,
                "Currently not support OUT FD/JpegYUV /thumbYuv in non-first "
                "IOMap !!  Need Check it !!!");
    }
  }
}

MVOID StreamingProcessor::P2RequestPack::updateBufferResult(MBOOL result) {
  for (const auto& outSet : mOutputs) {
    for (const auto& out : outSet.second) {
      if (out.mImg != nullptr) {
        out.mImg->updateResult(result);
      }
    }
  }
}

MVOID StreamingProcessor::P2RequestPack::updateMetaResult(MBOOL result) {
  for (auto&& req : mRequests) {
    req->updateMetaResult(result);
  }
}

MVOID StreamingProcessor::P2RequestPack::dropRecord() {
  for (const auto& outSet : mOutputs) {
    for (const auto& out : outSet.second) {
      if (out.mImg != nullptr && out.mImg->isRecord()) {
        out.mImg->updateResult(MFALSE);
      }
    }
  }
}

MVOID StreamingProcessor::P2RequestPack::earlyRelease(MUINT32 mask) {
  for (auto& in : mInputs) {
    if (mask & P2Util::RELEASE_ALL) {
      in.releaseAllImg();
    }
    if (mask & P2Util::RELEASE_DISP) {
      in.mIMGI = nullptr;
      in.mLCEI = nullptr;
    }
    if (mask & P2Util::RELEASE_RSSO) {
      in.mRSSO = nullptr;
      in.mPreRSSO = nullptr;
    }
  }
  for (auto& outSet : mOutputs) {
    for (auto& out : outSet.second) {
      if ((mask & P2Util::RELEASE_ALL) ||
          ((mask & P2Util::RELEASE_DISP) && out.mImg->isDisplay()) ||
          ((mask & P2Util::RELEASE_FD) && out.isFD())) {
        if (out.mImg != nullptr) {
          out.mImg = nullptr;
        }
      }
    }
  }
  if (mask & P2Util::RELEASE_ALL) {
    for (auto&& req : mRequests) {
      req->releaseResource(P2Request::RES_META);
    }
  }
}

MBOOL StreamingProcessor::P2RequestPack::contains(
    const std::shared_ptr<P2Request>& pReq) const {
  return mRequests.find(pReq) != mRequests.end();
}

P2Util::SimpleIn* StreamingProcessor::P2RequestPack::getInput(
    MUINT32 sensorID) {
  for (auto& in : mInputs) {
    if (in.getSensorId() == sensorID) {
      return &in;
    }
  }
  return nullptr;
}

StreamingProcessor::PartialPayload::PartialPayload(
    const ILog& mainLog, const std::shared_ptr<P2RequestPack>& pReqPack)
    : mRequestPack(pReqPack), mLog(mainLog) {}

StreamingProcessor::PartialPayload::~PartialPayload() {}

MVOID StreamingProcessor::PartialPayload::print() const {
  // TODO(mtk): print partialPayload : mInputs & mOutputs
}

StreamingProcessor::Payload::Payload(std::shared_ptr<StreamingProcessor> parent,
                                     const ILog& mainLog,
                                     MUINT32 masterSensorId)
    : mParent(parent), mLog(mainLog), mMasterID(masterSensorId) {}

StreamingProcessor::Payload::~Payload() {
  if (mpFdData != nullptr) {
    TRACE_FUNC("!!warn: mpFdData(%p) to be freed", mpFdData);
    delete mpFdData;
    mpFdData = nullptr;
  }

  std::shared_ptr<P2Request> mainReq = getMainRequest();
  if (mainReq != nullptr) {
    mainReq->beginBatchRelease();
    mPartialPayloads.clear();
    mReqPaths.clear();
    mainReq->releaseResource(P2Request::RES_ALL);
    mainReq->endBatchRelease();
  }
}

MVOID StreamingProcessor::Payload::addRequests(
    const vector<std::shared_ptr<P2Request>>& requests) {
  for (const auto& it : requests) {
    if (it->isPhysic()) {
      mReqPaths[ERequestPath::ePhysic][it->getSensorID()] = it;
      continue;
    }
    if (it->isLarge()) {
      mReqPaths[ERequestPath::eLarge][it->getSensorID()] = it;
      continue;
    }
    mReqPaths[ERequestPath::eGeneral][it->getSensorID()] = it;
  }
}

MVOID StreamingProcessor::Payload::addRequestPacks(
    const vector<std::shared_ptr<P2RequestPack>>& reqPacks) {
  for (const auto& it : reqPacks) {
    std::shared_ptr<PartialPayload> partialPayload =
        std::make_shared<PartialPayload>(mLog, it);
    mPartialPayloads.push_back(partialPayload);
  }
}

MBOOL StreamingProcessor::Payload::prepareFdData(const P2Info& p2Info,
                                                 IFDContainer* pFDContainer) {
  MBOOL ret = MFALSE;
  const P2ConfigInfo confingInfo = p2Info.getConfigInfo();
  const P2PlatInfo* pPlatInfoPtr = p2Info.getPlatInfo();
  if (pPlatInfoPtr == nullptr) {
    TRACE_FUNC("!!warn: pPlatInfoPtr is NULL");
  }

  TRACE_FUNC("param(pPlatInfoPtr=%p, pFDContainer=%p), mFdData=%p",
             pPlatInfoPtr, pFDContainer, mpFdData);

  return ret;
}

std::shared_ptr<P2Request> StreamingProcessor::Payload::getMainRequest() {
  // main request order: General Request-> Physic_1"
  std::shared_ptr<P2Request> request =
      getPathRequest(ERequestPath::eGeneral, mMasterID);
  if (request == nullptr) {
    request = getPathRequest(ERequestPath::ePhysic, mMasterID);
  }
  if (request == nullptr) {
    request = getPathRequest(ERequestPath::eLarge, mMasterID);
  }
  MY_S_LOGE_IF(request == nullptr, mLog, "can not find main request !!");
  return request;
}

std::shared_ptr<P2Request> StreamingProcessor::Payload::getPathRequest(
    ERequestPath path, const MUINT32& sensorID) {
  if (mReqPaths.find(path) == mReqPaths.end()) {
    return nullptr;
  }
  if (mReqPaths[path].find(sensorID) == mReqPaths[path].end()) {
    return nullptr;
  }
  return mReqPaths[path][sensorID];
}

std::shared_ptr<StreamingProcessor::P2RequestPack>
StreamingProcessor::Payload::getRequestPack(
    const std::shared_ptr<P2Request>& pReq) {
  for (const auto& partialPayload : mPartialPayloads) {
    if (partialPayload->mRequestPack->contains(pReq)) {
      return partialPayload->mRequestPack;
    }
  }
  MY_S_LOGE(mLog, "req(%p) not belong to any P2RequestPack!!", pReq.get());
  return nullptr;
}

FeaturePipeParam* StreamingProcessor::Payload::getMainFeaturePipeParam() {
  std::shared_ptr<P2Request> mainRequest = getMainRequest();
  if (mainRequest != nullptr) {
    std::shared_ptr<P2RequestPack> reqPack = getRequestPack(mainRequest);
    P2Util::SimpleIn* in =
        (reqPack != nullptr) ? reqPack->getInput(mMasterID) : nullptr;
    if (in != nullptr) {
      return &(in->mFeatureParam);
    }
  }
  MY_S_LOGE(mLog, "can not find main feature param !!");
  return nullptr;
}

MVOID StreamingProcessor::Payload::print() const {
  TRACE_S_FUNC_ENTER(mLog);
  MY_S_LOGD(mLog, "MasterID = %d", mMasterID);
  for (const auto& partialPayload : mPartialPayloads) {
    partialPayload->print();
  }
  TRACE_S_FUNC_EXIT(mLog);
}

}  // namespace P2
