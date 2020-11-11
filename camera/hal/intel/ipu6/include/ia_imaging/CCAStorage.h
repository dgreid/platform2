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
#pragma once

#include "AIQResult.h"
#include "CCAMacro.h"
#include "ia_abstraction.h"
#include <map>
#include <list>

namespace cca {

/*!
 * \brief storage for aiq results
 */
struct cca_aiq_results_storage {
    ia_aiq_pa_results_v1* pa_results;
    ia_aiq_awb_results* awb_results;
    ia_aiq_ae_results* aec_results;
    ia_aiq_gbce_results* gbce_results;
    ia_aiq_sa_results_v1 *sa_results;
    uint32_t aiq_results_bitmap;
    cca_aiq_results_storage() :
        pa_results(nullptr),
        awb_results(nullptr),
        aec_results(nullptr),
        gbce_results(nullptr),
        sa_results(nullptr),
        aiq_results_bitmap(0)
        {}
};

class CCAStorage {
public:
    CCAStorage(uint8_t len);
    ia_err saveAiqResults(uint64_t frameId, const cca_aiq_results_storage& results);
    ia_err queryAiqResults(uint64_t frameId, cca_aiq_results_storage* results);
    virtual ~CCAStorage();

private:
    ia_err createAiqResult();
    void deleteAiqResult();
    std::map<uint64_t, cca_aiq_results_storage> mAiqResultsMap;
    std::list<uint64_t> mFrameIdList;
    uint8_t mStorageLen;
    mutex_t mStorageMutex;

    typedef struct {
        //aec results
        ia_aiq_ae_exposure_result exposure_results[MAX_NUM_EXPOSURE];
        ia_aiq_aperture_control   aperture_control;
        ia_aiq_hist_weight_grid   weight_grid;
        unsigned char weights[MAX_WEIGHT_GRID_SIZE];
        ia_aiq_flash_parameters   flashes[MAX_NUM_FLASH_LEDS];
        ia_aiq_exposure_parameters        generic_exposure[MAX_NUM_EXPOSURE * MAX_EXPO_PLAN];
        ia_aiq_exposure_sensor_parameters sensor_exposure[MAX_NUM_EXPOSURE * MAX_EXPO_PLAN];

        //gbce results
        float r_gamma_lut[MAX_GAMMA_LUT_SIZE];
        float g_gamma_lut[MAX_GAMMA_LUT_SIZE];
        float b_gamma_lut[MAX_GAMMA_LUT_SIZE];
        float tone_map_lut[MAX_TONE_MAP_LUT_SIZE];

        //pa results
        ia_aiq_advanced_ccm_t preferred_acm;
        unsigned int hue_sectors[MAX_NUM_SECTORS];
        float ACM[MAX_NUM_SECTORS][3][3];
        ia_aiq_ir_weight_t ir_weight;
        unsigned short ir_weight_r[MAX_IR_WEIGHT_GRID_SIZE];
        unsigned short ir_weight_g[MAX_IR_WEIGHT_GRID_SIZE];
        unsigned short ir_weight_b[MAX_IR_WEIGHT_GRID_SIZE];
        ia_aiq_rgbir_t rgbir;
        ia_aiq_rgbir_model_t models;

        ia_aiq_pa_results_v1 pa_results;
        ia_aiq_awb_results awb_results;
        ia_aiq_ae_results aec_results;
        ia_aiq_gbce_results gbce_results;
        ia_aiq_sa_results_v1 sa_results;
    } aiq_results;
    aiq_results *mAiqResults;
};
}//cca
