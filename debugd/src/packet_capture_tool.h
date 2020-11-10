// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_PACKET_CAPTURE_TOOL_H_
#define DEBUGD_SRC_PACKET_CAPTURE_TOOL_H_

#include <string>

#include <base/macros.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>

#include "debugd/src/subprocess_tool.h"

namespace debugd {

class PacketCaptureTool : public SubprocessTool {
 public:
  PacketCaptureTool() = default;
  PacketCaptureTool(const PacketCaptureTool&) = delete;
  PacketCaptureTool& operator=(const PacketCaptureTool&) = delete;

  ~PacketCaptureTool() override = default;

  bool Start(const base::ScopedFD& status_fd,
             const base::ScopedFD& output_fd,
             const brillo::VariantDictionary& options,
             std::string* out_id,
             brillo::ErrorPtr* error);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_PACKET_CAPTURE_TOOL_H_
