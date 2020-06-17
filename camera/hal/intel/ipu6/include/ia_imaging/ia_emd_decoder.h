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

/*!
 * \file ia_emd_decoder.h
 * \brief Definitions of functions in Embedded Data decoder.
*/

#ifndef _IA_EMD_DECODER_H_
#define _IA_EMD_DECODER_H_

#include "ia_aiq_types.h"
#include "ia_emd_types.h"
#include "ia_types.h"
#include "ia_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef LOG_EMD
#define IA_EMD_LOG(fmt, ...) IA_LOG(ia_log_debug, "IAEMD: " fmt, ## __VA_ARGS__)
#define IA_EMD_LOG_ERROR(fmt, ...) IA_LOG(ia_log_error, "IAEMD: " fmt, ## __VA_ARGS__)
#define IA_EMD_LOG_INFO(fmt, ...) IA_LOG(ia_log_info, "IAEMD: " fmt, ## __VA_ARGS__)
#else
#define IA_EMD_LOG(...) ((void)0)
#define IA_EMD_LOG_ERROR(...) ((void)0)
#define IA_EMD_LOG_INFO(...) ((void)0)
#endif


/*!
 * \brief Creates Embedded Data Decoder.
 *
 * \param[in] ia_cmc                        Mandatory.\n
 *                                          Parsed camera module characterization structure. Internal copy of the structure will be taken.
 * \return                                  Pointer to Embedded Data Decoder handle.
 */
LIBEXPORT ia_emd_decoder_t *
ia_emd_decoder_init(
    const ia_cmc_t *ia_cmc);


/*!
 * \brief Deletes Sensor Data Decoder.
 *
 * \param[in] emd_decoder                   Mandatory. \n
 *                                          Pointer to decoder handle.
 * \return                                  None.
 */
LIBEXPORT void
ia_emd_decoder_deinit(ia_emd_decoder_t *emd_decoder);


/*!
 * \brief Runs Sensor Data Decoder.
 *
 * \param[in] emd_bin                       Mandatory. \n
 *                                          Pointer to sensor embedded data binary blob.
 * \param[in] emd_mode                      Mandatory. \n
 *                                          Pointer to sensor embedded data run-time configuration.
 * \param[in] sensor_descriptor             Mandatory. \n
 *                                          Pointer to sensor specific descriptor.
 * \param[in/out] emd_decoder               Mandatory. \n
                                            Pointer to decoder handle. Contains decoded exposure data as well.
 * \return                                  Error code.
 */
LIBEXPORT ia_err
ia_emd_decoder_run(
    const ia_binary_data *emd_bin,
    const ia_emd_mode_t *emd_mode,
    const ia_aiq_exposure_sensor_descriptor *sensor_descriptor,
    ia_emd_decoder_t *emd_decoder);

#ifdef __cplusplus
}
#endif

#endif /* _IA_EMD_DECODER_H_ */
