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

#include <mtkcam/utils/TuningUtils/FileDumpNamingRule.h>
#include "CommonRule.h"

namespace NSCam {
namespace TuningUtils {

void genFileName_YUV(char* pFilename,
                     int nFilename,
                     const FILE_DUMP_NAMING_HINT* pHint,
                     YUV_PORT type,
                     const char* pUserString) {
  if (pUserString == NULL) {
    pUserString = "";
  }
  int t;
  char* ptr = pFilename;
  if (property_get_int32("vendor.debug.enable.normalAEB", 0)) {
    t = MakePrefix(ptr, nFilename, pHint->UniqueKey, pHint->FrameNo,
                   pHint->RequestNo, pHint->EvValue);
  } else {
    t = MakePrefix(ptr, nFilename, pHint->UniqueKey, pHint->FrameNo,
                   pHint->RequestNo);
  }
  ptr += t;
  nFilename -= t;

  if (pHint->SensorDev >= 0) {
    t = snprintf(ptr, nFilename, "-%s", SENSOR_DEV_TO_STRING(pHint->SensorDev));
    ptr += t;
    nFilename -= t;
  }

  if (type != YUV_PORT_NULL) {
    if (type == YUV_PORT_IMG2O) {
      t = snprintf(ptr, nFilename, "-img2o");
    } else if (type == YUV_PORT_IMG3O) {
      t = snprintf(ptr, nFilename, "-img3o");
    } else if (type == YUV_PORT_WROTO) {
      t = snprintf(ptr, nFilename, "-wroto");
    } else if (type == YUV_PORT_WDMAO) {
      t = snprintf(ptr, nFilename, "-wdmao");
    } else {
      t = snprintf(ptr, nFilename, "-undef");
    }
    ptr += t;
    nFilename -= t;
  }
  if (*(pHint->additStr) != '\0') {
    t = snprintf(ptr, nFilename, "-%s", pHint->additStr);
    ptr += t;
    nFilename -= t;
  }

  if (*pUserString != '\0') {
    t = snprintf(ptr, nFilename, "-%s", pUserString);
    ptr += t;
    nFilename -= t;
  }

  t = snprintf(ptr, nFilename, "__%dx%d_8_s0", pHint->ImgWidth,
               pHint->ImgHeight);
  ptr += t;
  nFilename -= t;

  t = snprintf(ptr, nFilename, ".%s",
               IMAGE_FORMAT_TO_FILE_EXT(pHint->ImgFormat));
  ptr += t;
  nFilename -= t;
}

}  // namespace TuningUtils
}  // namespace NSCam
