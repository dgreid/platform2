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

#define LOG_TAG "mtkcam-FDIntentPolicy"

#include <compiler.h>
#include <mtkcam/pipeline/policy/IFaceDetectionIntentPolicy.h>
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
 * Make a function target as a policy - default version
 */
FunctionType_FaceDetectionIntentPolicy makePolicy_FDIntent_Default() {
  return [](fdintent::RequestOutputParams& out,
            fdintent::RequestInputParams const& in) -> int {
    if (!in.hasFDNodeConfigured) {
      out.isFdEnabled = false;
      return OK;
    }

    auto const pMetadata = in.pRequest_AppControl;
    if (CC_UNLIKELY(!pMetadata)) {
      MY_LOGE("null app control input params");
      return -EINVAL;
    }

    IMetadata::IEntry const& entryFdMode =
        pMetadata->entryFor(MTK_STATISTICS_FACE_DETECT_MODE);
    IMetadata::IEntry const& entryFaceScene =
        pMetadata->entryFor(MTK_CONTROL_SCENE_MODE);

    bool FDMetaEn = (!entryFdMode.isEmpty() &&
                     MTK_STATISTICS_FACE_DETECT_MODE_OFF !=
                         entryFdMode.itemAt(0, Type2Type<MUINT8>())) ||
                    (!entryFaceScene.isEmpty() &&
                     MTK_CONTROL_SCENE_MODE_FACE_PRIORITY ==
                         entryFaceScene.itemAt(0, Type2Type<MUINT8>()));

    bool isFDScene = !entryFaceScene.isEmpty() &&
                     MTK_CONTROL_SCENE_MODE_FACE_PRIORITY ==
                         entryFaceScene.itemAt(0, Type2Type<MUINT8>());

    out.hasFDMeta = !entryFdMode.isEmpty() || isFDScene;
    MY_LOGI("has fd meta(%d), FDMetaEn(%d)", out.hasFDMeta, FDMetaEn);

    if (out.hasFDMeta) {
      out.isFdEnabled = FDMetaEn;
      out.isFDMetaEn = FDMetaEn;
    } else {
      out.isFdEnabled = false;
      out.isFDMetaEn = false;
    }

    return OK;
  };
}

};  // namespace policy
};  // namespace pipeline
};  // namespace v3
};  // namespace NSCam
