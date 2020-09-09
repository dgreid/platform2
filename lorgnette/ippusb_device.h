// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_IPPUSB_DEVICE_H_
#define LORGNETTE_IPPUSB_DEVICE_H_

#include <string>

#include <base/optional.h>

namespace lorgnette {

base::Optional<std::string> BackendForDevice(const std::string& device_name);

}  // namespace lorgnette

#endif  // LORGNETTE_IPPUSB_DEVICE_H_
