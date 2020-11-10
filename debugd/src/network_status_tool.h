// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_NETWORK_STATUS_TOOL_H_
#define DEBUGD_SRC_NETWORK_STATUS_TOOL_H_

#include <string>

#include <base/macros.h>

namespace debugd {

class NetworkStatusTool {
 public:
  NetworkStatusTool() = default;
  NetworkStatusTool(const NetworkStatusTool&) = delete;
  NetworkStatusTool& operator=(const NetworkStatusTool&) = delete;

  ~NetworkStatusTool() = default;

  std::string GetNetworkStatus();
};

}  // namespace debugd

#endif  // DEBUGD_SRC_NETWORK_STATUS_TOOL_H_
