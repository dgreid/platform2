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

/*! \struct tnr7_bc_1_0
*/
typedef struct
{
/*!< enable block enable*/
    int32_t enable;
    /*!< is_first_frame If first frame, ignore input rec-sim*/
    int32_t is_first_frame;
    /*!< do_update Limit of S&R parameter update mechanism*/
    int32_t do_update;
    /*!< coeffs[3] Per-region mult-and-clamp coefficients*/
    int32_t coeffs[3];
    /*!< gpu_mode meta parameter controlling convolution implementation: 0 - HW implementation  1 - GPU implementation*/
    int32_t gpu_mode;
    /*!< tune_sensitivity user tuning - alignment-conf sensitivity*/
    int32_t tune_sensitivity;
    /*!< global_protection global protection enable*/
    int32_t global_protection;
    /*!< global_protection_sensitivity_lut_values[3] global protection - LUT values*/
    int32_t global_protection_sensitivity_lut_values[3];
    /*!< global_protection_sensitivity_lut_slopes[2] global protection - LUT slopes*/
    int32_t global_protection_sensitivity_lut_slopes[2];
    /*!< global_protection_motion_level average alignment conf of previous frame*/
    int32_t global_protection_motion_level;

} tnr7_bc_1_0_t;

/*! \struct tnr7_blend_1_0

*/
typedef struct
{
/*!< enable Enable TNR7 blend*/
    int32_t enable;
    /*!< enable_main_output Enable blend main output*/
    int32_t enable_main_output;
    /*!< enable_vision_output Enable blend computer vision output*/
    int32_t enable_vision_output;
    /*!< single_output_mode Both outputs use the same blend*/
    int32_t single_output_mode;
    /*!< spatial_weight_coeff Spatial weight coeff to be used in single_output_mode*/
    int32_t spatial_weight_coeff;
    /*!< max_recursive_similarity Maximum value of recursive similarity*/
    int32_t max_recursive_similarity;
    /*!< spatial_alpha Use of spatial filtering in the feedback output*/
    int32_t spatial_alpha;
    /*!< w_out_prev_LUT[32] Weight of reference in the main output*/
    int32_t w_out_prev_LUT[32];
    /*!< w_out_spl_LUT[32] Weight of spatial in the main output*/
    int32_t w_out_spl_LUT[32];
    /*!< output_cu_x[6] output config unit - x values*/
    int32_t output_cu_x[6];
    /*!< output_cu_a[5] output config unit - slope values*/
    int32_t output_cu_a[5];
    /*!< output_cu_b[5] output config unit - b values*/
    int32_t output_cu_b[5];
    /*!< max_recursive_similarity_vsn Vision - Maximum value of recursive similarity*/
    int32_t max_recursive_similarity_vsn;
    /*!< w_vsn_out_prev_LUT[32] Vision - Weight of reference in the main output*/
    int32_t w_vsn_out_prev_LUT[32];
    /*!< w_vsn_out_spl_LUT[32] Vision - Weight of spatial in the main output*/
    int32_t w_vsn_out_spl_LUT[32];

} tnr7_blend_1_0_t;

/*! \struct tnr7_ims_1_0

*/
typedef struct
{
/*!< enable block enable*/
    int32_t enable;
    /*!< update_limit Limit of S&R parameter update mechanism*/
    int32_t update_limit;
    /*!< update_coeff S&R parameter update coefficient*/
    int32_t update_coeff;
    /*!< d_ml[16] Maximum-likelihood of distance distribution*/
    int32_t d_ml[16];
    /*!< d_slopes[16] Distance log-likelihood slopes*/
    int32_t d_slopes[16];
    /*!< d_top[16] Distance log-likelihood constants*/
    int32_t d_top[16];
    /*!< gpu_mode meta parameter for controlling convolution implementation: 0 - HW implementation  1 - GPU implementation*/
    int32_t gpu_mode;
    /*!< outofbounds[16] Is ml value out-of-hostogram-bounds?*/
    int32_t outofbounds[16];

} tnr7_ims_1_0_t;

/*! \struct tnr_scale_1_0

*/
typedef struct
{
/*!< enable enable TNR_SCALE_1_0 filter*/
    int32_t enable;
    /*!< inWidth Number of pixels per row at output*/
    int32_t inWidth;
    /*!< inHeight Number of rows at output*/
    int32_t inHeight;
    /*!< bitReductionBypass bypass of bit reduction of input pixels*/
    int32_t bitReductionBypass;
    /*!< inputShift shift on input pixels*/
    int32_t inputShift;
    /*!< cu_bit_reduce_x[6] input bit reduction config unit - x values*/
    int32_t cu_bit_reduce_x[6];
    /*!< cu_bit_reduce_y[5] input bit reduction config unit - y values*/
    int32_t cu_bit_reduce_y[5];
    /*!< cu_bit_reduce_slope[5] input bit reduction config unit - slope values*/
    int32_t cu_bit_reduce_slope[5];

} tnr_scale_1_0_t;
