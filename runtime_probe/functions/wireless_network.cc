// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/wireless_network.h"

#include <chromeos/dbus/shill/dbus-constants.h>

namespace runtime_probe {

base::Optional<std::string> WirelessNetworkFunction::GetNetworkType() const {
  return shill::kTypeWifi;
}

}  // namespace runtime_probe
