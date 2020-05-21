// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory/shared_pointer_mapper.h"

#include "memory/shared_pointer_memory.h"
#include <string>

namespace android {
namespace hidl {
namespace memory {
namespace V1_0 {

// static
sp<IMapper> IMapper::getService(const std::string&, bool) {
  return new implementation::SharedPointerMapper;
}

namespace implementation {

Return<sp<IMemory>> SharedPointerMapper::mapMemory(const hidl_memory& mem) {
  return new SharedPointerMemory(mem);
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace memory
}  // namespace hidl
}  // namespace android
