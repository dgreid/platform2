// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_MEMORY_TOOL_H_
#define DEBUGD_SRC_MEMORY_TOOL_H_

#include <string>

#include <base/files/scoped_file.h>
#include <base/macros.h>

#include "debugd/src/subprocess_tool.h"

namespace debugd {

class MemtesterTool : public SubprocessTool {
 public:
  MemtesterTool() = default;
  ~MemtesterTool() override = default;

  std::string Start(const base::ScopedFD& outfd, const uint32_t& memory);

 private:
  DISALLOW_COPY_AND_ASSIGN(MemtesterTool);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_MEMORY_TOOL_H_
