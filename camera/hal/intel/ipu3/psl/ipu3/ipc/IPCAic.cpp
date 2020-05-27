/*
 * Copyright (C) 2016-2020 Intel Corporation
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

#define LOG_TAG "IPC_AIC"

#include <ia_types.h>
#include <utils/Errors.h>

#include "common/UtilityMacros.h"
#include "LogHelper.h"
#include "IPCAic.h"

namespace cros {
namespace intel {
IPCAic::IPCAic()
{
    LOG1("@%s", __FUNCTION__);
}

IPCAic::~IPCAic()
{
    LOG1("@%s", __FUNCTION__);
}

bool IPCAic::flattenIPU3AICRuntimeParams(const IPU3AICRuntimeParams& params, IPU3AICRuntimeParams_data* input)
{
    LOG1("@%s, input:%p", __FUNCTION__, input);
    CheckError(input == nullptr, false, "@%s, input is nullptr", __FUNCTION__);

    IPU3AICRuntimeParams* base = &input->base;
    *base = params;

    if (base->input_frame_params) {
        input->input_frame_params = *base->input_frame_params;
    }

    if (base->frame_resolution_parameters) {
        input->frame_resolution_parameters = *base->frame_resolution_parameters;
    }

    if (base->output_frame_params) {
        input->output_frame_params = *base->output_frame_params;
    }

    if (base->exposure_results) {
        input->exposure_results = *base->exposure_results;
    }

    if (base->weight_grid) {
        input->weight_grid = *base->weight_grid;
        if (base->weight_grid->weights) {
            MEMCPY_S(input->weights,
                sizeof(input->weights),
                base->weight_grid->weights,
                base->weight_grid->width * base->weight_grid->height * sizeof(*base->weight_grid->weights));
        }
    }

    if (base->awb_results) {
        input->awb_results = *base->awb_results;
    }

    if (base->gbce_results) {
        input->gbce_results = *base->gbce_results;

        size_t size = base->gbce_results->gamma_lut_size * sizeof(float);
        if (input->gbce_results.r_gamma_lut) {
            MEMCPY_S(input->r_gamma_lut,
                    sizeof(input->r_gamma_lut),
                    base->gbce_results->r_gamma_lut,
                    size);
        }
        if (input->gbce_results.b_gamma_lut) {
            MEMCPY_S(input->b_gamma_lut,
                    sizeof(input->b_gamma_lut),
                    base->gbce_results->b_gamma_lut,
                    size);
        }
        if (input->gbce_results.g_gamma_lut) {
            MEMCPY_S(input->g_gamma_lut,
                    sizeof(input->g_gamma_lut),
                    base->gbce_results->g_gamma_lut,
                    size);
        }
        if (input->gbce_results.tone_map_lut) {
            MEMCPY_S(input->tone_map_lut,
                    sizeof(input->tone_map_lut),
                    base->gbce_results->tone_map_lut,
                    base->gbce_results->tone_map_lut_size * sizeof(float));
        }
    }

    if (base->pa_results) {
        bool ret = IPCAiq::flattenPaResults(*base->pa_results, &input->pa_results);
        CheckError(ret == false, false, "@%s, flattenPaResults fails", __FUNCTION__);
    }

    if (base->sa_results) {
        bool ret = IPCAiq::flattenSaResults(*base->sa_results, &input->sa_results);
        CheckError(ret == false, false, "@%s, flattenSaResults fails", __FUNCTION__);
    }

    if (base->focus_rect) {
        input->focus_rect = *base->focus_rect;
    }

    return true;
}

bool IPCAic::unflattenIPU3AICRuntimeParams(IPU3AICRuntimeParams_data* input)
{
    LOG1("@%s, input:%p", __FUNCTION__, input);
    CheckError(input == nullptr, false, "@%s, input is nullptr", __FUNCTION__);

    IPU3AICRuntimeParams* base = &input->base;

    if (base->input_frame_params) {
        base->input_frame_params = &input->input_frame_params;
    }

    if (base->frame_resolution_parameters) {
        base->frame_resolution_parameters = &input->frame_resolution_parameters;
    }

    if (base->output_frame_params) {
        base->output_frame_params = &input->output_frame_params;
    }

    if (base->exposure_results) {
        base->exposure_results = &input->exposure_results;
    }

    if (base->weight_grid) {
        base->weight_grid = &input->weight_grid;
        if (input->weight_grid.weights) {
            input->weight_grid.weights = input->weights;
        }
    }

    if (base->awb_results) {
        base->awb_results = &input->awb_results;
    }

    if (base->gbce_results) {
        base->gbce_results = &input->gbce_results;
        if (input->gbce_results.r_gamma_lut) {
            input->gbce_results.r_gamma_lut = input->r_gamma_lut;
        }
        if (input->gbce_results.b_gamma_lut) {
            input->gbce_results.b_gamma_lut = input->b_gamma_lut;
        }
        if (input->gbce_results.g_gamma_lut) {
            input->gbce_results.g_gamma_lut = input->g_gamma_lut;
        }
        if (input->gbce_results.tone_map_lut) {
            input->gbce_results.tone_map_lut = input->tone_map_lut;
        }
    }

    if (base->pa_results) {
        IPCAiq::unflattenPaResults(&input->pa_results);
        base->pa_results = &input->pa_results.base;
    }

    if (base->sa_results) {
        IPCAiq::unflattenSaResults(&input->sa_results);
        base->sa_results = &input->sa_results.base;
    }

    if (base->focus_rect) {
        base->focus_rect = &input->focus_rect;
    }

    return true;
}

bool IPCAic::clientFlattenInit(const IPU3AICRuntimeParams& runtimeParams,
                                    unsigned int numPipes,
                                    const ia_binary_data* aiqb,
                                    uintptr_t cmc,
                                    unsigned int dumpAicParameters,
                                    int testFrameworkDump,
                                    Transport* transport)
{
    LOG1("@%s, numPipes:%d, aiqb:%p, dumpAicParameters:%d, testFrameworkDump:%d, transport:%p",
        __FUNCTION__, numPipes, aiqb, dumpAicParameters, testFrameworkDump, transport);
    CheckError(transport == nullptr, false, "@%s, transport is nullptr", __FUNCTION__);

    if (aiqb) {
        CheckError(aiqb->size > sizeof(transport->aiqb.data),
            false, "@%s, aiqb, size:%d is too big", __FUNCTION__, aiqb->size);
        transport->aiqb.size = aiqb->size;
        MEMCPY_S(transport->aiqb.data, sizeof(transport->aiqb.data), aiqb->data, aiqb->size);
    }

    CheckError(reinterpret_cast<ia_cmc_t*>(cmc) == nullptr, false, "@%s, cmc is nullptr", __FUNCTION__);
    transport->cmcRemoteHandle = cmc;

    transport->dumpAicParameters = dumpAicParameters;
    transport->test_frameworkDump = testFrameworkDump;
    transport->numPipes = numPipes;

    bool ret = flattenIPU3AICRuntimeParams(runtimeParams, &transport->input);
    CheckError(ret == false, false, "@%s, flattenIPU3AICRuntimeParams fails", __FUNCTION__);

    return true;
}

bool IPCAic::serverUnflattenInit(const Transport& transport,
                                    IPU3AICRuntimeParams** params,
                                    ia_binary_data* aiqb,
                                    ia_cmc_t** cmc,
                                    unsigned int* numPipes,
                                    unsigned int* dumpAicParameters,
                                    int* test_frameworkDump)
{
    LOG1("@%s, params:%p, aiqb:%p, numPipes:%p, dumpAicParameters:%p, test_frameworkDump:%p",
        __FUNCTION__, params, aiqb, numPipes, dumpAicParameters, test_frameworkDump);
    CheckError(params == nullptr, false, "@%s, params is nullptr", __FUNCTION__);
    CheckError(numPipes == nullptr, false, "@%s, numPipes is nullptr", __FUNCTION__);
    CheckError(dumpAicParameters == nullptr, false, "@%s, dumpAicParameters is nullptr", __FUNCTION__);
    CheckError(test_frameworkDump == nullptr, false, "@%s, test_frameworkDump is nullptr", __FUNCTION__);

    if (aiqb) {
        char* aiqbData = const_cast<char*>(transport.aiqb.data);
        aiqb->data = static_cast<void*>(aiqbData);
        aiqb->size = transport.aiqb.size;
    }

    if (cmc) {
        *cmc = reinterpret_cast<ia_cmc_t*>(transport.cmcRemoteHandle);
    }

    *numPipes = transport.numPipes;
    *dumpAicParameters = transport.dumpAicParameters;
    *test_frameworkDump = transport.test_frameworkDump;

    IPU3AICRuntimeParams_data* input = const_cast<IPU3AICRuntimeParams_data*>(&transport.input);

    bool ret = unflattenIPU3AICRuntimeParams(input);
    CheckError(ret == false, false, "@%s, unflattenIPU3AICRuntimeParams fails", __FUNCTION__);

    *params = &input->base;

    return true;
}

bool IPCAic::clientFlattenRun(const IPU3AICRuntimeParams& runtimeParams, Transport* transport)
{
    LOG1("@%s, transport:%p", __FUNCTION__, transport);
    CheckError(transport == nullptr, false, "@%s, transport is nullptr", __FUNCTION__);

    bool ret = flattenIPU3AICRuntimeParams(runtimeParams, &transport->input);
    CheckError(ret == false, false, "@%s, flattenIPU3AICRuntimeParams fails", __FUNCTION__);

    return true;
}

bool IPCAic::serverUnflattenRun(const Transport& transport, IPU3AICRuntimeParams** params)
{
    LOG1("@%s, params:%p", __FUNCTION__, params);
    CheckError(params == nullptr, false, "@%s, params is nullptr", __FUNCTION__);

    IPU3AICRuntimeParams_data* input = const_cast<IPU3AICRuntimeParams_data*>(&transport.input);

    bool ret = unflattenIPU3AICRuntimeParams(input);
    CheckError(ret == false, false, "@%s, unflattenIPU3AICRuntimeParams fails", __FUNCTION__);

    *params = &input->base;

    return true;
}

bool IPCAic::clientFlattenReset(const IPU3AICRuntimeParams &runtimeParams, Transport* transport)
{
    LOG1("@%s, transport:%p", __FUNCTION__, transport);
    return clientFlattenRun(runtimeParams, transport);
}

bool IPCAic::serverUnflattenReset(const Transport& transport, IPU3AICRuntimeParams** params)
{
    LOG1("@%s, params:%p", __FUNCTION__, params);
    return serverUnflattenRun(transport, params);
}

} /* namespace intel */
} /* namespace cros */
