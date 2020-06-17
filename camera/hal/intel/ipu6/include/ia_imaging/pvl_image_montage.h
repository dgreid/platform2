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


#ifndef __PVL_IMAGE_MONTAGE_H__
#define __PVL_IMAGE_MONTAGE_H__

/** @file    pvl_image_montage.h
 *  @brief   This file declares the structures and native APIs of image montage component.
 */

#include "pvl_types.h"
#include "pvl_config.h"

#ifdef __cplusplus
extern "C" {
#endif


/** @brief A structure to hold run-time configurable parameters for this component.
 *
 *  The variables in this structure will be retrieved and assigned, via pvl_image_montage_get_parameters() and pvl_image_montage_set_parameters() respectively.
 */
struct pvl_image_montage_parameters
{
    int search_region_margin_percentage;    /**< The percentage of the region to be searched to the entire sub image (default: 20). */
};
typedef struct pvl_image_montage_parameters pvl_image_montage_parameters;


/** @brief A structure to hold the run-time context of this component.
 *
 *  This structure represents the image montage instance which is used as the handle over most of API.
 *  It holds its own properties, constant parameters and internal context inside.
 */
struct pvl_image_montage
{
    const pvl_version version;              /**< The version information. */

    const int default_search_region_margin; /**< The default percentage of the region to be search to the entire sub image (recommended to use) */
};
typedef struct pvl_image_montage pvl_image_montage;


/** @brief Get default configuration of this component.
 *
 *  This function returns default configuration of the image montage component.
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
pvl_image_montage_get_default_config(pvl_config *config);


/** @brief Create an instance of the image montage component.
 *
 *  This function initializes and returns an instance of this component.
 *  Multiple instances are allowed to be created concurrently.
 *
 *  @param[in]  config  The configuration information of the component.
 *  @param[out] im      A pointer to indicate the handle newly created.
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
pvl_image_montage_create(const pvl_config *config, pvl_image_montage **im);


/** @brief Destroy the instance of this component.
 *
 *  @param[in]  im   The handle of this component to be destroyed.
 */
PVLAPI void
pvl_image_montage_destroy(pvl_image_montage *im);


/** @brief Reset the instance of this component.
 *
 *  All the internal states and context will be reset except the run-time parameters set by user.
 *
 *  @param[in]  im  The handle of this component.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 *  @retval     pvl_err_nomem               Failed to re-allocate the memory.
 */
PVLAPI pvl_err
pvl_image_montage_reset(pvl_image_montage *im);


/** @brief Set run-time parameters of this component.
 *
 *  Set given parameters to the handle.
 *  It is required to get proper parameters instance by pvl_image_montage_get_parameters() before setting something.

 *  @param[in]  im      The handle of this component.
 *  @param[in]  params  The parameters to be set.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params or wrong value is specified in the params.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 *  @retval     pvl_err_nomem               Failed to re-allocate the memory for parameter changes.
 */
PVLAPI pvl_err
pvl_image_montage_set_parameters(pvl_image_montage *im, const pvl_image_montage_parameters *params);


/** @brief Get current run-time parameters of this component.
 *
 *  Get the parameters from the handle.
 *  This function should be called before calling pvl_image_montage_set_parameters().
 *
 *  @param[in]  im      The handle of this component.
 *  @param[out] params  The buffer which will hold parameters. Its allocation must be managed by the caller.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 */
PVLAPI pvl_err
pvl_image_montage_get_parameters(pvl_image_montage *im, pvl_image_montage_parameters *params);


/** @brief Set the main image.
 *
 *  This functions stores information about the main image.
 *  The main image is labelled as the "background" of image montage.
 *  Input image data and an array of the objects coordinates which have been stored into internal
 *  memory by this function could be used when the function pvl_image_montage_run() runs.
 *
 *  @param[in]  im           The handle of the image montage component.
 *  @param[in]  main_img     The main image. Currently, only pvl_image_format_rgba32 is supported.
 *  @param[in]  objects      The coordinates of the objects.
 *  @param[in]  num_objects  The number of objects.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null instance of im.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 */
PVLAPI pvl_err
pvl_image_montage_set_main_image(pvl_image_montage *im, const pvl_image *main_img, pvl_rect *objects, int num_objects);


/** @brief Set the sub image where the object patches come from.
 *
 *  This functions stores information about the sub image.
 *  The sub image contains the target objects which would be fetched in composing function.
 *  Input image data and an array of the objects coordinates which have been stored by this
 *  function could be used when the function pvl_image_montage_run() runs.
 *
 *  @param[in]  im           The handle of the image montage component.
 *  @param[in]  sub_img      The sub image. Currently, only pvl_image_format_rgba32 is supported.
 *  @param[in]  objects      The coordinates of the objects.
 *  @param[in]  num_objects  The number of objects.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null instance of im.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 */
PVLAPI pvl_err
pvl_image_montage_set_sub_image(pvl_image_montage *im, const pvl_image *sub_img, pvl_rect *objects, int num_objects);


/** @brief Compose the montage on the main image, cropping the patch from the sub image.
 *
 *  This function is to combine two images(main,sub), cropping an object image patch which is placed on the N-th coordinate
 *  of object array in the sub image, overlaying the patch onto the N-th coordinate of objects array in the main image.
 *
 *  @param[in]  im           The handle of the image montage component.
 *  @param[in]  idx_on_main  The index of the object on the main image.
 *  @param[in]  idx_on_sub   The index of the object on the sub image.
 *  @param[out] result       The composed image. Must allocate memory big enough as the main image size to data field of 'result' before this function is called.
 *                           The image format should be same as the formats of main/sub images, which is currently pvl_image_format_rgba32 only.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null instance of im.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 */
PVLAPI pvl_err
pvl_image_montage_run(pvl_image_montage *im, int idx_on_main, int idx_on_sub, pvl_image *result);


#ifdef __cplusplus
}
#endif // __cplusplus

/** @example image_montage_sample.c
 *  Sample of Image Montage
 */

#endif /* __PVL_IMAGE_MONTAGE_H__ */
