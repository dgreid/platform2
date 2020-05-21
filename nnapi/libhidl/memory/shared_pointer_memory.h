// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NNAPI_LIBHIDL_MEMORY_SHARED_POINTER_MEMORY_H_
#define NNAPI_LIBHIDL_MEMORY_SHARED_POINTER_MEMORY_H_

#include <android/hidl/memory/1.0/IMemory.h>

namespace android {
namespace hidl {
namespace memory {
namespace V1_0 {
namespace implementation {

using ::android::hardware::Return;
using ::android::hidl::memory::V1_0::IMemory;
using ::android::hardware::hidl_memory;

class SharedPointerMemory : public IMemory {
 public:
  explicit SharedPointerMemory(const hidl_memory& memory);

  // Methods from ::android::hidl::memory::V1_0::IMemory
  Return<void> update() override;
  Return<void> updateRange(uint64_t start, uint64_t length) override;
  Return<void> read() override;
  Return<void> readRange(uint64_t start, uint64_t length) override;
  Return<void> commit() override;
  Return<void*> getPointer() override;
  Return<uint64_t> getSize() override;

 private:
  SharedPointerMemory(const SharedPointerMemory&) = delete;
  SharedPointerMemory operator=(const SharedPointerMemory&) = delete;

  hidl_memory memory_;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace memory
}  // namespace hidl
}  // namespace android

#endif  // NNAPI_LIBHIDL_MEMORY_SHARED_POINTER_MEMORY_H_
