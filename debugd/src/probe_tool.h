// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_PROBE_TOOL_H_
#define DEBUGD_SRC_PROBE_TOOL_H_

#include <string>

#include <base/macros.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>

namespace debugd {

class ProbeTool {
 public:
  ProbeTool() = default;
  ProbeTool(const ProbeTool&) = delete;
  ProbeTool& operator=(const ProbeTool&) = delete;

  ~ProbeTool() = default;

  // Executes the function defined for runtime_probe.
  bool EvaluateProbeFunction(brillo::ErrorPtr* error,
                             const std::string& probe_statement,
                             brillo::dbus_utils::FileDescriptor* outfd);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_PROBE_TOOL_H_
