// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_EC_TYPEC_TOOL_H_
#define DEBUGD_SRC_EC_TYPEC_TOOL_H_

#include <string>

#include <base/macros.h>
#include <brillo/errors/error.h>

namespace debugd {

class EcTypeCTool {
 public:
  EcTypeCTool() = default;
  EcTypeCTool(const EcTypeCTool&) = delete;
  EcTypeCTool& operator=(const EcTypeCTool&) = delete;

  ~EcTypeCTool() = default;

  std::string GetInventory();
  bool EnterMode(brillo::ErrorPtr* error,
                 uint32_t port_num,
                 uint32_t mode,
                 std::string* output);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_EC_TYPEC_TOOL_H_
