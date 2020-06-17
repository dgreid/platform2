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

#ifndef IA_PAL_TYPES_ISP_H_
#define IA_PAL_TYPES_ISP_H_

#ifdef __cplusplus
extern "C"{
#endif

/*!
 * \brief Common header for all PAL output data structures.
 */
typedef struct
{
    int uuid;                   /*!< UUID of PAL output. Indicates, which ISP block configuration this record contains. */
    int size;                   /*!< Size of PAL output for a particular kernel. */
    bool update;                /*!< Flag indicating if PAL calculations updated results. */
    char enable;                /*!< Three-state kernel enable (passthrough, enable, disable) */
    unsigned int run_time_diff; /*!< Time difference since these PAL results were previously calculated. */
    unsigned short width;       /*!< Input width of frame for this ISP block. */
    unsigned short height;      /*!< Input height of frame for this ISP block. */
} ia_pal_record_header;


#ifdef __cplusplus
}
#endif

#endif /* IA_PAL_TYPES_ISP_H_ */
