// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NNAPI_LIBHIDL_MEMORY_SHARED_POINTER_MAPPER_H_
#define NNAPI_LIBHIDL_MEMORY_SHARED_POINTER_MAPPER_H_

#include <android/hidl/memory/1.0/IMapper.h>
#include <android/hidl/memory/1.0/IMemory.h>
#include <utils/StrongPointer.h>

namespace android {
namespace hidl {
namespace memory {
namespace V1_0 {
namespace implementation {

using ::android::hidl::memory::V1_0::IMapper;
using ::android::hardware::Return;
using ::android::sp;
using ::android::hidl::memory::V1_0::IMemory;
using ::android::hardware::hidl_memory;

class SharedPointerMapper : public IMapper {
 public:
  Return<sp<IMemory>> mapMemory(const hidl_memory& mem) override;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace memory
}  // namespace hidl
}  // namespace android

#endif  // NNAPI_LIBHIDL_MEMORY_SHARED_POINTER_MAPPER_H_
