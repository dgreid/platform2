// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/generic_network.h"

namespace runtime_probe {

base::Optional<std::string> GenericNetworkFunction::GetNetworkType() const {
  return base::nullopt;
}

}  // namespace runtime_probe
