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

#define LOG_TAG "ParameterWorker"

#include <ia_cmc_types.h>
#include <ia_types.h>
#include <cpffData.h>
#include <Pipe.h>
#include <KBL_AIC.h>
#include <linux/intel-ipu3.h>

#include "ParameterWorker.h"
#include "PlatformData.h"
#include "SkyCamProxy.h"
#include "IPU3AicToFwEncoder.h"
#include "NodeTypes.h"

namespace cros {
namespace intel {

const unsigned int PARA_WORK_BUFFERS = 1;

ParameterWorker::ParameterWorker(std::shared_ptr<cros::V4L2VideoNode> node,
                                 int cameraId,
                                 GraphConfig::PipeType pipeType) :
        FrameWorker(node, cameraId, PARA_WORK_BUFFERS, "ParameterWorker"),
        mPipeType(pipeType),
        mSkyCamAIC(nullptr),
        mCmcData(nullptr),
        mAicConfig(nullptr)
{
    LOG1("%s, mPipeType %d", __FUNCTION__, mPipeType);
    CLEAR(mIspPipes);
    CLEAR(mRuntimeParamsOutFrameParams);
    CLEAR(mRuntimeParamsResCfgParams);
    CLEAR(mRuntimeParamsInFrameParams);
    CLEAR(mRuntimeParamsRec);
    CLEAR(mRuntimeParams);
    CLEAR(mCpfData);
    CLEAR(mGridInfo);
}

ParameterWorker::~ParameterWorker()
{
    LOG1("%s, mPipeType %d", __FUNCTION__, mPipeType);
    for (int i = 0; i < NUM_ISP_PIPES; i++) {
        delete mIspPipes[i];
        mIspPipes[i] = nullptr;
    }
}

status_t ParameterWorker::configure(std::shared_ptr<GraphConfig> &config)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    status_t ret = OK;
    uintptr_t cmcHandle = reinterpret_cast<uintptr_t>(nullptr);

    if (PlatformData::getCpfAndCmc(mCpfData, &mCmcData, &cmcHandle, mCameraId) != OK) {
        LOGE("%s : Could not get cpf and cmc data",__FUNCTION__);
        return NO_INIT;
    }

    CLEAR(mRuntimeParamsOutFrameParams);
    CLEAR(mRuntimeParamsResCfgParams);
    CLEAR(mRuntimeParamsInFrameParams);
    CLEAR(mRuntimeParamsRec);
    CLEAR(mRuntimeParams);
    mRuntimeParams.output_frame_params = &mRuntimeParamsOutFrameParams;
    mRuntimeParams.frame_resolution_parameters = &mRuntimeParamsResCfgParams;
    mRuntimeParams.input_frame_params = &mRuntimeParamsInFrameParams;
    mRuntimeParams.focus_rect = &mRuntimeParamsRec;

    GraphConfig::NodesPtrVector sinks;
    std::string name = "csi_be:output";
    ret = config->graphGetDimensionsByName(name, mCsiBe.width, mCsiBe.height);
    if (ret != OK) {
        LOGE("Cannot find <%s> node", name.c_str());
        return ret;
    }

    ret = setGridInfo(mCsiBe.width);
    if (ret != OK) {
        return ret;
    }

    ia_aiq_frame_params sensorParams;
    config->getSensorFrameParams(sensorParams);

    PipeConfig pipeConfig;
    if (config->doesNodeExist("imgu:main")) {
        ret = getPipeConfig(pipeConfig, config, GC_MAIN);
        CheckError(ret != OK, ret, "Failed to get pipe config main pipe");

        overrideCPFFMode(&pipeConfig, config);
        fillAicInputParams(sensorParams, pipeConfig, &mRuntimeParams);
    } else if (config->doesNodeExist("imgu:vf")) {
        ret = getPipeConfig(pipeConfig, config, GC_VF);
        CheckError(ret != OK, ret, "Failed to get pipe config vf pipe");

        overrideCPFFMode(&pipeConfig, config);
        fillAicInputParams(sensorParams, pipeConfig, &mRuntimeParams);
    } else {
        LOGE("PipeType %d config is wrong", mPipeType);
        return BAD_VALUE;
    }

