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

#define PIPE_CLASS_TAG "TuningHelper"
#define PIPE_MODULE_TAG "TuningHelper"
#include <featurePipe/common/include/PipeLog.h>
#include <isp_tuning/isp_tuning.h>
#include <memory>
#include <mtkcam/feature/3dnr/util_3dnr.h>
#include <mtkcam/feature/featureCore/featurePipe/streaming/P2CamContext.h>
#include <mtkcam/feature/featureCore/featurePipe/streaming/TuningHelper.h>
#include <mtkcam/feature/utils/p2/P2Trace.h>
#include <mtkcam/feature/utils/p2/P2Util.h>
#include <mtkcam/utils/metadata/hal/mtk_platform_metadata_tag.h>
#include <mtkcam/utils/metadata/IMetadata.h>

using NSCam::Feature::P2Util::P2IOPack;

namespace NSCam {
namespace NSCamFeature {
namespace NSFeaturePipe {

MVOID
updateInputMeta_Raw2Yuv(const TuningHelper::Input& in,
                        NS3Av3::MetaSet_T* inMetaSet) {
  if (in.mScene == TuningHelper::Tuning_Normal) {
    std::shared_ptr<P2CamContext> p2CamContext = getP2CamContext(in.mSensorID);
    if (p2CamContext != NULL &&
        p2CamContext->get3dnr() != NULL) {  // Run only if 3DNR is enabled
      p2CamContext->get3dnr()->updateISPMetadata(&(inMetaSet->halMeta),
                                                 in.mpNr3dTuningInfo);
    }
  } else if (in.mScene == TuningHelper::Tuning_Pure) {
    // TODO(MTK) get profile maybe from meta or custom?   Now just use N3D
    // Preview
    IMetadata::setEntry<MUINT8>(&(inMetaSet->halMeta), MTK_3A_ISP_PROFILE,
                                NSIspTuning::EIspProfile_N3D_Preview);
  }
}

MVOID
updateOutputMeta_Raw2Yuv(const TuningHelper::ExtraMetaParam& metaParam,
                         IMetadata* outHal,
                         IMetadata* /*outApp*/) {
  if (outHal != NULL) {
    if (metaParam.isFDCropValid) {
      IMetadata::setEntry<MRect>(outHal, MTK_P2NODE_FD_CROP_REGION,
                                 metaParam.fdCrop);
    }
  }
}

MBOOL
TuningHelper::process3A_P2A_Raw2Yuv(const Input& in,
                                    NSIoPipe::FrameParams* frameParam,
                                    NSCam::IMetadata* halOut,
                                    NSCam::IMetadata* appOut) {
  if ((in.mp3A == NULL && SUPPORT_3A_HAL) || in.mTuningBuf == NULL) {
    MY_LOGE("Process Tuning fail! p3A(%p), tuningBuf(%p)", in.mp3A.get(),
            in.mTuningBuf.get());
    return MFALSE;
  }

  if (!(in.mTargetTuning.isRRZOin() ^ in.mTargetTuning.isIMGOin())) {
    MY_LOGE(
        "Process Tuning fail! Not support both IMGO/RRZO exist or non-exist. "
        "rrzIn(%d), imgIn(%d)",
        in.mTargetTuning.isRRZOin(), in.mTargetTuning.isIMGOin());
    return MFALSE;
  }

  if (in.mSensorInput.mHalIn == NULL || in.mSensorInput.mAppIn == NULL) {
    MY_LOGE(
        "Can not get Input Metadata from sensor input! halI/O(%p/%p), "
        "appI/O(%p/%p)",
        in.mSensorInput.mHalIn, halOut, in.mSensorInput.mAppIn, appOut);
    return MFALSE;
  }

  {
    P2_CAM_TRACE_BEGIN(TRACE_ADVANCED, "TH(p2a.r2y)::copyInMeta");
    NS3Av3::MetaSet_T inMetaSet;
    NS3Av3::MetaSet_T outMetaSet;
    //
    inMetaSet.appMeta = *(in.mSensorInput.mAppIn);  // copy
    inMetaSet.halMeta = *(in.mSensorInput.mHalIn);  // copy
    P2_CAM_TRACE_END(TRACE_ADVANCED);

    updateInputMeta_Raw2Yuv(in, &inMetaSet);

    // --- prepare P2IO ---
    P2IOPack io;
    if (in.mTargetTuning.isRRZOin()) {
      io.mIMGI.mBuffer = in.mSensorInput.mRRZO;
      io.mFlag |= Feature::P2Util::P2Flag::FLAG_RESIZED;
    } else {
      io.mIMGI.mBuffer = in.mSensorInput.mIMGO;
    }

    if (in.mTargetTuning.isLCSOin()) {
      io.mLCSO.mBuffer = in.mSensorInput.mLCSO;
    }

    if (in.mTuningBuf) {
      io.mTuning.mBuffer = in.mTuningBuf.get();
    }
    // ---  set Isp ---
    P2_CAM_TRACE_BEGIN(TRACE_ADVANCED, "TH(p2a.r2y)::makeFrameParam");
    NS3Av3::TuningParam tuningParam = Feature::P2Util::makeTuningParam(
        in.mP2Pack.mLog, in.mP2Pack, in.mp3A, &inMetaSet, &outMetaSet,
        io.isResized(), in.mTuningBuf, in.mSensorInput.mLCSO);
    *frameParam = Feature::P2Util::makeFrameParams(in.mP2Pack, in.mTag, io,
                                                   in.mP2ObjPtr, tuningParam);
    frameParam->UniqueKey = in.mUniqueKey;
    P2_CAM_TRACE_END(TRACE_ADVANCED);

    P2_CAM_TRACE_BEGIN(TRACE_ADVANCED, "TH(p2a.r2y)::copyOutMeta");
    if (appOut != NULL) {
      *appOut = outMetaSet.appMeta;  // copy
    }
    if (halOut != NULL) {
      *halOut = inMetaSet.halMeta;    // copy
      *halOut += outMetaSet.halMeta;  // copy
    }
    P2_CAM_TRACE_END(TRACE_ADVANCED);
    P2_CAM_TRACE_BEGIN(TRACE_ADVANCED, "TH(p2a.r2y)::updateOutputMeta");
    updateOutputMeta_Raw2Yuv(in.mExtraMetaParam, halOut, appOut);
    P2_CAM_TRACE_END(TRACE_ADVANCED);
    P2_CAM_TRACE_BEGIN(TRACE_ADVANCED, "TH(p2a.r2y)::freeMetaSet");
  }
  P2_CAM_TRACE_END(TRACE_ADVANCED);
  return MTRUE;
}

}  // namespace NSFeaturePipe
}  // namespace NSCamFeature
}  // namespace NSCam
