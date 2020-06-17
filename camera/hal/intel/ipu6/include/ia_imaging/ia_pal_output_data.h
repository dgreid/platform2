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
#include <map>
#include "ia_pal_types_isp_ids_autogen.h"
#include "ia_isp_bxt_types.h"
#include "ia_types.h"

class PalOutputData
{
public:
    PalOutputData(const ia_isp_bxt_program_group *pg);
    ~PalOutputData();
    ia_err setPublicOutput(ia_binary_data *output);
    ia_err getKernelPublicOutput(ia_pal_uuid id, void *&result) const;

private:
    ia_binary_data m_public_output;
    std::map<ia_pal_uuid, unsigned int> m_public_output_offsets;
};
