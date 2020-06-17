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

#include "AIQResult.h"
// TODO: port AiqResultStorage.h/cpp class from libcamhal here.

namespace cca {

class CCAStorage {
public:
    CCAStorage();
    virtual ~CCAStorage();

private:
    //TODO: frame indexed stats and result arrays as private member.
    //      Result is compond structure so define CCAResult, as for stats
    //      just use ia_xxx structures.
};
}//cca
