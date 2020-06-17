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
#include "ia_aiq_types.h"
#include "ia_ltm_types.h"
#include "ia_dvs_types.h"
#include "ia_isp_bxt_types.h"
#include "ia_cmc_types.h"
#include "ia_view_types.h"
#include "ia_bcomp_types.h"

/*!
 *\brief Enumeration for direct results UUIDs.
 * UUIDs are used when fetching direct results from InputData class.
 */
typedef enum
{
    ia_pal_direct_results_uuid_start,
    ia_pal_direct_results_uuid_aiq_sa_results = ia_pal_direct_results_uuid_start,
    ia_pal_direct_results_uuid_aiq_hist_weight_grid,
    ia_pal_direct_results_uuid_ltm_drc_params,
    ia_pal_direct_results_uuid_dvs_morph_table,
    ia_pal_direct_results_uuid_ltm_results,
    ia_pal_direct_results_uuid_cmc_phase_difference,
    ia_pal_direct_results_uuid_cmc_general_data,
    ia_pal_direct_results_uuid_dvs_image_transformation,
    ia_pal_direct_results_uuid_cmc_parsed_geometric_distortion2,
    ia_pal_direct_results_uuid_cmc_lateral_chromatic_aberration_correction,
    ia_pal_direct_results_uuid_cmc_optomechanics,
    ia_pal_direct_results_uuid_view_params,
    ia_pal_direct_results_uuid_bcomp_results,
    ia_pal_direct_results_uuid_count, /* Keep this last */
} ia_pal_direct_results_uuid;

/*!
 * \brief Direct results coming from algorithms without conversion to algo API structures.
 * In case GAIC is not used, for improved PnP, it's not needed to copy large buffers from algorithm results to
 * algo API structures. Define a structure, where algorithm results can be passed directly to PAL.
 * ia_pal_direct_results structure must contain only pointers to other structures (no copied structures)!
 */
typedef struct
{
    ia_aiq_sa_results_v1 const *direct_ia_aiq_sa_results_v1;
    ia_aiq_hist_weight_grid const *direct_ia_aiq_hist_weight_grid;
    ia_ltm_drc_params const *direct_ia_ltm_drc_params;
    ia_dvs_morph_table const *direct_ia_dvs_morph_table;
    ia_ltm_results const *direct_ia_ltm_results;
    cmc_phase_difference_t const *direct_cmc_phase_difference_t;
    cmc_general_data_t const *direct_cmc_general_data_t;
    ia_dvs_image_transformation const *direct_ia_dvs_image_transformation;
    cmc_parsed_geometric_distortion2_t const *direct_cmc_parsed_geometric_distortion2_t;
    cmc_lateral_chromatic_aberration_correction const *direct_cmc_lateral_chromatic_aberration_correction;
    cmc_optomechanics_t const *direct_cmc_optomechanics_t;
    ia_view_t const *direct_ia_view_t;
    ia_bcomp_results const *direct_ia_bcomp_results;
    cmc_parsed_sensor_decompand_t const *direct_cmc_parsed_sensor_decompand_t;
    cmc_parsed_pipe_compand_t const *direct_cmc_parsed_pipe_compand_t;
    cmc_parsed_pipe_decompand_t const* direct_cmc_parsed_pipe_decompand_t;
} ia_pal_direct_results;