    for (int i = 0; i < NUM_ISP_PIPES; i++) {
        mIspPipes[i] = new IPU3ISPPipe;
    }

    ia_cmc_t* cmc = reinterpret_cast<ia_cmc_t*>(cmcHandle);

    if (mPipeType == GraphConfig::PIPE_STILL) {
        mRuntimeParams.frame_use = ia_aiq_frame_use_still;
    } else {
        mRuntimeParams.frame_use = ia_aiq_frame_use_preview;
    }

    AicMode aicMode = mPipeType == GraphConfig::PIPE_STILL ? AIC_MODE_STILL : AIC_MODE_VIDEO;
    if (mSkyCamAIC == nullptr) {
        mSkyCamAIC = SkyCamProxy::createProxy(mCameraId, aicMode, mIspPipes, NUM_ISP_PIPES, cmc, &mCpfData, &mRuntimeParams, 0, 0);
        CheckError((mSkyCamAIC == nullptr), NO_MEMORY, "Not able to create SkyCam AIC");
    }

    FrameInfo frame;
    int page_size = getpagesize();
    frame.width = sizeof(ipu3_uapi_params) + page_size - (sizeof(ipu3_uapi_params) % page_size);
    frame.height = 1;
    frame.stride = frame.width;
    frame.format = V4L2_META_FMT_IPU3_PARAMS;
    ret = setWorkerDeviceFormat(frame);
    if (ret != OK)
        return ret;

    ret = setWorkerDeviceBuffers(getDefaultMemoryType(IMGU_NODE_PARAM));
    if (ret != OK)
        return ret;

    ret = allocateWorkerBuffers(GRALLOC_USAGE_SW_WRITE_OFTEN |
                                GRALLOC_USAGE_HW_CAMERA_READ,
                                HAL_PIXEL_FORMAT_BLOB);
    if (ret != OK)
        return ret;

    mIndex = 0;

    return OK;
}

status_t ParameterWorker::setGridInfo(uint32_t csiBeWidth)
{
    if (csiBeWidth == 0) {
        LOGE("CSI BE width cannot be 0 - BUG");
        return BAD_VALUE;
    }
    mGridInfo.bds_padding_width = ALIGN128(csiBeWidth);

    return OK;
}

void ParameterWorker::dump()
{
    LOGD("dump mRuntimeParams");
    if (mRuntimeParams.awb_results)
        LOGD("  mRuntimeParams.awb_results: %f", mRuntimeParams.awb_results->accurate_b_per_g);
    if (mRuntimeParams.frame_resolution_parameters)
        LOGD("  mRuntimeParams.frame_resolution_parameters->BDS_horizontal_padding %d", mRuntimeParams.frame_resolution_parameters->BDS_horizontal_padding);
    if (mRuntimeParams.exposure_results)
        LOGD("  mRuntimeParams.exposure_results->analog_gain: %f", mRuntimeParams.exposure_results->analog_gain);
}

status_t ParameterWorker::prepareRun(std::shared_ptr<DeviceMessage> msg)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    std::lock_guard<std::mutex> l(mParamsMutex);

    mMsg = msg;

    // Don't queue ISP parameter buffer if test pattern mode is used.
    if (mMsg->pMsg.processingSettings->captureSettings->testPatternMode
            != ANDROID_SENSOR_TEST_PATTERN_MODE_OFF) {
        return OK;
    }

    if (mPipeType == GraphConfig::PIPE_STILL) {
        // always update LSC for still pipe
        mMsg->pMsg.processingSettings->captureSettings->aiqResults.saResults.lsc_update = true;
    }
    updateAicInputParams(mMsg, &mRuntimeParams);
    LOG2("frame use %d, timestamp %lld", mRuntimeParams.frame_use, mRuntimeParams.time_stamp);
    if (mSkyCamAIC)
        mSkyCamAIC->Run(mRuntimeParams);
    mAicConfig = mSkyCamAIC->GetAicConfig();
    if (mAicConfig == nullptr) {
        LOGE("Could not get AIC config");
        return UNKNOWN_ERROR;
    }

