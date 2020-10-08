/*
 * Copyright (C) 2017 Intel Corporation.
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

#define LOG_TAG "Rk3aAiq"

#include "LogHelper.h"
#include "Rk3aAiq.h"

NAMESPACE_DECLARATION {
Rk3aAiq::Rk3aAiq():
        mAiq(nullptr)
{
    LOG1("@%s", __FUNCTION__);
}

Rk3aAiq::~Rk3aAiq()
{
    LOG1("@%s", __FUNCTION__);
}

bool Rk3aAiq::init(const char* xmlFilePath)
{
    LOG1("@%s", __FUNCTION__);

    mAiq = rk_aiq_init(xmlFilePath);
    CheckAndLogError(mAiq == nullptr, false, "@%s, rk_aiq_init fails",
                     __FUNCTION__);

    return true;
}

void Rk3aAiq::deinit()
{
    LOG1("@%s", __FUNCTION__);
    CheckAndLogError(mAiq == nullptr, VOID_VALUE, "@%s, mAiq is nullptr",
                     __FUNCTION__);

    rk_aiq_deinit(mAiq);
    mAiq = nullptr;
}

status_t Rk3aAiq::aeRun(const rk_aiq_ae_input_params* ae_input_params,
                        rk_aiq_ae_results** ae_results)
{
    LOG1("@%s", __FUNCTION__);
    CheckAndLogError(mAiq == nullptr, UNKNOWN_ERROR, "@%s, mAiq is nullptr",
                     __FUNCTION__);

    return rk_aiq_ae_run(mAiq, ae_input_params, *ae_results);
}

status_t Rk3aAiq::awbRun(const rk_aiq_awb_input_params* awb_input_params,
                         rk_aiq_awb_results** awb_results)
{
    LOG1("@%s", __FUNCTION__);
    CheckAndLogError(mAiq == nullptr, UNKNOWN_ERROR, "@%s, mAiq is nullptr",
                     __FUNCTION__);

    return rk_aiq_awb_run(mAiq, awb_input_params, *awb_results);
}

status_t Rk3aAiq::miscRun(const rk_aiq_misc_isp_input_params* misc_input_params,
                          rk_aiq_misc_isp_results** misc_results)
{
    LOG1("@%s", __FUNCTION__);
    CheckAndLogError(mAiq == nullptr, UNKNOWN_ERROR, "@%s, mAiq is nullptr",
                     __FUNCTION__);

    return rk_aiq_misc_run(mAiq, misc_input_params, *misc_results);
}

status_t Rk3aAiq::statisticsSet(const rk_aiq_statistics_input_params* input_params,
                                const rk_aiq_exposure_sensor_descriptor *sensor_desc)
{
    LOG1("@%s", __FUNCTION__);
    CheckAndLogError(mAiq == nullptr, UNKNOWN_ERROR, "@%s, mAiq is nullptr",
                     __FUNCTION__);

    return rk_aiq_stats_set(mAiq, input_params, sensor_desc);
}

const char* Rk3aAiq::getVersion(void)
{
    LOG1("@%s", __FUNCTION__);
    CheckAndLogError(mAiq == nullptr, "", "@%s, mAiq is nullptr", __FUNCTION__);

    return "";//TODO: rk_aiq_get_version();
}

bool Rk3aAiq::isInitialized() const
{
    LOG1("@%s", __FUNCTION__);

    return mAiq ? true : false;
}
} NAMESPACE_DECLARATION_END
