// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "allocator/shared_pointer_allocator.h"

#include <hidl/HidlSupport.h>
#include <memory>
#include <string>

namespace android {
namespace hidl {
namespace allocator {
namespace V1_0 {

// static
::android::sp<IAllocator> IAllocator::getService(const std::string& /*type*/,
                                                 bool /*getStub*/) {
  return new implementation::SharedPointerAllocator;
}

namespace implementation {

using ::android::hardware::hidl_memory;

// TODO(155837828): Add memory allocation implementation.
static hidl_memory allocate_aligned(uint64_t) {
  return hidl_memory();
}

Return<void> SharedPointerAllocator::allocate(uint64_t size,
                                              allocate_cb _hidl_cb) {
  hidl_memory memory = allocate_aligned(size);
  _hidl_cb(memory.handle() != nullptr, memory);
  return Void();
}

Return<void> SharedPointerAllocator::batchAllocate(uint64_t /*size*/,
                                                   uint64_t /*count*/,
                                                   batchAllocate_cb) {
  return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace allocator
}  // namespace hidl
}  // namespace android
