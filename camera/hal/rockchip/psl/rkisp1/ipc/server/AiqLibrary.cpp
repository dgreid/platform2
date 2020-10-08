/*
 * Copyright (C) 2016-2017 Intel Corporation
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
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

#define LOG_TAG "AiqLibrary"

#include <string.h>

#include "AiqLibrary.h"

#include <rk_aiq.h>

#include "IPCCommon.h"
#include "common/UtilityMacros.h"
#include "LogHelper.h"

namespace rockchip {
namespace camera {
AiqLibrary::AiqLibrary()
{
    LOG1("@%s", __FUNCTION__);
}

AiqLibrary::~AiqLibrary()
{
    LOG1("@%s", __FUNCTION__);
}

status_t AiqLibrary::aiq_init(void* pData, int dataSize)
{
    LOG1("@%s, pData:%p, dataSize:%d", __FUNCTION__, pData, dataSize);
    CheckAndLogError((pData == nullptr), UNKNOWN_ERROR, "@%s, pData is nullptr",
                     __FUNCTION__);
    CheckAndLogError((dataSize < sizeof(aiq_init_params)), UNKNOWN_ERROR,
                     "@%s, buffer is small", __FUNCTION__);

    aiq_init_params* params = static_cast<aiq_init_params*>(pData);

    const char* xmlFilePath;
    bool ret = mIpc.serverUnflattenInit(*params, &xmlFilePath);
    CheckAndLogError(ret == false, UNKNOWN_ERROR,
                     "@%s, serverUnflattenInit fails", __FUNCTION__);

    rk_aiq * aiq = rk_aiq_init(xmlFilePath);
    CheckAndLogError((aiq == nullptr), UNKNOWN_ERROR, "@%s, rk_aiq_init failed",
                     __FUNCTION__);

    params->results = reinterpret_cast<uintptr_t>(aiq);

    return OK;
}

status_t AiqLibrary::aiq_deinit(void* pData, int dataSize)
{
    LOG1("@%s, pData:%p, dataSize:%d", __FUNCTION__, pData, dataSize);
    CheckAndLogError((pData == nullptr), UNKNOWN_ERROR, "@%s, pData is nullptr",
                     __FUNCTION__);
    CheckAndLogError((dataSize < sizeof(aiq_deinit_params)), UNKNOWN_ERROR,
                     "@%s, buffer is small", __FUNCTION__);

    aiq_deinit_params* params = static_cast<aiq_deinit_params*>(pData);
    rk_aiq_deinit(reinterpret_cast<rk_aiq*>(params->aiq_handle));

    return OK;
}

status_t AiqLibrary::aiq_misc_run(void* pData, int dataSize)
{
    LOG1("@%s, pData:%p, dataSize:%d", __FUNCTION__, pData, dataSize);
    CheckAndLogError((pData == nullptr), UNKNOWN_ERROR, "@%s, pData is nullptr",
                     __FUNCTION__);
    CheckAndLogError((dataSize < sizeof(misc_isp_run_params)), UNKNOWN_ERROR,
                     "@%s, buffer is small", __FUNCTION__);

    misc_isp_run_params *params = static_cast<misc_isp_run_params*>(pData);

    status_t err = rk_aiq_misc_run(reinterpret_cast<rk_aiq*>(params->aiq_handle), &params->base, &params->results);
    CheckAndLogError((err != 0), UNKNOWN_ERROR,
                     "@%s, rk_aiq_gbce_run failed %d", __FUNCTION__, err);

    return OK;
}

status_t AiqLibrary::statistics_set(void* pData, int dataSize)
{
    LOG1("@%s, pData:%p, dataSize:%d", __FUNCTION__, pData, dataSize);
    CheckAndLogError((pData == nullptr), UNKNOWN_ERROR, "@%s, pData is nullptr",
                     __FUNCTION__);
    CheckAndLogError((dataSize < sizeof(set_statistics_params)), UNKNOWN_ERROR,
                     "@%s, buffer is small", __FUNCTION__);

    set_statistics_params* params = static_cast<set_statistics_params*>(pData);
    set_statistics_params_data* statParams = nullptr;

    bool ret = mIpc.serverUnflattenStat(*params, &statParams);
    CheckAndLogError(ret == false, UNKNOWN_ERROR,
                     "@%s, serverUnflattenStat fails", __FUNCTION__);

    status_t err = rk_aiq_stats_set((rk_aiq*)params->aiq_handle, statParams->input, statParams->sensor_desc);
    CheckAndLogError((err != 0), UNKNOWN_ERROR,
                     "@%s, rk_aiq_statistics_set failed %d", __FUNCTION__, err);

    return OK;
}

status_t AiqLibrary::aiq_ae_run(void* pData, int dataSize)
{
    LOG1("@%s, pData:%p, dataSize:%d", __FUNCTION__, pData, dataSize);
    CheckAndLogError((pData == nullptr), UNKNOWN_ERROR, "@%s, pData is nullptr",
                     __FUNCTION__);
    CheckAndLogError((dataSize < sizeof(ae_run_params)), UNKNOWN_ERROR,
                     "@%s, buffer is small", __FUNCTION__);

    ae_run_params* params = static_cast<ae_run_params*>(pData);

    rk_aiq_ae_input_params* aeParams = nullptr;
    bool ret = mIpc.serverUnflattenAe(*params, &aeParams);
    CheckAndLogError(ret == false, UNKNOWN_ERROR,
                     "@%s, serverUnflattenAe fails", __FUNCTION__);

    status_t err = rk_aiq_ae_run(reinterpret_cast<rk_aiq*>(params->aiq_handle), aeParams, &params->results);
    CheckAndLogError((err != 0), UNKNOWN_ERROR, "@%s, rk_aiq_ae_run failed %d",
                     __FUNCTION__, err);

    return OK;
}

status_t AiqLibrary::aiq_awb_run(void* pData, int dataSize)
{
    LOG1("@%s, pData:%p, dataSize:%d", __FUNCTION__, pData, dataSize);
    CheckAndLogError((pData == nullptr), UNKNOWN_ERROR, "@%s, pData is nullptr",
                     __FUNCTION__);
    CheckAndLogError((dataSize < sizeof(awb_run_params)), UNKNOWN_ERROR,
                     "@%s, buffer is small", __FUNCTION__);

    awb_run_params* params = static_cast<awb_run_params*>(pData);

    rk_aiq_awb_input_params* awbParams = nullptr;
    bool ret = mIpc.serverUnflattenAwb(*params, &awbParams);
    CheckAndLogError(ret == false, UNKNOWN_ERROR,
                     "@%s, serverUnflattenAwb fails", __FUNCTION__);

    status_t err = rk_aiq_awb_run(reinterpret_cast<rk_aiq*>(params->aiq_handle), awbParams, &params->results);
    CheckAndLogError((err != 0), UNKNOWN_ERROR, "@%s, rk_aiq_awb_run failed %d",
                     __FUNCTION__, err);

    return OK;
}

status_t AiqLibrary::aiq_get_version(void* pData, int dataSize)
{
    LOG1("@%s, pData:%p, dataSize:%d", __FUNCTION__, pData, dataSize);
    CheckAndLogError((pData == nullptr), UNKNOWN_ERROR, "@%s, pData is nullptr",
                     __FUNCTION__);
    CheckAndLogError((dataSize < sizeof(rk_aiq_version_params)), UNKNOWN_ERROR,
                     "@%s, buffer is small", __FUNCTION__);

    rk_aiq_version_params* params = static_cast<rk_aiq_version_params*>(pData);

    const char* version = "";//TODO: rk_aiq_get_version();
    strncpy(params->data, version, sizeof(params->data));
    params->size = MIN(strlen(version), sizeof(params->data));
    LOG2("@%s, aiq version:%s, size:%d", __FUNCTION__, version, params->size);

    return OK;
}
} /* namespace camera */
} /* namespace rockchip */
