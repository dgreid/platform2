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

#define LOG_TAG "IPC_AIQ"

#include <ia_types.h>
#include <utils/Errors.h>

#include "common/UtilityMacros.h"
#include "LogHelper.h"
#include "IPCAiq.h"

NAMESPACE_DECLARATION {
IPCAiq::IPCAiq()
{
    LOG1("@%s", __FUNCTION__);
}

IPCAiq::~IPCAiq()
{
    LOG1("@%s", __FUNCTION__);
}

// init
bool IPCAiq::clientFlattenInit(const char* xmlFilePath, aiq_init_params* params)
{
    CheckAndLogError(xmlFilePath == nullptr, false,
                     "@%s, xmlFilePath is nullptr", __FUNCTION__);
    LOG1("@%s, params:%p xmlFilePath:%s", __FUNCTION__, params, xmlFilePath);
    CheckAndLogError(params == nullptr, false, "@%s, params is nullptr",
                     __FUNCTION__);
    CheckAndLogError(sizeof(params->data) <= strlen(xmlFilePath), false,
                     "@%s, params is small", __FUNCTION__);

    strcpy(params->data, xmlFilePath);

    return true;
}

bool IPCAiq::serverUnflattenInit(const aiq_init_params& inParams, const char** xmlFilePath)
{
    LOG1("@%s, xmlFilePath:%p", __FUNCTION__, xmlFilePath);
    CheckAndLogError(xmlFilePath == nullptr, false,
                     "@%s, xmlFilePath is nullptr", __FUNCTION__);

    *xmlFilePath = inParams.data;

    return true;
}

// misc
bool IPCAiq::clientFlattenMisc(uintptr_t aiq, const rk_aiq_misc_isp_input_params& inParams, misc_isp_run_params* params)
{
    LOG1("@%s, params:%p", __FUNCTION__,  params);
    CheckAndLogError(reinterpret_cast<rk_aiq*>(aiq) == nullptr, false,
                     "@%s, aiq is nullptr", __FUNCTION__);
    CheckAndLogError(params == nullptr, false, "@%s, params is nullptr",
                     __FUNCTION__);

    params->aiq_handle = aiq;
    params->base = inParams;

    return true;
}

bool IPCAiq::clientUnflattenMisc(const misc_isp_run_params& params, rk_aiq_misc_isp_results** results)
{
    LOG1("@%s, results:%p", __FUNCTION__, results);
    CheckAndLogError((results == nullptr), false, "@%s, results is nullptr",
                     __FUNCTION__);

    *results = const_cast<rk_aiq_misc_isp_results*>(&params.results);

    return true;
}

// statistics
bool IPCAiq::clientFlattenStat(uintptr_t aiq, const set_statistics_params_data& inParams, set_statistics_params* params)
{
    LOG1("@%s, aiq:0x%" PRIxPTR ", params:%p", __FUNCTION__, aiq, params);
    CheckAndLogError(reinterpret_cast<rk_aiq*>(aiq) == nullptr, false,
                     "@%s, aiq is nullptr", __FUNCTION__);
    CheckAndLogError(params == nullptr, false, "@%s, params is nullptr",
                     __FUNCTION__);

    params->aiq_handle = aiq;

    params->base = inParams;
    set_statistics_params_data* base = &params->base;

    if (base->input) {
        params->input = *base->input;

        rk_aiq_statistics_input_params* input = &params->input;

        if (input->ae_results) {
            params->ae_results = *input->ae_results;
        }

        if (input->awb_results) {
            params->awb_results = *input->awb_results;
        }

        if (input->misc_results) {
            params->misc_results = *input->misc_results;
        }
    }

    if (base->sensor_desc) {
        params->sensor_desc = *base->sensor_desc;
    }

    return true;
}

bool IPCAiq::serverUnflattenStat(const set_statistics_params& inParams, set_statistics_params_data** params)
{
    LOG1("@%s, params:%p", __FUNCTION__, params);
    CheckAndLogError((params == nullptr), false, "@%s, params is nullptr",
                     __FUNCTION__);

    set_statistics_params_data* base = const_cast<set_statistics_params_data*>(&inParams.base);

    if (base->input) {
        base->input = const_cast<rk_aiq_statistics_input_params*>(&inParams.input);

        rk_aiq_statistics_input_params* input = base->input;

        if (input->ae_results) {
            input->ae_results = const_cast<rk_aiq_ae_results*>(&inParams.ae_results);
        }

        if (input->awb_results) {
            input->awb_results = const_cast<rk_aiq_awb_results*>(&inParams.awb_results);
        }

        if (input->misc_results) {
            input->misc_results = const_cast<rk_aiq_misc_isp_results*>(&inParams.misc_results);
        }
    }

    if (base->sensor_desc) {
        base->sensor_desc = const_cast<rk_aiq_exposure_sensor_descriptor*>(&inParams.sensor_desc);
    }

    *params = base;

    return true;
}

// ae
bool IPCAiq::clientFlattenAe(uintptr_t aiq, const rk_aiq_ae_input_params& inParams, ae_run_params* params)
{
    LOG1("@%s, params:%p", __FUNCTION__, params);
    CheckAndLogError((reinterpret_cast<rk_aiq*>(aiq) == nullptr), false,
                     "@%s, aiq is nullptr", __FUNCTION__);
    CheckAndLogError((params == nullptr), false, "@%s, params is nullptr",
                     __FUNCTION__);

    params->aiq_handle = aiq;

    params->base = inParams;
    const rk_aiq_ae_input_params* base = &params->base;

    if (base->window) {
        params->window = *inParams.window;
    }

    if (base->sensor_descriptor) {
        params->sensor_descriptor = *inParams.sensor_descriptor;
    }

    if (base->manual_exposure_time_us) {
        params->manual_exposure_time_us = *inParams.manual_exposure_time_us;
    }

    if (base->manual_analog_gain) {
        params->manual_analog_gain = *inParams.manual_analog_gain;
    }

    if (base->manual_iso) {
        params->manual_iso = *inParams.manual_iso;
    }

    if (base->manual_limits) {
        params->manual_limits = *inParams.manual_limits;
    }

    return true;
}

bool IPCAiq::clientUnflattenAe(const ae_run_params& params, rk_aiq_ae_results** results)
{
    LOG1("@%s, results:%p", __FUNCTION__, results);
    CheckAndLogError((results == nullptr), false, "@%s, results is nullptr",
                     __FUNCTION__);

    *results = const_cast<rk_aiq_ae_results*>(&params.results);

    return true;
}

bool IPCAiq::serverUnflattenAe(const ae_run_params& inParams, rk_aiq_ae_input_params** params)
{
    LOG1("@%s, params:%p", __FUNCTION__, params);
    CheckAndLogError((params == nullptr), false, "@%s, params is nullptr",
                     __FUNCTION__);

    rk_aiq_ae_input_params* base = const_cast<rk_aiq_ae_input_params*>(&inParams.base);

    if (base->window) {
        base->window = const_cast<rk_aiq_window*>(&inParams.window);
    }

    if (base->sensor_descriptor) {
        base->sensor_descriptor = const_cast<rk_aiq_exposure_sensor_descriptor*>(&inParams.sensor_descriptor);
    }

    if (base->manual_exposure_time_us) {
        base->manual_exposure_time_us = const_cast<long*>(&inParams.manual_exposure_time_us);
    }

    if (base->manual_analog_gain) {
        base->manual_analog_gain = const_cast<float*>(&inParams.manual_analog_gain);
    }

    if (base->manual_iso) {
        base->manual_iso = const_cast<short*>(&inParams.manual_iso);
    }

    if (base->manual_limits) {
        base->manual_limits = const_cast<rk_aiq_ae_manual_limits*>(&inParams.manual_limits);
    }

    *params = base;

    return true;
}

// awb
bool IPCAiq::clientFlattenAwb(uintptr_t aiq, const rk_aiq_awb_input_params& inParams, awb_run_params* params)
{
    LOG1("@%s, aiq:0x%" PRIxPTR ", params:%p", __FUNCTION__, aiq, params);
    CheckAndLogError(reinterpret_cast<rk_aiq*>(aiq) == nullptr, false,
                     "@%s, aiq is nullptr", __FUNCTION__);
    CheckAndLogError(params == nullptr, false, "@%s, params is nullptr",
                     __FUNCTION__);

    params->aiq_handle = aiq;

    params->base = inParams;
    const rk_aiq_awb_input_params* base = &params->base;

    if (base->manual_cct_range) {
        params->manual_cct_range = *inParams.manual_cct_range;
    }

    if (base->window) {
        params->window = *inParams.window;
    }

    return true;
}

bool IPCAiq::clientUnflattenAwb(const awb_run_params& params, rk_aiq_awb_results** results)
{
    LOG1("@%s, results:%p", __FUNCTION__, results);
    CheckAndLogError((results == nullptr), false, "@%s, results is nullptr",
                     __FUNCTION__);

    *results = const_cast<rk_aiq_awb_results*>(&params.results);

    return true;
}

bool IPCAiq::serverUnflattenAwb(const awb_run_params& inParams, rk_aiq_awb_input_params** params)
{
    LOG1("@%s, params:%p", __FUNCTION__, params);
    CheckAndLogError((params == nullptr), false, "@%s, params is nullptr",
                     __FUNCTION__);

    rk_aiq_awb_input_params* base = const_cast<rk_aiq_awb_input_params*>(&inParams.base);

    if (base->manual_cct_range) {
        base->manual_cct_range = const_cast<rk_aiq_awb_manual_cct_range*>(&inParams.manual_cct_range);
    }

    if (base->window) {
        base->window = const_cast<rk_aiq_window*>(&inParams.window);
    }

    *params = base;

    return true;
}

} NAMESPACE_DECLARATION_END
