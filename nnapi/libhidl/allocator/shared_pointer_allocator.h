// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NNAPI_LIBHIDL_ALLOCATOR_SHARED_POINTER_ALLOCATOR_H_
#define NNAPI_LIBHIDL_ALLOCATOR_SHARED_POINTER_ALLOCATOR_H_

#include <android/hidl/allocator/1.0/IAllocator.h>

namespace android {
namespace hidl {
namespace allocator {
namespace V1_0 {
namespace implementation {

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hidl::allocator::V1_0::IAllocator;

class SharedPointerAllocator : public IAllocator {
  // Methods from ::android::hidl::allocator::V1_0::IAllocator follow
  Return<void> allocate(uint64_t size, allocate_cb _hidl_cb) override;
  Return<void> batchAllocate(uint64_t size,
                             uint64_t count,
                             batchAllocate_cb _hidl_cb) override;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace allocator
}  // namespace hidl
}  // namespace android


#endif  // NNAPI_LIBHIDL_ALLOCATOR_SHARED_POINTER_ALLOCATOR_H_
