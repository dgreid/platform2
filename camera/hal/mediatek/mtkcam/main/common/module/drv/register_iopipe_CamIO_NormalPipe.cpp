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

#define LOG_TAG "mtkcam-module"
//
#include <mtkcam/main/common/module/local.h>
//
#include "mtkcam/drv/iopipe/CamIO/V4L2IIOPipe.h"
//
extern "C" mtkcam_module* get_mtkcam_module_iopipe_CamIO_NormalPipe();
REGISTER_MTKCAM_MODULE(MTKCAM_MODULE_ID_DRV_IOPIPE_CAMIO_NORMALPIPE,
                       get_mtkcam_module_iopipe_CamIO_NormalPipe);
