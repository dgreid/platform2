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


#ifndef __PVL_PERSPECTIVE_CONTROL_H__
#define __PVL_PERSPECTIVE_CONTROL_H__

/** @file    pvl_perspective_control.h
 *  @brief   This file declares the structures and native APIs of perspective control component.
 */

#include "pvl_types.h"
#include "pvl_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_VANISHING_POINT 3
#define MAX_VERTEX_POINT 4

/** @brief The enumerated values to specify correcting mode
 *
 *  This enumeration indicates setting of how the perspective_control engine conducts image warping.
 */
typedef enum {
    PC_SCAN_MODE = 0,   /**< Suitable for using indoor scenes */
    PC_BUILDING_MODE,   /**< Suitable for using outdoor scenes */
    NUM_PC_MODES
} pvl_perspective_control_mode;

typedef enum
{
    PC_ORI_VERTICAL = 0,    /**< The orientation for the vertical direction */
    PC_ORI_HORIZONTAL,      /**< The orientation for the horizontal direction */
    NUM_ORI
} pvl_pc_orientation;

struct pvl_pc_correction_range
{
    float min_vertical;     /**< The minimum value of available correction range in vertical orientation */
    float max_vertical;     /**< The maximum value of available correction range in vertical orientation */
    float min_horizontal;   /**< The minimum value of available correction range in horizontal orientation */
    float max_horizontal;   /**< The maximum value of available correction range in horizontal orientation */
};
typedef struct pvl_pc_correction_range pvl_pc_correction_range;

/** @Deprecated
 * @brief A structure to hold run-time configurable parameters for this component.
 *
 *  The variables in this structure will be retrieved and assigned, via pvl_perspective_control_get_parameters() and pvl_perspective_control_set_parameters() respectively.
 */
struct pvl_perspective_control_parameters {

    pvl_perspective_control_mode  control_mode;
                                      /* To choose mode of perspective control(the PC engine recommends direction and intensity)
                                         If a user wants to use this engine for indoor usage, then set this parameter PC_SCAN_MODE.
                                         If a user wants to change the intensity or to choose a certain direction(a certain direction among vanishig points), then set this paramter PC_MANUAL_OUTDOOR_MODE.
                                         [Default : PC_AUTO_OUTDOOR_MODE]
                                      */
    // Intensity and orientation of perspective correction ('correction_range' parameters will be applied only if 'control_mode' is PC_MANUAL_OUTPUT_MODE)
    float vertical_correction_range;         /* For vertical direction
                                              [Valid Range: -1.0 ~ 1.0] [Default: 0]
                                              -1 means full perspective control for the case that the vanishing point is located down direction.
                                               0 means no perspective control to maintain input image.
                                               1 means full perspective control for the case that the vanishing point is located up direction.
                                              */
    float horizontal_correction_range;       /* For horizontal direction
                                               [Valid Range: -1.0 ~ 1.0] [Default: 0]
                                              -1 means full perspective control for the case that the vanishing point is located left direction.
                                               0 means no perspective control to maintain input image.
                                               1 means full perspective control for the case that the vanishing point is located right direction.
                                              */
};
typedef struct pvl_perspective_control_parameters pvl_perspective_control_parameters;

/** @brief A structure to supply the public information of this component.
 *
 *  This structure represents the perspective_control instance which is used as the handle over most of API.
 *  It holds its own properties, constant context information.
 */
struct pvl_perspective_control {
    const pvl_version                  version;                   /**< The version information. */
    const int                          max_vanishing_point;       /**< The maximum configurable value of the number of vanishing point from input image.
                                                                          Refers to MAX_VANISHING_POINT. (This value is not modifiable in any purpose) */
    const pvl_perspective_control_mode  default_control_mode;      /**< Default value of the 'PC_AUTO_OUTDOOR_MODE' */
    const int                          default_limit_angle;       /**< Default value of limited angle: 20 degree */
};
typedef struct pvl_perspective_control pvl_perspective_control;


/** @ Deprecated
 * @brief A structure to hold the analysis data of vanishing points from input image.
 */
