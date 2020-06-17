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
 * \file ia_aiqb_encode.h
 * \brief Helper functtions to encode records into AIQB.
 */


#ifndef IA_AIQB_ENCODE_H_
#define IA_AIQB_ENCODE_H_

#include "ia_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NUM_MAPS 20
typedef struct
{
    ia_aiqd_parser_offset_map offset_maps[MAX_NUM_MAPS];
    ia_aiqd_parser_maps_info maps_info;
} pointer_map;

/*!
 * \brief Copies data from source data pointer to target data pointer and updates pointer address
 * param[in]     data_end     End address of output buffer. Used to make sure that data is not copied beyond allocated memory.
 * param[in]     data_input   Input buffer to copy.
 * param[in]     data_size    Size of data to copy.
 * param[in/out] data_current Target data pointer (where to copy data).
 * param[in/out] data_target  Target address pointer (where to update address of copied data).
 */
ia_err memory_assign_and_copy(
    const char *data_end,
    const void *data_input,
    size_t data_size,
    char **data_current,
    char **data_target);

ia_err update_pointer_map(
    const char *data_start,
    const char *pointer_to_pointer,
    const char *pointer_to_data,
    pointer_map *maps);

ia_err append_pointer_map(
    char *data_start,
    char *data_end,
    pointer_map *maps,
    char **data_current);

#ifdef __cplusplus
}
#endif

#endif /* IA_AIQB_ENCODE_H_ */
