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


#ifndef __PD_PVL_PEDESTRIAN_DETECTION_H__
#define __PD_PVL_PEDESTRIAN_DETECTION_H__

/** @file    pvl_pedestrian_detection.h
 *  @brief   This file declares the structure and native APIs of pedestrian detection component.
 */

#include "pvl_types.h"
#include "pvl_config.h"

#ifdef __cplusplus
extern "C" {
#endif


/** @brief A structure to hold run-time configurable parameters for this component.
 *
 *  The variables in this structure will be retrieved and assigned via pvl_pedestrian_detection_get_parameters() and pvl_pedestrian_detection_set_parameters() respectively.
 */
struct pvl_pedestrian_detection_parameters
{
    int32_t max_num_pedestrians;                /* The maximum number of detectable pedestrian in one frame.
                                                max_supported_pedestrians in pvl_pedestrian_detection structure represents the maximum allowable value, and minimum allowable value set to 1.
                                                The default value is set to maximum when the component is created. */

    int32_t min_pedestrian_height;              /* The minimum height(pixel)size of detectable pedestrian on preview mode.
                                                It should be bigger than 'default_pedestrian_height'.
                                                It should be smaller than 'default_pedestrian_height'*2. */
};
typedef struct pvl_pedestrian_detection_parameters pvl_pedestrian_detection_parameters;


/** @brief A structure to hold the run-time context of this component.
 *
 *  This structure represents the pedestrian_detection instance which is used as the handle over most of API.
 *  It holds its own properties, constant parameters and internal context inside.
 */
struct pvl_pedestrian_detection_context
{
    const pvl_version version;                      /**< The version information. */

    const int32_t max_supported_num_pedestrians;    /**< The maximum number of pedestrians supported by this component. */
    const int32_t default_pedestrian_height;        /**< The default value of minimum detectable height(pixel) size.
                                                    Current version: 128 */
};
typedef struct pvl_pedestrian_detection_context pvl_pedestrian_detection_context;

/** @brief A structure to hold the outcomes from the Pedestrian Detection component.
*/
struct pvl_pedestrian_detection_result {
    pvl_rect rect;                  /**< The rectangular region of the detected pedestrian. */

    int32_t  confidence;            /**< The confidence value of the detected pedestrian. (0~100) */

    int32_t  tracking_id;           /**< The tracking id of the pedestrian. Only valid in the outcomes of pvl_pedestrian_detection_process_frame().
                                    The value will be unique throughout the component life cycle, unless pvl_pedestrian_detection_reset() is called. */
};
typedef struct pvl_pedestrian_detection_result pvl_pedestrian_detection_result;

/** @brief Get default configuration of this component.
 *
 *  This function returns default configuration of the pedestrina detection component.
 *  The returned configuration could be customized as per its usage.
 *
 *  @param[out] config  The structure to load default configuration.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params.
 */
PVLAPI pvl_err
pvl_pedestrian_detection_get_default_config(pvl_config *config);


/** @brief Create an instance of the pedestrina detection component.
 *
 *  This function initializes and returns an instance of this component.
 *  Multiple instances are allowed to be created concurrently.
 *
 *  @param[in]  config  The configuration information of the component.
 *  @param[out] pd      A pointer to indicate the handle newly created.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_not_supported       Unsupported configuration.
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 *  @retval     pvl_err_nomem               Failed to allocate the memory.
 */
PVLAPI pvl_err
pvl_pedestrian_detection_create(const pvl_config *config, pvl_pedestrian_detection_context **pd);


/** @brief Destroy the instance of this component.
 *
 *  @param[in]  pd   The handle of this component to be destroyed.
 */
PVLAPI void
pvl_pedestrian_detection_destroy(pvl_pedestrian_detection_context *pd);


/** @brief Reset the instance of this component.
 *
 *  All the internal states and context will be reset except the run-time parameters set by user.
 *
 *  @param[in]  pd  The handle of this component.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 */
PVLAPI pvl_err
pvl_pedestrian_detection_reset(pvl_pedestrian_detection_context *pd);


/** @brief Set run-time parameters of this component.
 *
 *  Set given parameters to the handle.
 *  It is required to get proper parameters instance by pvl_pedestrian_detection_get_parameters() before setting something.
 *
 *  @param[in]  pd      The handle of this component.
 *  @param[in]  params  The parameters to be set.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params or wrong value is specified in the params.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 */
PVLAPI pvl_err
pvl_pedestrian_detection_set_parameters(pvl_pedestrian_detection_context *pd, const pvl_pedestrian_detection_parameters *params);


/** @brief Get current run-time parameters of this component.
 *
 *  Get the parameters from the handle.
 *  This function should be called before calling pvl_pedestrian_detection_set_parameters().
 *
 *  @param[in]  pd      The handle of this component.
 *  @param[out] params  The buffer which will hold parameters. Its allocation must be managed by the caller.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 */
PVLAPI pvl_err
pvl_pedestrian_detection_get_parameters(pvl_pedestrian_detection_context *pd, pvl_pedestrian_detection_parameters *params);


/** @brief Detect pedestrian from an input image.
*
*  This function will conduct pedestrian detection.
*
*  @param[in]  pd          The handle of the pedestrian detection component.
*  @param[in]  image       The input image. Currently, pvl_image_format_rgba32 and pvl_image_format_nv12 are supported.
*  @param[out] result      The result buffer.
*  @param[in]  max_result  The number of 'result' that the buffers can hold.
*
*  @return     On success, @ref integer    Number of the detected pedestrians from the input image(positive integer).
*  @return     On failure, @ref pvl_err    Error code(negative integer), which will be the one of the following return value(s).
*
*  @retval     pvl_err_not_supported       Unsupported image format specified.
*  @retval     pvl_err_invalid_argument    Passing null pointer/negative value to the params or non-matching values in num_rois and rois.
*  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
*  @retval     pvl_err_nomem               Failed to allocate the internal memory buffers.
*/
PVLAPI int32_t
pvl_pedestrian_detection_process_image(pvl_pedestrian_detection_context *pd, const pvl_image* image, pvl_pedestrian_detection_result* result, int max_result);


/** @brief process continuous frames for conducting pedestrian detection.
*
*  This function will conduct pedestrian detection.
*
*  @param[in]  pd           The handle of the pedestrian detection component.
*  @param[in]  image       The input image. Currently, pvl_image_format_rgba32 and pvl_image_format_nv12 are supported.
*  @param[out] result      The result buffer.
*  @param[in]  max_result  The number of 'result' that the buffer can hold.
*
*  @return     On success, @ref integer.   The number of the detected pedestrians at the current input frame(positive integer).
*  @return     On failure, @ref pvl_err    Error code, which will be the one of the following return value(s).
*
*  @retval     pvl_err_not_supported       Unsupported image format specified.
*  @retval     pvl_err_invalid_argument    Passing null pointer/negative value to the params or non-matching values in num_rois and rois.
*  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
*  @retval     pvl_err_nomem               Failed to allocate the internal memory buffers.
*/
PVLAPI int32_t
pvl_pedestrian_detection_process_frame(pvl_pedestrian_detection_context *pd, const pvl_image *image, pvl_pedestrian_detection_result* result, int max_result);

#ifdef __cplusplus
}
#endif  // __cplusplus

/** @example pedestrian_detection_sample.cpp
 *  Sample of Pedestrian Detection
 */

#endif  // __PD_PVL_PEDESTRIAN_DETECTION_H__