struct pvl_vanishing_orientation {
    int       angle;                                /**< (To divide an input image into 4 quadrants and locate the origin on the centre point of the image.)
                                                       The value is degree of each vanishing point in a counter-clockwise rotation from X axis.*/
    pvl_bool  is_outside;                           /**< This value is related to how far each vanishing point is from the centre point.
                                                         'pvl_true' means this vanishing point is located outside of an image.
                                                         'pvl_true' means this vanishing point is located inside of an image.
                                                         if this value is false, then the image warping based on this target VaP can cause too severe image warping*/
};
typedef struct pvl_vanishing_orientation pvl_vanishing_orientation;

struct pvl_perspective_data {
    int                       num_vanishing_points;                        /**< The number of vanishing point of the input image */
    pvl_vanishing_orientation vanishing_point[MAX_VANISHING_POINT];        /**< Information of each vanishing point.
                                                                            Users can choose the vanishing point based on angle. */
};
typedef struct pvl_perspective_data pvl_perspective_data;

/** @brief A structure to hold the outcomes from this component.
 */
struct pvl_perspective_control_result{
    pvl_image         output_image;        /**< The final output image. The component manages the memory. Do not free the internal data buffer of this structure.
                                                 After resetting or destroying the 'perspective_control' component handle, this data would not be reachable. */
    pvl_rect crop_hint;
    float v_correction_value;
    float h_correction_value;
};
typedef struct pvl_perspective_control_result pvl_perspective_control_result;



/** @brief Get default configuration of this component.
 *
 *  This function returns default configuration of the perspective_control component.
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
pvl_perspective_control_get_default_config(pvl_config *config);


/** @brief Create an instance of the perspective_control component.
 *
 *  This function initializes and returns an instance of this component.
 *  Multiple instances are allowed to be created concurrently.
 *
 *  @param[in]  config  The configuration information of the component.
 *  @param[out] pc      A pointer to indicate the handle newly created.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_not_supported       Unsupported configuration.
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params.
 *  @retval     pvl_err_invalid_status      Invalid internal status of the handle. Failure in loading run-time library related to GPU or IPU acceleration.
 *  @retval     pvl_err_nomem               Failed to allocate the memory.
 */
PVLAPI pvl_err
pvl_perspective_control_create(const pvl_config *config, pvl_perspective_control **pc);


/** @brief Destroy the instance of this component.
 *
 *  @param[in]  pc   The handle of this component to be destroyed.
 */
PVLAPI void
pvl_perspective_control_destroy(pvl_perspective_control *pc);


/** @brief Reset the instance of this component.
 *
 *  All the internal states, the analysis information, the composed output image and context will be reset except the run-time parameters set by user.
 *  If there are any ongoing processes(maybe on another thread) it cancels them or waits until done.
 *
 *  @param[in]  pc      The handle of this component.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the params.
 */
PVLAPI pvl_err
pvl_perspective_control_reset(pvl_perspective_control *pc);

/** @brief Set the perspective control mode for the given instance.
 *
 *  Current control mode of given instance will be changed to the designated control mode.
 *  If the input mode is same with the previous mode, it will return pvl_success without changing the internal status of current instance.
 *  but if it's changed, it will reset all internal states. so user should perform the process_image() or process_frame() with new input image.
 *
 *  @param[in]  pc      The handle of perspective control instance.
 *  @parma[in]  mode    The new perspective mode want to set.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the arguments.
 *  @retval     pvl_err_not_supported       Passing not supported perspective control mode.
 */
PVLAPI pvl_err pvl_perspective_control_set_control_mode(pvl_perspective_control* pc, pvl_perspective_control_mode mode);

/** @brief Set and analyze input image for the perspective control.
 *
 *  Given image will be set as source image and perform analysis to detect characteristics depending on the current control mode.
 *  If the current control mode is PC_SCAN_MODE, it will try to find quadrangle points from the input image.
 *  And if the control mode is PC_BUILDING_MODE, it will try to find primary lines in both vertical and horizontal orientations.
 *  By calling this, user can get corresponding characteristics such as quadrangle points and primary lines using other APIs.
 *
 *  @param[in]  pc          The handle of perspective control instance.
 *  @parma[in]  input_image The image that want to be used as source image to the perspective control.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the arguments.
 *  @retval     pvl_err_not_supported       When the image format of input_image is not supported format.
 *                                          Supported image formats are @refs pvl_image_format_nv12 and @refs pvl_image_format_rgba32
 */
PVLAPI pvl_err pvl_perspective_control_process_image(pvl_perspective_control* pc, pvl_image* input_image);

