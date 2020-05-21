// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory/shared_pointer_memory.h"

#include <hidl/HidlSupport.h>

namespace android {
namespace hidl {
namespace memory {
namespace V1_0 {
namespace implementation {

using ::android::hardware::Void;

SharedPointerMemory::SharedPointerMemory(const hidl_memory& memory)
    : memory_(memory) {}

Return<void> SharedPointerMemory::update() {
  return Void();
}

Return<void> SharedPointerMemory::updateRange(uint64_t, uint64_t) {
  return Void();
}

Return<void> SharedPointerMemory::read() {
  return Void();
}
Return<void> SharedPointerMemory::readRange(uint64_t, uint64_t) {
  return Void();
}

Return<void> SharedPointerMemory::commit() {
  return Void();
}

Return<void*> SharedPointerMemory::getPointer() {
  return static_cast<void*>(nullptr);
}

Return<uint64_t> SharedPointerMemory::getSize() {
  return memory_.size();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace memory
}  // namespace hidl
}  // namespace android
