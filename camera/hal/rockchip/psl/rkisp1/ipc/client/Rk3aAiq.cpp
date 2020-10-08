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

#define LOG_TAG "RK_AIQ_IPC"

#include <rk_aiq.h>
#include <ia_types.h>
#include <utils/Errors.h>

#include "Rk3aAiq.h"
#include "common/UtilityMacros.h"
#include "LogHelper.h"

NAMESPACE_DECLARATION {
Rk3aAiq::Rk3aAiq():
        mInitialized(false)
{
    LOG1("@%s", __FUNCTION__);

    mAiq = reinterpret_cast<uintptr_t>(nullptr);

    mMems = {
        {"/aiqInitShm", sizeof(aiq_init_params), &mMemInit, false},
        {"/aiqDeinitShm", sizeof(aiq_deinit_params), &mMemDeinit, false},
        {"/aiqAeShm", sizeof(ae_run_params), &mMemAe, false},
        {"/aiqAwbShm", sizeof(awb_run_params), &mMemAwb, false},
        {"/aiqMiscShm", sizeof(misc_isp_run_params), &mMemMisc, false},
        {"/aiqStatShm", sizeof(set_statistics_params), &mMemStat, false},
        {"/aiqVersionShm", sizeof(rk_aiq_version_params), &mMemVersion, false}};

    bool success = mCommon.allocateAllShmMems(mMems);
    if (!success) {
        mCommon.releaseAllShmMems(mMems);
        return;
    }

    LOG1("@%s, done", __FUNCTION__);
    mInitialized = true;
}

Rk3aAiq::~Rk3aAiq()
{
    LOG1("@%s", __FUNCTION__);
    mCommon.releaseAllShmMems(mMems);
}

bool Rk3aAiq::init(const char* xmlFilePath)
{
    CheckAndLogError(xmlFilePath == nullptr, false,
                     "@%s, xmlFilePath is nullptr", __FUNCTION__);
    LOG1("@%s, xmlFilePath:%s", __FUNCTION__, xmlFilePath);

    CheckAndLogError(mInitialized == false, false, "@%s, mInitialized is false",
                     __FUNCTION__);

    aiq_init_params* params = static_cast<aiq_init_params*>(mMemInit.mAddr);

    int ret = mIpc.clientFlattenInit(xmlFilePath, params);
    CheckAndLogError(ret == false, false, "@%s, clientFlattenInit fails",
                     __FUNCTION__);

    ret = mCommon.requestSync(IPC_3A_AIQ_INIT, mMemInit.mHandle);
    CheckAndLogError(ret == false, false, "@%s, requestSync fails",
                     __FUNCTION__);

    mAiq = params->results;
    LOG2("@%s, success, aiq:%p\n", __FUNCTION__, reinterpret_cast<rk_aiq*>(mAiq));

    return true;
}

void Rk3aAiq::deinit()
{
    LOG1("@%s", __FUNCTION__);
    CheckAndLogError(mInitialized == false, VOID_VALUE,
                     "@%s, mInitialized is false", __FUNCTION__);
    CheckAndLogError(reinterpret_cast<rk_aiq*>(mAiq) == nullptr, VOID_VALUE,
                     "@%s, mAiq is nullptr", __FUNCTION__);

    aiq_deinit_params* params = static_cast<aiq_deinit_params*>(mMemDeinit.mAddr);
    params->aiq_handle = mAiq;

    int ret = mCommon.requestSync(IPC_3A_AIQ_DEINIT, mMemDeinit.mHandle);
    CheckAndLogError(ret == false, VOID_VALUE, "@%s, requestSync fails",
                     __FUNCTION__);

    mAiq = reinterpret_cast<uintptr_t>(nullptr);
}

status_t Rk3aAiq::aeRun(const rk_aiq_ae_input_params* ae_input_params,
                        rk_aiq_ae_results** ae_results)
{
    LOG1("@%s, ae_input_params:%p, ae_results:%p", __FUNCTION__, ae_input_params, ae_results);
    CheckAndLogError(mInitialized == false, UNKNOWN_ERROR,
                     "@%s, mInitialized is false", __FUNCTION__);
    CheckAndLogError(reinterpret_cast<rk_aiq*>(mAiq) == nullptr, UNKNOWN_ERROR,
                     "@%s, mAiq is nullptr", __FUNCTION__);

    CheckAndLogError((ae_input_params == nullptr), BAD_VALUE,
                     "@%s, ae_input_params is nullptr", __FUNCTION__);
    CheckAndLogError((ae_results == nullptr), BAD_VALUE,
                     "@%s, ae_results is nullptr", __FUNCTION__);

    ae_run_params* params = static_cast<ae_run_params*>(mMemAe.mAddr);

    bool ret = mIpc.clientFlattenAe(mAiq, *ae_input_params, params);
    CheckAndLogError(ret == false, UNKNOWN_ERROR, "@%s, clientFlattenAe fails",
                     __FUNCTION__);

    ret = mCommon.requestSync(IPC_3A_AIQ_AE_RUN, mMemAe.mHandle);
    CheckAndLogError(ret == false, UNKNOWN_ERROR, "@%s, requestSync fails",
                     __FUNCTION__);

    ret = mIpc.clientUnflattenAe(*params, ae_results);
    CheckAndLogError(ret == false, UNKNOWN_ERROR,
                     "@%s, clientUnflattenAe fails", __FUNCTION__);

    return NO_ERROR;
}

status_t Rk3aAiq::awbRun(const rk_aiq_awb_input_params* awb_input_params,
                         rk_aiq_awb_results** awb_results)
{
    LOG1("@%s, awb_input_params:%p, awb_results:%p", __FUNCTION__, awb_input_params, awb_results);
    CheckAndLogError(mInitialized == false, UNKNOWN_ERROR,
                     "@%s, mInitialized is false", __FUNCTION__);
    CheckAndLogError(reinterpret_cast<rk_aiq*>(mAiq) == nullptr, UNKNOWN_ERROR,
                     "@%s, mAiq is nullptr", __FUNCTION__);

    CheckAndLogError((awb_input_params == nullptr), BAD_VALUE,
                     "@%s, awb_input_params is nullptr", __FUNCTION__);
    CheckAndLogError((awb_results == nullptr), BAD_VALUE,
                     "@%s, awb_results is nullptr", __FUNCTION__);

    awb_run_params* params = static_cast<awb_run_params*>(mMemAwb.mAddr);

    bool ret = mIpc.clientFlattenAwb(mAiq, *awb_input_params, params);
    CheckAndLogError(ret == false, UNKNOWN_ERROR, "@%s, clientFlattenAwb fails",
                     __FUNCTION__);

    ret = mCommon.requestSync(IPC_3A_AIQ_AWB_RUN, mMemAwb.mHandle);
    CheckAndLogError(ret == false, UNKNOWN_ERROR, "@%s, requestSync fails",
                     __FUNCTION__);

    ret = mIpc.clientUnflattenAwb(*params, awb_results);
    CheckAndLogError(ret == false, UNKNOWN_ERROR,
                     "@%s, clientUnflattenAwb fails", __FUNCTION__);

    return NO_ERROR;
}

status_t Rk3aAiq::miscRun(const rk_aiq_misc_isp_input_params* misc_input_params,
                          rk_aiq_misc_isp_results** misc_results)
{
    LOG1("@%s, misc_input_params:%p, misc_results:%p", __FUNCTION__, misc_input_params, misc_results);
    CheckAndLogError(mInitialized == false, UNKNOWN_ERROR,
                     "@%s, mInitialized is false", __FUNCTION__);
    CheckAndLogError(reinterpret_cast<rk_aiq*>(mAiq) == nullptr, UNKNOWN_ERROR,
                     "@%s, mAiq is nullptr", __FUNCTION__);

    CheckAndLogError((misc_input_params == nullptr), BAD_VALUE,
                     "@%s, misc_input_params is nullptr", __FUNCTION__);
    CheckAndLogError((misc_results == nullptr), BAD_VALUE,
                     "@%s, misc_results is nullptr", __FUNCTION__);

    misc_isp_run_params* params = static_cast<misc_isp_run_params*>(mMemMisc.mAddr);

    bool ret = mIpc.clientFlattenMisc(mAiq, *misc_input_params, params);
    CheckAndLogError(ret == false, UNKNOWN_ERROR,
                     "@%s, clientFlattenMisc fails", __FUNCTION__);

    ret = mCommon.requestSync(IPC_3A_AIQ_MISC_ISP_RUN, mMemMisc.mHandle);
    CheckAndLogError(ret == false, UNKNOWN_ERROR, "@%s, requestSync fails",
                     __FUNCTION__);

    ret = mIpc.clientUnflattenMisc(*params, misc_results);
    CheckAndLogError(ret == false, UNKNOWN_ERROR,
                     "@%s, clientUnflattenMisc fails", __FUNCTION__);

    return NO_ERROR;
}

status_t Rk3aAiq::statisticsSet(const rk_aiq_statistics_input_params* input_params,
                                const rk_aiq_exposure_sensor_descriptor* sensor_desc)
{
    LOG1("@%s, input_params:%p", __FUNCTION__, input_params);
    CheckAndLogError(mInitialized == false, UNKNOWN_ERROR,
                     "@%s, mInitialized is false", __FUNCTION__);
    CheckAndLogError(reinterpret_cast<rk_aiq*>(mAiq) == nullptr, UNKNOWN_ERROR,
                     "@%s, mAiq is nullptr", __FUNCTION__);
    /*TODO: we allow null now.*/
    // CheckAndLogError((input_params == nullptr), BAD_VALUE, "@%s, input_params
    // is nullptr", __FUNCTION__);

    set_statistics_params* params = static_cast<set_statistics_params*>(mMemStat.mAddr);
    set_statistics_params_data stat_input_params;
    stat_input_params.input = const_cast<rk_aiq_statistics_input_params*>(input_params);
    stat_input_params.sensor_desc = const_cast<rk_aiq_exposure_sensor_descriptor*>(sensor_desc);

    bool ret = mIpc.clientFlattenStat(mAiq, stat_input_params, params);
    CheckAndLogError(ret == false, UNKNOWN_ERROR,
                     "@%s, clientFlattenStat fails", __FUNCTION__);

    ret = mCommon.requestSync(IPC_3A_AIQ_STATISTICS_SET, mMemStat.mHandle);
    CheckAndLogError(ret == false, UNKNOWN_ERROR, "@%s, requestSync fails",
                     __FUNCTION__);

    return NO_ERROR;
}

const char* Rk3aAiq::getVersion(void)
{
    LOG1("@%s", __FUNCTION__);
    CheckAndLogError(mInitialized == false, "null",
                     "@%s, mInitialized is false", __FUNCTION__);

    int ret = mCommon.requestSync(IPC_3A_AIQ_GET_VERSION, mMemVersion.mHandle);
    CheckAndLogError(ret == false, "null", "@%s, requestSync fails",
                     __FUNCTION__);

    rk_aiq_version_params* params = static_cast<rk_aiq_version_params*>(mMemVersion.mAddr);
    return params->data;
}

bool Rk3aAiq::isInitialized() const
{
    LOG1("@%s", __FUNCTION__);

    return mAiq ? true : false;
}
} NAMESPACE_DECLARATION_END