/** @brief Set and analyze input image for the perspective control.
 *
 *  The most of funtionality is same with process_image(). but it is designed to use for the preview processing.
 *  In the input image analysis phase, it will try to detect characteristics using the sequance of input frames.
 *  It means that the input image analysis will be performed partially. so the corresponding characteristics could not be retrived
 *  after calling this API.
 *
 *  @param[in]  pc          The handle of perspective control instance.
 *  @param[in]  input_frame The image that want to be used as source image to analyze characteristics for the perspective control.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the arguments.
 *  @retval     pvl_err_not_supported       When the image format of input_image is not supported format.
 *                                          Supported image formats are @refs pvl_image_format_nv12 and @refs pvl_image_format_rgba32
 */
PVLAPI pvl_err pvl_perspective_control_process_frame(pvl_perspective_control* pc, pvl_image* input_frame);

/** @brief Get perspective corrected image with selected 'control_mode'.
 *
 *  This API will produce perspective-corrected output image using the configured parameters.
 *  In PC_SCAN_MODE, detected quadrangle points or configured one by user will be used for the image warping.
 *  And horizontal/vertical correction values and primary lines will be used for the correction of PC_BUILDING_MODE.
 *  This API should be called after calling process_image() or process_frame().
 *  The output image could have blank area since source image will be warped in vertical and/or horizontal direction.
 *  so, the crop_hint in the result structure could be used to get the soild image from the output image by cutting out the blank area.
 *
 *  @param[in]  pc      The handle of perspective control instance.
 *  @param[out]  result  The final output contains an output image. The component manages the memory. Do not allocate or free the internal 'data' buffer in 'output_image'.
 *                      After resetting or destroying the component handle this data would be not reachable.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the arguments.
 *  @retval     pvl_err_invalid_status      If it called before calling process_image() or process_frame().
 *  @retval     pvl_err_not_supported       When the image format of input_image is not supported format.
 *                                          Supported image format is only @refs pvl_image_format_rgba32
 */
PVLAPI pvl_err pvl_perspective_control_get_warped_image(pvl_perspective_control* pc, pvl_perspective_control_result* result);

/** @brief Get the detected quadrangle points.
 *
 *  As a result of the process_image() or process_frame under the PC_SCAN_MODE, this API will return the detected quadrangle points.
 *  This API should be called after calling process_image() or process_frame().
 *  And available only for the PC_SCAN_MODE.
 *
 *  @param[in]  pc      The handle of perspective control instance.
 *  @parma[out] points  The array of pvl_point struct to be filled with detected quadrangle's points.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the arguments.
 *  @retval     pvl_err_not_supported       Called in the PC_BUILDING_MODE
 *  @retval     pvl_err_invalid_status      If it called before calling process_image() or process_frame().
 *  @retval     pvl_err_no_such_item        There is no detcted quadrangle from the latest result of process_image() or process_frame().
 */
PVLAPI pvl_err pvl_perspective_control_get_quadrangle(pvl_perspective_control* pc, pvl_point points[4]);

/** @brief Set the user customized quadrangle points.
 *
 *  If the output coodinates of get_quadrangle() API are not correct, user can adjust them using this API.
 *  Once quadrangle points were configured by this API, those coodinates will be used for the final processing of the perspective contorl
 *  This API should be called after calling process_image() or process_frame().
 *  And available only for the PC_SCAN_MODE.
 *
 *  @param[in]  pc      The handle of perspective control instance.
 *  @param[in]  points  The array of pvl_point struct which contains user-customized quadrangle points.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the arguments.
 *  @retval     pvl_err_not_supported       Called in the PC_BUILDING_MODE
 *  @retval     pvl_err_invalid_status      If it called before calling process_image() or process_frame().
 */
PVLAPI pvl_err pvl_perspective_control_set_quadrangle(pvl_perspective_control* pc, pvl_point points[4]);

/** @brief Get the available orientation of the input image.
 *
 *  As a result of process_image() or process_frame() in the PC_BUILDING_MODE, perspective control engine will detect available
 *  orientation of perspective correction. it will be represented to the available correction range and filled to the correction_range structure.
 *  This API should be called after calling process_image() or process_frame().
 *  And available only for the PC_BUILDING_MODE.
 *
 *  @param[in]  pc              The handle of perspective control instance.
 *  @param[out] available_range The correction range structure which will be filled with available correction range values.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the arguments.
 *  @retval     pvl_err_not_supported       Called in the PC_SCAN_MODE
 *  @retval     pvl_err_invalid_status      If it called before calling process_image() or process_frame().
 */
