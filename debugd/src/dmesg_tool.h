// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This tool is used for getting dmesg information through debugd.

#ifndef DEBUGD_SRC_DMESG_TOOL_H_
#define DEBUGD_SRC_DMESG_TOOL_H_

#include <string>

#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>

namespace debugd {

class DmesgTool {
 public:
  DmesgTool() = default;
  DmesgTool(const DmesgTool&) = delete;
  DmesgTool& operator=(const DmesgTool&) = delete;

  ~DmesgTool() = default;

  bool CallDmesg(const brillo::VariantDictionary& options,
                 brillo::ErrorPtr* error,
                 std::string* output);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_DMESG_TOOL_H_
