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


#ifndef __PVL_OBJECT_TRACKING_H__
#define __PVL_OBJECT_TRACKING_H__

/** @file    pvl_object_tracking.h
 *  @brief   This file declares the structure and native APIs of object tracking component.
 */

#include "pvl_types.h"
#include "pvl_config.h"


#ifdef __cplusplus
extern "C" {
#endif

/** @brief A structure to hold run-time configurable parameters for this component.
 *  The variables in this structure will be retrieved and asigned, via pvl_object_tracking_get_parameters() and pvl_object_tracking_set_parameters() respectively,
 */
struct pvl_object_tracking_parameters
{
    int32_t num_concurrent_tracking;                      /**< the number of maximum tracking context inside the handle */
    int32_t tracking_type;                                /**< the options about how the tracking started. Reserved for future usage. */
};

typedef struct pvl_object_tracking_parameters pvl_object_tracking_parameters;

/** @brief A structure to hold the outcomes from this component.
 */
struct pvl_object_tracking_result
{
    pvl_bool is_tracking_succeed;        /**< The tracking state of the object. */
    int32_t tracking_id;                 /**< The ID for a certain object starting at 1. This is unique among the sessions until the handle is destroyed. */
    int32_t score;                       /**< The tracking score of the object in the range of 0 to 100, where 0 means it doesn't like the object at all, and 100 means quite sure of that object. */
    pvl_rect tracked_region;             /**< The tracking area */
};

typedef struct pvl_object_tracking_result pvl_object_tracking_result;

/** @brief A structure to hold the run-time context of this component.
 *
 *  This structure represents the object tracking instance which is used as the handle over most of API.
 *  It holds its own properties, constant parameters and internal context inside.
 */
struct pvl_object_tracking
{
    const pvl_version version;                /**< The version information. */

    const int32_t max_supported_num_object;   /**< The maximum number of objects supported by this component. */
};

typedef struct pvl_object_tracking pvl_object_tracking;


/** @brief Get default configuration of this component.
 *
 *  This function returns default configuration of the face detection component.
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
pvl_object_tracking_get_default_config(pvl_config* config);


/** @brief Create an instance of the object tracking component.
 *
 *  This function initializes and returns an instance of this component.
 *  Multiple instances are allowed to be created concurrently.
 *
 *  @param[in]  config      The configuration information of the component.
 *  @param[out] ot          A pointer to indicate the handle newly created.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_not_supported       Unsupported configuration.
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params.
 *  @retval     pvl_err_nomem               Failed to allocate the memory.
 */
PVLAPI pvl_err
pvl_object_tracking_create(const pvl_config* config, pvl_object_tracking **ot);


/** @brief Destroy the instance of this component.
 *
 *  @param[in]  ot  The handle of this component to be destroyed.
 */
PVLAPI void
pvl_object_tracking_destroy(pvl_object_tracking *ot);


/** @brief Reset the instance of this component.
 *
 *  All the internal states including object tracking information and context will be reset except the run-time parameters set by user.
 *
 *  @param[in]  ot  The handle of this component.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params.
 */
PVLAPI pvl_err
pvl_object_tracking_reset(pvl_object_tracking *ot);


/** @brief Set run-time parameters of this component.
 *
 *  Set given parameters to the handle.
 *  It is required to get proper parameters instance by pvl_face_detection_get_parameters() before setting something.
 *
 *  @param[in]  ot      The handle of this component.
 *  @param[in]  params  The parameters to be set.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params or wrong value is specified in the params.
 */
PVLAPI pvl_err
pvl_object_tracking_set_parameters(pvl_object_tracking *ot, const pvl_object_tracking_parameters *params);


/** @brief Get current run-time parameters of this component.
 *
 *  Get the parameters from the handle.
 *  This function should be called before calling pvl_face_detection_set_parameters().
 *
 *  @param[in]  ot      The handle of this component.
 *  @param[out] params  The buffer which will hold parameters. Its allocation must be managed by the caller.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params.
 */
PVLAPI pvl_err
pvl_object_tracking_get_parameters(pvl_object_tracking *ot, pvl_object_tracking_parameters *params);


/** @brief Specify the start ROI for a new tracking session.
 *
 *  This function will initialize the object tracking in the input image.
 *  The tracking context will be created and kept in the handle, which is supposed to be used at next OT processing.
 *
 *  Caller is responsible for allocating the the buffer for the result.
 *
 *  @param[in]  ot          The handle of this component.
 *  @param[in]  image       The input image. Currently, only pvl_image_format_nv12 is supported.
 *  @param[in]  the_object  The ROI of the object to track.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null instance of ot or image or the object.
 *  @retval     pvl_err_nomem               Failed to allocate the internal memory buffers.
 *  @retval     pvl_err_not_supported       Unsupported image format specified.
 *  @retval     pvl_err_out_of_bound        Current number of tracking object is already full.
 */
PVLAPI pvl_err
pvl_object_tracking_add_object(pvl_object_tracking *ot, const pvl_image *image, const pvl_rect *the_object);


/** @brief Remove specified object from object tracking.
 *
 *  This function will stop tracking the object which have same tracking id with the input parameter.
 *  The tracking context will be removed in the handle.
 *
 *  Caller is responsible for allocating the pvl_rect.
 *
 *  @param[in]  ot          The handle of this component.
 *  @param[in]  tracking_id Tracking id of the object which will be removed.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null instance of ot or non-positive tracking_id.
 *  @retval     pvl_err_no_such_item        Failed to find tracking object having tracking_id.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 */
PVLAPI pvl_err
pvl_object_tracking_remove_object(pvl_object_tracking *ot, const int32_t tracking_id);


/** @brief Main function to run object tracking on all of the object in the input image as a part of preview or video frames.
 *
 *  This function will run object tracking on the input image with the tracking context held inside the OT handle.
 *  As this is a tracking component, there's an assumption this frame has temporal correlation with previous and next input images.
 *  The tracking result will be stored up to max_result, no matter how many contexts are inside handle.
 *
 *  Caller is responsible for allocating the buffer for result.
 *
 *  @param[in]  ot             The handle of this component.
 *  @param[in]  image          The input image. Currently, only pvl_image_format_n12 is supported.
 *  @param[out] result         Buffer to write the result back. Must have enough memory to hold the result.
 *  @param[in]  max_result     The number of results that the buffers can hold.
 *
 *  @return     On success, the number of current tracking objects.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null instance of ot or result, or non-positive max_result.
 *  @retval     pvl_err_not_supported       Unsupported image format specified.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 */
PVLAPI int32_t
pvl_object_tracking_run(pvl_object_tracking *ot, const pvl_image *image, pvl_object_tracking_result *result, int32_t max_result);

#ifdef __cplusplus
}
#endif // __cplusplus

/** @example object_tracking_sample.cpp
*  Sample of Object Tracking
*/

#endif /* __PVL_OBJECT_TRACKING_H__ */
