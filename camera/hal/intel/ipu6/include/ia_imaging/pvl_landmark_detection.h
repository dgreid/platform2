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


#ifndef __PVL_LANDMARK_DETECTION_H__
#define __PVL_LANDMARK_DETECTION_H__

/** @file    pvl_landmark_detection.h
 *  @brief   This file declares the structures and native APIs of facial landmark detection component.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "pvl_types.h"
#include "pvl_config.h"

/** @brief The enumerated values to specify the facial landmark shape points.
*/
enum pvl_facial_landmark_shape_point_num {
    pvl_facial_landmark_shape27 = 27,           /**< 27 point */
    pvl_facial_landmark_shape50 = 50,           /**< 50 point */
    pvl_facial_landmark_shape78 = 78,           /**< 78 point */
};
typedef enum pvl_facial_landmark_shape_point_num pvl_facial_landmark_shape_point_num;

/** @brief A structure to hold the outcomes from this component.
 */
struct pvl_facial_landmark_detection_result
{
    float points[pvl_facial_landmark_shape78 * 2];
};
typedef struct pvl_facial_landmark_detection_result pvl_facial_landmark_detection_result;


/** @brief A structure to hold the run-time context of this component.
 *
 *  This structure represents the facial landmark detection instance which is used as the handle over most of API.
 *  It holds its own properties, constant parameters and internal context inside.
 */
struct pvl_facial_landmark_detection
{
    const pvl_version version;              /**< The version information. */
    pvl_facial_landmark_shape_point_num shape_point_num;
};
typedef struct pvl_facial_landmark_detection pvl_facial_landmark_detection_context;


/** @brief Get default configuration of this component.
 *
 *  This function returns default configuration of the facial landmark detection component.
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
pvl_facial_landmark_detection_get_default_config(pvl_config* config);


/** @brief Create an instance of the facial landmark detection component.
 *
 *  This function initializes and returns an instance of this component.
 *  Multiple instances are allowed to be created concurrently.
 *
 *  @param[in]  config  The configuration information of the component.
 *  @param[out] ed      A pointer to indicate the handle newly created.
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
pvl_facial_landmark_detection_create(const pvl_config* config, pvl_facial_landmark_detection_context** fld, const pvl_facial_landmark_shape_point_num point_num = pvl_facial_landmark_shape78);


/** @brief Destroy the instance of this component.
 *
 *  @param[in]  ed   The handle of this component to be destroyed.
 */
PVLAPI void
pvl_facial_landmark_detection_destroy(pvl_facial_landmark_detection_context* fld);

/** @brief Detect facial landmark positions from one of face in the image.
 *
 *  This function will do the facial landmark detection in the given face in the image.
 *  The function caller is responsible for allocation of the buffer for result.
 *
 *  @param[in]  fld             The handle of the facial landmark detection component.
 *  @param[in]  image           The input image for detecting the positions of facial landmark detection. All image formats are supported.
 *                              pvl_image_format_gray, pvl_image_format_nv12, pvl_image_format_nv21 and pvl_image_format_yv12 are preferred in terms of speed.
 *  @param[in]  face_regions    The struct of rectangular region of the face.
 *  @param[in]  rip_angles      The value of RIP (Rotation in Plane) of the face in degree.
 *  @param[out] result          The result buffer.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_not_supported       Unsupported image format specified.
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle.
 *  @retval     pvl_err_nomem               Failed to allocate the internal memory buffers.
 */

PVLAPI pvl_err pvl_facial_landmark_detection_run(pvl_facial_landmark_detection_context* fld,
    const pvl_image* image, pvl_rect face_region, int32_t rip_angle,
    pvl_facial_landmark_detection_result* result);

#ifdef __cplusplus
}
#endif

#endif // __PVL_LANDMARK_DETECTION_H__
