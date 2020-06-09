// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/smart_discharge_configurator.h"

#include <base/files/file_util.h>

#include "power_manager/powerd/system/cros_ec_ioctl.h"

namespace power_manager {
namespace system {

void ConfigureSmartDischarge(int64_t to_zero_hr,
                             int64_t cutoff_ua,
                             int64_t hibernate_ua) {
  if (to_zero_hr < 0 || cutoff_ua < 0 || hibernate_ua < 0)
    return;

  base::ScopedFD cros_ec_fd =
      base::ScopedFD(open(cros_ec_ioctl::kCrosEcDevNodePath, O_RDWR));
  if (!cros_ec_fd.is_valid()) {
    PLOG(ERROR) << "Failed to open " << cros_ec_ioctl::kCrosEcDevNodePath;
    return;
  }

  struct ec_params_smart_discharge params = {};
  params.flags = EC_SMART_DISCHARGE_FLAGS_SET;
  params.hours_to_zero = to_zero_hr;
  params.drate.cutoff = cutoff_ua;
  params.drate.hibern = hibernate_ua;

  cros_ec_ioctl::IoctlCommand<struct ec_params_smart_discharge,
                              struct ec_response_smart_discharge>
      cmd(EC_CMD_SMART_DISCHARGE);
  cmd.SetReq(params);

  if (!cmd.Run(cros_ec_fd.get())) {
    LOG(ERROR) << "Failed to set Smart Discharge to " << params.hours_to_zero
               << " hrs to zero, cutoff power " << params.drate.cutoff
               << " uA, hibernate power " << params.drate.hibern << " uA";
    return;
  }
  struct ec_response_smart_discharge* response = cmd.Resp();
  LOG(INFO) << "Smart Discharge set to " << response->hours_to_zero
            << " hrs to zero, cutoff power " << response->drate.cutoff
            << " uA, hibernate power " << response->drate.hibern
            << " uA, cutoff threshold " << response->dzone.cutoff
            << " mAh, stay-up threshold " << response->dzone.stayup << " mAh";
}

}  // namespace system
}  // namespace power_manager
