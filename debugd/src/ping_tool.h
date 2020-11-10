// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_PING_TOOL_H_
#define DEBUGD_SRC_PING_TOOL_H_

#include <string>

#include <base/macros.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>

#include "debugd/src/subprocess_tool.h"

namespace debugd {

class PingTool : public SubprocessTool {
 public:
  PingTool() = default;
  PingTool(const PingTool&) = delete;
  PingTool& operator=(const PingTool&) = delete;

  ~PingTool() override = default;

  bool Start(const base::ScopedFD& outfd,
             const std::string& destination,
             const brillo::VariantDictionary& options,
             std::string* out_id,
             brillo::ErrorPtr* error);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_PING_TOOL_H_
