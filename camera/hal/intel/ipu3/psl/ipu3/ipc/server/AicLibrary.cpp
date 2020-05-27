/*
 * Copyright (C) 2016-2020 Intel Corporation.
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

#define LOG_TAG "AicLibrary"
#include <string.h>

#include "UtilityMacros.h"
#include "AicLibrary.h"
#include "LogHelper.h"
#include "ia_cmc_parser.h"

#include "client/SkyCamMojoProxy.h"

namespace cros {
namespace intel {
AicLibrary::AicLibrary()
{
    LOG1("@%s", __FUNCTION__);
}

AicLibrary::~AicLibrary()
{
    LOG1("@%s", __FUNCTION__);
}

status_t AicLibrary::init(void* pData, int dataSize)
{
    LOG1("@%s, pData:%p, dataSize:%d", __FUNCTION__, pData, dataSize);
    CheckError((pData == nullptr), UNKNOWN_ERROR, "@%s, pData is nullptr", __FUNCTION__);
    CheckError((dataSize < sizeof(Transport)), UNKNOWN_ERROR, "@%s, buffer is small", __FUNCTION__);

    ia_binary_data aiqb = {nullptr, 0};
    unsigned int numPipes = 0;
    unsigned int dumpAicParameters = 0;
    int testFrameworkDump = 0;

    Transport* transport = static_cast<Transport*>(pData);
    IPU3AICRuntimeParams* params = nullptr;
    ia_cmc_t* cmc = nullptr;
    bool ret = mIpc.serverUnflattenInit(*transport,
                                            &params,
                                            &aiqb,
                                            &cmc,
                                            &numPipes,
                                            &dumpAicParameters,
                                            &testFrameworkDump);
    CheckError((ret == false), UNKNOWN_ERROR, "@%s, clientUnflattenInit fails", __FUNCTION__);
    CheckError((numPipes > NUM_ISP_PIPES), UNKNOWN_ERROR,
        "@%s, numPipes:%d is bigger than NUM_ISP_PIPES:%d", __FUNCTION__, numPipes, NUM_ISP_PIPES);
    CheckError(cmc == nullptr, UNKNOWN_ERROR, "@%s, cmc is nullptr", __FUNCTION__);

    ISPPipe *tempISPPipes[NUM_ISP_PIPES];
    for (int i = 0; i < numPipes; i++) {
        mIspPipes[transport->aicMode][i].reset(new IPU3ISPPipe);
        tempISPPipes[i] = mIspPipes[transport->aicMode][i].get();
    }

    mSkyCam[transport->aicMode] = nullptr;
    mSkyCam[transport->aicMode].reset(new KBL_AIC(tempISPPipes,
                                numPipes,
                                cmc,
                                &aiqb,
                                *params,
                                dumpAicParameters,
                                testFrameworkDump));

    return OK;
}

void AicLibrary::run(void* pData, int dataSize)
{
    LOG1("@%s, pData:%p, dataSize:%d", __FUNCTION__, pData, dataSize);
    CheckError((pData == nullptr), VOID_VALUE, "@%s, pData is nullptr", __FUNCTION__);
    CheckError((dataSize < sizeof(Transport)), VOID_VALUE, "@%s, buffer is small", __FUNCTION__);

    Transport* transport = static_cast<Transport*>(pData);
    IPU3AICRuntimeParams* params = nullptr;
    bool ret = mIpc.serverUnflattenRun(*transport, &params);
    CheckError(ret == false, VOID_VALUE, "@%s, serverUnflattenRun fails", __FUNCTION__);

    mSkyCam[transport->aicMode]->Run(params, 1);
}

void AicLibrary::reset(void* pData, int dataSize)
{
    LOG1("@%s, pData:%p, dataSize:%d", __FUNCTION__, pData, dataSize);
    CheckError((pData == nullptr), VOID_VALUE, "@%s, pData is nullptr", __FUNCTION__);
    CheckError((dataSize < sizeof(Transport)), VOID_VALUE, "@%s, buffer is small", __FUNCTION__);

    Transport* transport = static_cast<Transport*>(pData);
    IPU3AICRuntimeParams* params = nullptr;
    bool ret = mIpc.serverUnflattenRun(*transport, &params);
    CheckError(ret == false, VOID_VALUE, "@%s, serverUnflattenRun fails", __FUNCTION__);

    mSkyCam[transport->aicMode]->Reset(*params);
}

status_t AicLibrary::getAicVersion(void* pData, int dataSize)
{
    LOG1("@%s, pData:%p, dataSize:%d", __FUNCTION__, pData, dataSize);
    CheckError((pData == nullptr), UNKNOWN_ERROR, "@%s, pData is nullptr", __FUNCTION__);
    CheckError((dataSize < sizeof(ia_aic_version_params)), UNKNOWN_ERROR, "@%s, buffer is small", __FUNCTION__);

    ia_aic_version_params* params = static_cast<ia_aic_version_params*>(pData);
    std::string version = mSkyCam[params->aicMode]->GetAICVersion();
    CheckError(version.size() == 0, UNKNOWN_ERROR, "@%s, GetAICVersion fails", __FUNCTION__);

    strncpy(params->data, version.c_str(), sizeof(params->data));
    params->data[MAX_IA_AIC_VERSION_PARAMS_DATA_SIZE - 1] = '\0';
    params->size = MIN(version.size(), sizeof(params->data));
    LOG2("@%s, aic version:%s, size:%d", __FUNCTION__, version.c_str(), params->size);

    return OK;
}

status_t AicLibrary::getAicConfig(void* pData, int dataSize)
{
    LOG1("@%s, pData:%p, dataSize:%d", __FUNCTION__, pData, dataSize);
    CheckError((pData == nullptr), UNKNOWN_ERROR, "@%s, pData is nullptr", __FUNCTION__);
    CheckError((dataSize < sizeof(aic_config)), UNKNOWN_ERROR, "@%s, buffer is small", __FUNCTION__);

    IPU3AicConfig* params = static_cast<IPU3AicConfig*>(pData);
    aic_config* cfg = mIspPipes[params->aicMode][0]->GetAicConfig();
    CheckError(cfg == nullptr, UNKNOWN_ERROR, "@%s, BUG: GetAicConfig fails", __FUNCTION__);

    MEMCPY_S(&params->aicConfig, dataSize, cfg, sizeof(aic_config));

    return OK;
}
} /* namespace intel */
} /* namespace cros */
