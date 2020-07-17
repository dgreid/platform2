// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dhcp/mock_dhcp_properties.h"

#include <gtest/gtest.h>

namespace shill {

MockDhcpProperties::MockDhcpProperties()
    : DhcpProperties(/*delegate=*/nullptr) {}

MockDhcpProperties::~MockDhcpProperties() = default;

}  // namespace shill