PVLAPI pvl_err pvl_perspective_control_get_available_correction_range(pvl_perspective_control* pc, pvl_pc_correction_range* available_range);

/** @brief Set the correction value for the given orientation.
 *
 *  To set the valid correction value, user should refer the result of get_available_correction_range() API.
 *  Valid range of 'value' is [pvl_pc_correction_range::min_vertical, pvl_pc_correction_range::max_vertical] where 'ori' is PC_ORI_VERTICAL,
 *  and [pvl_pc_correction_range::min_horizontal, pvl_pc_correction_range::min_horizontal] where 'ori' is PC_ORI_HORIZONTAL.
 *  Note taht tha zero value means no correction and 1.0 or -1.0 means the maximum correction.
 *  This API should be called after calling process_image() or process_frame().
 *  And available only for the PC_BUILDING_MODE.
 *
 *  @param[in]  pc      The handle of perspective control instance.
 *  @parma[in]  ori     The orientation for the correction value.
 *  @param[in]  value   The correction value to the given orientation.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null pointer to the arguments.
 *  @retval     pvl_err_not_supported       Called in the PC_SCAN_MODE or 'value' is out of avaliable range.
 */
PVLAPI pvl_err pvl_perspective_control_set_correction_value(pvl_perspective_control* pc, pvl_pc_orientation ori, float value);

/** @brief Get the information of primary lines.
 *
 *  If the available range have non-zero value for the vertical/horizontal orientation, user can get the primary lines for that orientation
 *  to provide guide-line to the user using this API.
 *  This API should be called after calling process_image() or process_frame().
 *  And available only for the PC_BUILDING_MODE.
 *
 *  @param[in]  pc      The handle of the perspective_control component.
 *  @param[in]  ori     The orientation for the primary lines.
 *  @param[out] line1   The array of pvl_point to be represented a line with two points.
 *  @param[out] line2   The array of pvl_point to be represented a line with two points.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null instance of pc.
 *  @retval     pvl_err_invalid_status      If it called before calling process_image() or process_frame().
 *  @retval     pvl_err_not_supported       Called in the PC_SCAN_MODE.
 *  @retval     pvl_err_nomem               Failed to allocate the internal memory buffers.
 *  @retval     pvl_err_no_such_item        There is no detected primary line from the source image.
 *                                          The source image may not have any vanishing point at the given orientation.
 */
PVLAPI pvl_err pvl_perspective_control_get_primary_lines(pvl_perspective_control* pc, pvl_pc_orientation ori, pvl_point line1[2], pvl_point line2[2]);

/** @brief Set the customized primary lines for the given orientation.
 *
 *  User can mannually customize primary lines for the perspective correction using this API.
 *  This API should be called after calling process_image() or process_frame().
 *  And available only for the PC_BUILDING_MODE.
 *
 *  @param[in]  pc      The handle of the perspective_control component.
 *  @param[in]  ori     The orientation for the primary lines.
 *  @param[in]  line1   The array of pvl_point to be represented a line with two points.
 *  @param[in]  line2   The array of pvl_point to be represented a line with two points.
 *
 *  @return     On success, @ref pvl_success.
 *  @return     On failure, @ref pvl_err error code, which will be the one of the following return value(s).
 *
 *  @retval     pvl_err_invalid_argument    Passing null instance of pc.
 *  @retval     pvl_err_not_supported       Called in the PC_SCAN_MODE.
 *  @retval     pvl_err_invalid_status      If it called before calling process_image() or process_frame().
 */
PVLAPI pvl_err pvl_perspective_control_set_primary_lines(pvl_perspective_control* pc, pvl_pc_orientation ori, pvl_point line1[2], pvl_point line2[2]);

PVLAPI pvl_err
pvl_perspective_control_enhance_contrast(pvl_perspective_control *pc, pvl_image *image);

#ifdef __cplusplus
}
#endif // __cplusplus

/** @example perspective_control_sample.c
 *  Sample of Perspective_control
 */

#endif /* __PVL_PERSPECTIVE_CONTROL_H__ */
