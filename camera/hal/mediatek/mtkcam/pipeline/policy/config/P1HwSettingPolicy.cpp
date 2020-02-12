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

#define LOG_TAG "mtkcam-P1HwSettingPolicy"

#include <memory>
#include <mtkcam/pipeline/policy/IP1HwSettingPolicy.h>
//
#include <mtkcam/utils/hw/HwInfoHelper.h>
#include <mtkcam/utils/LogicalCam/IHalLogicalDeviceList.h>
//
#include "MyUtils.h"
#include <mtkcam/utils/std/common.h>
#include <mtkcam/utils/std/compiler.h>
#include <vector>
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
 * Make a function target - default version
 */
FunctionType_Configuration_P1HwSettingPolicy
makePolicy_Configuration_P1HwSetting_Default() {
  return [](std::vector<P1HwSetting>* pvOut,
            std::vector<SensorSetting> const* pSensorSetting,
            StreamingFeatureSetting const* pStreamingFeatureSetting,
            PipelineNodesNeed const* pPipelineNodesNeed,
            PipelineStaticInfo const* pPipelineStaticInfo,
            PipelineUserConfiguration const* pPipelineUserConfiguration)
             -> int {
    auto const& pParsedAppImageStreamInfo =
        pPipelineUserConfiguration->pParsedAppImageStreamInfo;

    for (size_t idx = 0; idx < pPipelineStaticInfo->sensorIds.size(); idx++) {
      P1HwSetting setting;
      std::shared_ptr<NSCamHW::HwInfoHelper> infohelper =
          std::make_shared<NSCamHW::HwInfoHelper>(
              pPipelineStaticInfo->sensorIds[idx]);
      bool ret = infohelper != nullptr && infohelper->updateInfos() &&
                 infohelper->queryPixelMode((*pSensorSetting)[idx].sensorMode,
                                            (*pSensorSetting)[idx].sensorFps,
                                            &setting.pixelMode);
      if (CC_UNLIKELY(!ret)) {
        MY_LOGE("idx : %zu, queryPixelMode error", idx);
        return UNKNOWN_ERROR;
      }
      MSize const& sensorSize = (*pSensorSetting)[idx].sensorSize;

      MINT32 rrzoMaxWidth = 0;
      infohelper->quertMaxRrzoWidth(&rrzoMaxWidth);

#define CHECK_TARGET_SIZE(_msg_, _size_) \
  MY_LOGD("%s: target size(%dx%d)", _msg_, _size_.w, _size_.h);

      // estimate preview yuv max size
      MSize const max_preview_size = refine::align_2(
          MSize(rrzoMaxWidth, rrzoMaxWidth * sensorSize.h / sensorSize.w));
      //
      CHECK_TARGET_SIZE("max_rrzo_size", max_preview_size);
      MSize maxYuvStreamSize(0, 0);
      MSize largeYuvStreamSize(0, 0);

      for (const auto& n : pParsedAppImageStreamInfo->vAppImage_Output_Proc) {
        if (!(n.second->getUsageForConsumer() & GRALLOC_USAGE_HW_TEXTURE) &&
            !(n.second->getUsageForConsumer() & GRALLOC_USAGE_HW_COMPOSER) &&
            !(n.second->getUsageForConsumer() & GRALLOC_USAGE_HW_VIDEO_ENCODER))
          continue;

        MSize const streamSize = (n.second)->getImgSize();
        // if stream's size is suitable to use rrzo
        if (streamSize.w <= max_preview_size.w &&
            streamSize.h <= max_preview_size.h) {
          refine::not_smaller_than(&maxYuvStreamSize, streamSize);
        } else {
          refine::not_smaller_than(&largeYuvStreamSize, streamSize);
        }
      }
      if (maxYuvStreamSize.w == 0 || maxYuvStreamSize.h == 0) {
        MY_LOGW(
            "all yuv size is larger than max rrzo size, set default rrzo to "
            "1280x720");
        maxYuvStreamSize.w = 1280;
        maxYuvStreamSize.h = 720;
      }
      // use resized raw if
      // 1. raw sensor
      // 2. some streams need this
#if 1  // just test imgo, remove for test , need check later
      if (infohelper->isRaw()) {
        //
        // currently, should always enable resized raw due to some reasons...
        //
        // initial value
        MSize target_rrzo_size = (pParsedAppImageStreamInfo->hasVideo4K)
                                     ? largeYuvStreamSize
                                     : maxYuvStreamSize;

        if (target_rrzo_size.w < sensorSize.w / 4 &&
            target_rrzo_size.h < sensorSize.h / 4)
          target_rrzo_size = MSize(sensorSize.w / 4, sensorSize.h / 4);

        // align sensor aspect ratio
        if (target_rrzo_size.w * sensorSize.h >
            target_rrzo_size.h * sensorSize.w) {
          ALIGNX(target_rrzo_size.w, 4);
          target_rrzo_size.h = target_rrzo_size.w * sensorSize.h / sensorSize.w;
          ALIGNX(target_rrzo_size.h, 4);
        } else {
          ALIGNX(target_rrzo_size.h, 4);
          target_rrzo_size.w = target_rrzo_size.h * sensorSize.w / sensorSize.h;
          ALIGNX(target_rrzo_size.w, 4);
        }
        CHECK_TARGET_SIZE("sensor size", sensorSize);
        CHECK_TARGET_SIZE("target rrzo stream", target_rrzo_size);
        // apply limitations
        //  1. lower bounds
        {}  //  2. upper bounds
        {
          {
            refine::not_larger_than(&target_rrzo_size, max_preview_size);
            CHECK_TARGET_SIZE("preview upper bound limitation",
                              target_rrzo_size);
          }
          refine::not_larger_than(&target_rrzo_size, sensorSize);
          CHECK_TARGET_SIZE("sensor size upper bound limitation",
                            target_rrzo_size);
        }
        MY_LOGD("rrzo size(%dx%d)", target_rrzo_size.w, target_rrzo_size.h);
        //
        setting.rrzoSize = target_rrzo_size;
        // check hw limitation with pixel mode & stride
        if (!infohelper->getRrzoFmt(10, &setting.rrzoFormat) ||
            !infohelper->alignRrzoHwLimitation(target_rrzo_size, sensorSize,
                                               &setting.rrzoSize) ||
            !infohelper->alignPass1HwLimitation(
                setting.pixelMode, setting.rrzoFormat, MFALSE,
                &setting.rrzoSize, &setting.rrzoStride)) {
          MY_LOGE("wrong params about rrzo");
          return BAD_VALUE;
        }
        MY_LOGI("rrzo size(%dx%d) stride %zu", setting.rrzoSize.w,
                setting.rrzoSize.h, setting.rrzoStride);
      }
      //

#endif
      // use full raw, if
      // 1. jpeg stream (&& not met BW limit)
      // 2. raw stream
      // 3. opaque stream
      // 4. or stream's size is beyond rrzo's limit
      // 5. PDENode needs full raw if NOT dualcam
      // 6. have capture node
      bool useImgo =
          (pParsedAppImageStreamInfo->pAppImage_Jpeg
               .get() /*&& ! mPipelineConfigParams.mbHasRecording*/) ||
          pParsedAppImageStreamInfo->pAppImage_Input_Yuv.get() ||
          !!largeYuvStreamSize ||
          pPipelineStaticInfo->isType3PDSensorWithoutPDE ||
          pPipelineNodesNeed->needP2CaptureNode ||
          property_get_int32("vendor.debug.feature.forceEnableIMGO", 0);

      if (useImgo) {
        setting.imgoSize = sensorSize;
        // check hw limitation with pixel mode & stride
        if (!infohelper->getImgoFmt(10, &setting.imgoFormat) ||
            !infohelper->alignPass1HwLimitation(
                setting.pixelMode, setting.imgoFormat, MTRUE, &setting.imgoSize,
                &setting.imgoStride)) {
          MY_LOGE("wrong params about imgo");
          return BAD_VALUE;
        }
        MY_LOGI("imgo size(%dx%d) stride %zu", setting.imgoSize.w,
                setting.imgoSize.h, setting.imgoStride);
      } else {
        setting.imgoSize = MSize(0, 0);
      }

      // determine rsso size
      setting.rssoSize = MSize(288, 512);

      pvOut->push_back(setting);
    }

    return OK;
  };
}

};  // namespace policy
};  // namespace pipeline
};  // namespace v3
};  // namespace NSCam