    ipu3_uapi_params *ipu3Params = (ipu3_uapi_params*)mBufferAddr[mIndex];
    IPU3AicToFwEncoder::encodeParameters(mAicConfig, ipu3Params);

    status_t status = mNode->PutFrame(&mBuffers[mIndex]);
    if (status != OK) {
        LOGE("putFrame failed");
        return UNKNOWN_ERROR;
    }

    mIndex = (mIndex + 1) % mPipelineDepth;

    return OK;
}

status_t ParameterWorker::run()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

    // Don't dequeue ISP parameter buffer if test pattern mode is used.
    if (mMsg->pMsg.processingSettings->captureSettings->testPatternMode
            != ANDROID_SENSOR_TEST_PATTERN_MODE_OFF) {
        return OK;
    }

    cros::V4L2Buffer outBuf;

    status_t status = mNode->GrabFrame(&outBuf);
    if (status < 0) {
        LOGE("grabFrame failed");
        return UNKNOWN_ERROR;
    }

    return OK;
}

status_t ParameterWorker::postRun()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    mMsg = nullptr;
    return OK;
}

void ParameterWorker::updateAicInputParams(const std::shared_ptr<DeviceMessage> &msg,
                                           IPU3AICRuntimeParams *params) const
{
    CheckError(!params, VOID_VALUE, "@%s, params is nullptr", __func__);

    const std::shared_ptr<CaptureUnitSettings>& settings =
        msg->pMsg.processingSettings->captureSettings;

    params->time_stamp = settings->timestamp / 1000; //microsecond unit
    params->manual_brightness = settings->ispSettings.manualSettings.manualBrightness;
    params->manual_contrast = settings->ispSettings.manualSettings.manualContrast;
    params->manual_hue = settings->ispSettings.manualSettings.manualHue;
    params->manual_saturation = settings->ispSettings.manualSettings.manualSaturation;
    params->manual_sharpness = settings->ispSettings.manualSettings.manualSharpness;
    params->pa_results = &settings->aiqResults.paResults;
    params->sa_results = &settings->aiqResults.saResults;
    params->weight_grid = settings->aiqResults.aeResults.weight_grid;
    params->isp_vamem_type = 0;
    params->exposure_results = settings->aiqResults.aeResults.exposures->exposure;
    params->awb_results = &settings->aiqResults.awbResults;
    params->gbce_results = &settings->aiqResults.gbceResults;
}

void ParameterWorker::fillAicInputParams(const ia_aiq_frame_params &sensorFrameParams,
                                         const PipeConfig &pipeCfg,
                                         IPU3AICRuntimeParams *params)
{
    CheckError(!params, VOID_VALUE, "@%s, params is nullptr", __func__);

    //Fill AIC input frame params
    aic_input_frame_parameters_t *inFrameParams = &mRuntimeParamsInFrameParams;
    inFrameParams->sensor_frame_params = sensorFrameParams;
    inFrameParams->fix_flip_x = 0;
    inFrameParams->fix_flip_y = 0;

    //Fill AIC output frame params
    ia_aiq_output_frame_parameters_t *outFrameParams = &mRuntimeParamsOutFrameParams;
    outFrameParams->height =
        params->input_frame_params->sensor_frame_params.cropped_image_height;
    outFrameParams->width =
        params->input_frame_params->sensor_frame_params.cropped_image_width;

    aic_resolution_config_parameters_t *resCfgParams = &mRuntimeParamsResCfgParams;
    // Temporary assigning values to resCfgParams until KS property will give the information.
    // IF crop is the offset between the sensor output and the IF cropping.
    // Currently assuming that the ISP crops in the middle.
    // Need to consider bayer order

    resCfgParams->horizontal_IF_crop = (pipeCfg.csi_be_width - pipeCfg.input_feeder_out_width) / 2;
    resCfgParams->vertical_IF_crop = (pipeCfg.csi_be_height - pipeCfg.input_feeder_out_height) / 2;
    resCfgParams->BDSin_img_width = pipeCfg.input_feeder_out_width;
    resCfgParams->BDSin_img_height = pipeCfg.input_feeder_out_height;
    resCfgParams->BDSout_img_width = pipeCfg.bds_out_width;
    resCfgParams->BDSout_img_height = pipeCfg.bds_out_height;
    resCfgParams->BDS_horizontal_padding =
        static_cast<uint16_t>(ALIGN128(pipeCfg.bds_out_width) - pipeCfg.bds_out_width);

    LOG2("AIC res CFG params: IF Crop %dx%d, BDS In %dx%d, BDS Out %dx%d, BDS Padding %d",
         resCfgParams->horizontal_IF_crop,
         resCfgParams->vertical_IF_crop,
         resCfgParams->BDSin_img_width,
         resCfgParams->BDSin_img_height,
         resCfgParams->BDSout_img_width,
         resCfgParams->BDSout_img_height,
         resCfgParams->BDS_horizontal_padding);

    LOG2("Sensor/cio2 Output %dx%d, effective input %dx%d",
         pipeCfg.csi_be_width, pipeCfg.csi_be_height,
         pipeCfg.input_feeder_out_width, pipeCfg.input_feeder_out_height);

    params->mode_index = pipeCfg.cpff_mode_hint;
}

