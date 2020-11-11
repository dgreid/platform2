/*
 * Copyright (C) 2017-2020 Intel Corporation.
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

#ifndef PSL_IPU3_WORKERS_PARAMETERWORKER_H_
#define PSL_IPU3_WORKERS_PARAMETERWORKER_H_

#include <mutex>
#include <KBL_AIC.h>
#include "FrameWorker.h"
#include "IPU3ISPPipe.h"

namespace cros {
namespace intel {

struct PipeConfig
{
    unsigned short valid;                   // boolean, but using unsigned short for DWORD alignment for static size check.
    unsigned short cpff_mode_hint;          // boolean, but using unsigned short for DWORD alignment for static size check.
    unsigned short input_feeder_out_width;
    unsigned short input_feeder_out_height;
    unsigned short bds_out_width;
    unsigned short bds_out_height;
    unsigned short gdc_out_width;
    unsigned short gdc_out_height;
    unsigned short main_out_width;
    unsigned short main_out_height;
    unsigned short view_finder_out_width;
    unsigned short view_finder_out_height;
    unsigned short filter_width;
    unsigned short filter_height;
    unsigned short envelope_width;
    unsigned short envelope_height;

    unsigned short csi_be_width;
    unsigned short csi_be_height;

    PipeConfig() :
        valid(0),
        cpff_mode_hint(0),
        input_feeder_out_width(0),
        input_feeder_out_height(0),
        bds_out_width(0),
        bds_out_height(0),
        gdc_out_width(0),
        gdc_out_height(0),
        main_out_width(0),
        main_out_height(0),
        view_finder_out_width(0),
        view_finder_out_height(0),
        filter_width(0),
        filter_height(0),
        envelope_width(0),
        envelope_height(0),
        csi_be_width(0),
        csi_be_height(0) {}
};

struct GridInfo
{
    uint32_t bds_padding_width;
};

class SkyCamProxy;

class ParameterWorker: public FrameWorker
{
public:
    ParameterWorker(std::shared_ptr<cros::V4L2VideoNode> node, int cameraId, GraphConfig::PipeType pipeType);
    ParameterWorker(const ParameterWorker&) = delete;
    ParameterWorker& operator=(const ParameterWorker&) = delete;

    virtual ~ParameterWorker();

    virtual status_t configure(std::shared_ptr<GraphConfig> &config);
    status_t prepareRun(std::shared_ptr<DeviceMessage> msg);
    status_t run();
    status_t postRun();

    void dump();

private:
    struct CsiBeOut {
        int width;
        int height;

        CsiBeOut() :
            width(0),
            height(0) {}
    };

    // same mode definition with tuning file
    enum CPFFMode {
        CPFF_MAIN = 0,
        CPFF_FHD,
        CPFF_HD,
        CPFF_VGA,
    };
    void updateAicInputParams(const std::shared_ptr<DeviceMessage> &msg,
                              IPU3AICRuntimeParams *params) const;
    void fillAicInputParams(const ia_aiq_frame_params &sensorFrameParams,
                            const PipeConfig &pipeCfg,
                            IPU3AICRuntimeParams *params);
    status_t setGridInfo(uint32_t csiBeWidth);
    status_t getPipeConfig(PipeConfig &pipeCfg, std::shared_ptr<GraphConfig> &config, const string &pin) const;
    void overrideCPFFMode(PipeConfig *pipeCfg, std::shared_ptr<GraphConfig> &config);

private:
    GraphConfig::PipeType mPipeType;
    std::shared_ptr<SkyCamProxy> mSkyCamAIC;

    ia_aiq_output_frame_parameters_t mRuntimeParamsOutFrameParams; // used by mRuntimeParams
    aic_resolution_config_parameters_t mRuntimeParamsResCfgParams; // used by mRuntimeParams
    aic_input_frame_parameters_t mRuntimeParamsInFrameParams; // used by mRuntimeParams
    ia_rectangle mRuntimeParamsRec; // used by mRuntimeParams
    IPU3AICRuntimeParams mRuntimeParams;

    IPU3ISPPipe *mIspPipes[NUM_ISP_PIPES];

    ia_binary_data mCpfData;
    ia_cmc_t *mCmcData; /* Aiq owns this */

    GridInfo mGridInfo;

    CsiBeOut mCsiBe;

    aic_config *mAicConfig;

    std::mutex mParamsMutex;
};

} /* namespace intel */
} /* namespace cros */

#endif /* PSL_IPU3_WORKERS_PARAMETERWORKER_H_ */
