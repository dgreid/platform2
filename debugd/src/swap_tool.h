// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_SWAP_TOOL_H_
#define DEBUGD_SRC_SWAP_TOOL_H_

#include <string>

#include <brillo/errors/error.h>
#include <base/macros.h>

namespace debugd {

class SwapTool {
 public:
  SwapTool() = default;
  SwapTool(const SwapTool&) = delete;
  SwapTool& operator=(const SwapTool&) = delete;

  ~SwapTool() = default;

  std::string SwapEnable(int32_t size, bool change_now) const;
  std::string SwapDisable(bool change_now) const;
  std::string SwapStartStop(bool on) const;
  std::string SwapStatus() const;
  std::string SwapSetParameter(const std::string& parameter_name,
                               int32_t parameter_value) const;
  // Kstaled swap configuration.
  bool KstaledSetRatio(brillo::ErrorPtr* error, uint8_t kstaled_ratio) const;
};

}  // namespace debugd

#endif  // DEBUGD_SRC_SWAP_TOOL_H_
