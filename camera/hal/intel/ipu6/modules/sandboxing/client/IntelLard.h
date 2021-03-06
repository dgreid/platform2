/*
 * Copyright (C) 2019-2020 Intel Corporation.
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

#include <ia_lard.h>

#include <vector>

#include "IntelAlgoCommon.h"
#include "modules/sandboxing/IPCIntelLard.h"

namespace icamera {
class IntelLard {
 public:
    IntelLard();
    virtual ~IntelLard();

    ia_lard* init(const ia_binary_data* lard_data_ptr);
    ia_err getTagList(ia_lard* ia_lard_ptr, unsigned int mode_tag, unsigned int* num_tags,
                      const unsigned int** tags);
    ia_err run(ia_lard* ia_lard_ptr, ia_lard_input_params* lard_input_params_ptr,
               ia_lard_results** lard_results_ptr);
    void deinit(ia_lard* ia_lard_ptr);

 private:
    IPCIntelLard mIpc;
    IntelAlgoCommon mCommon;

    bool mInitialized;

    ShmMemInfo mMemInit;
    ShmMemInfo mMemGetTagList;
    ShmMemInfo mMemRun;
    ShmMemInfo mMemDeinit;

    std::vector<ShmMem> mMems;
};
} /* namespace icamera */