status_t ParameterWorker::getPipeConfig(PipeConfig &pipeCfg, std::shared_ptr<GraphConfig> &config, const string &pin) const
{
    status_t ret = OK;

    string baseNode = string("imgu:");

    string node = baseNode + "if";
    ret |= config->graphGetDimensionsByName(node, pipeCfg.input_feeder_out_width,
            pipeCfg.input_feeder_out_height);

    node = baseNode + "bds";
    ret |= config->graphGetDimensionsByName(node, pipeCfg.bds_out_width,
            pipeCfg.bds_out_height);

    node = baseNode + "gdc";
    ret |= config->graphGetDimensionsByName(node, pipeCfg.gdc_out_width,
            pipeCfg.gdc_out_height);

    node = baseNode + (config->doesNodeExist("imgu:yuv") ? "yuv" : pin);
    ret |= config->graphGetDimensionsByName(node, pipeCfg.main_out_width,
            pipeCfg.main_out_height);

    node = baseNode + "filter";
    ret |= config->graphGetDimensionsByName(node, pipeCfg.filter_width,
            pipeCfg.filter_height);

    node = baseNode + "env";
    ret |= config->graphGetDimensionsByName(node, pipeCfg.envelope_width,
            pipeCfg.envelope_height);

    if (ret != OK) {
        LOGE("Cannot GraphConfig data.");
        return UNKNOWN_ERROR;
    }

    pipeCfg.view_finder_out_width = 0;
    pipeCfg.view_finder_out_height = 0;
    pipeCfg.csi_be_height = mCsiBe.height;
    pipeCfg.csi_be_width = mCsiBe.width;

    return ret;
}

void ParameterWorker::overrideCPFFMode(PipeConfig *pipeCfg, std::shared_ptr<GraphConfig> &config)
{
    if (pipeCfg == nullptr)
        return;

    if (mPipeType == GraphConfig::PIPE_STILL) {
        pipeCfg->cpff_mode_hint = CPFF_MAIN;
        return;
    }

    /* Due to suppport 360 degree orientation, so width is less than
     * height in portrait mode, need to use max length between width
     * and height to do comparison.
     */
    int maxLength = MAX(pipeCfg->main_out_width, pipeCfg->main_out_height);
    if (maxLength > RESOLUTION_1080P_WIDTH) {
        pipeCfg->cpff_mode_hint = CPFF_MAIN;
    } else if (maxLength > RESOLUTION_720P_WIDTH) {
        pipeCfg->cpff_mode_hint = CPFF_FHD;
    } else if (maxLength > RESOLUTION_VGA_WIDTH) {
        pipeCfg->cpff_mode_hint = CPFF_HD;
    } else {
        pipeCfg->cpff_mode_hint = CPFF_VGA;
    }
    LOG2("%s final cpff mode %d", __FUNCTION__, pipeCfg->cpff_mode_hint);
}

} /* namespace intel */
} /* namespace cros */
