// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_SMART_DISCHARGE_CONFIGURATOR_H_
#define POWER_MANAGER_POWERD_SYSTEM_SMART_DISCHARGE_CONFIGURATOR_H_

#include <stdint.h>

namespace power_manager {
namespace system {

// Configures Smart Discharge in EC.
// to_zero_hr cutoff_ua hibernate_ua
//         <0        <0           <0 no operation
//          0       >=0          >=0 disables Smart Discharge
//         >0         0            0 sets hours_to_zero while cutoff and
//                                   hibernate power remain unchanged
//         >0        >0           >0 sets all 3 values for Smart Discharge
void ConfigureSmartDischarge(int64_t to_zero_hr,
                             int64_t cutoff_ua,
                             int64_t hibernate_ua);

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_SMART_DISCHARGE_CONFIGURATOR_H_
