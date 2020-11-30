// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_COMMON_PSTORE_H_
#define VM_TOOLS_COMMON_PSTORE_H_

#include <cstdint>

namespace vm_tools {

constexpr const char kArcVmPstorePath[] = "/run/arcvm/arcvm.pstore";
constexpr uint32_t kArcVmPstoreSize = 1024 * 1024;

}  // namespace vm_tools

#endif  // VM_TOOLS_COMMON_PSTORE_H_
