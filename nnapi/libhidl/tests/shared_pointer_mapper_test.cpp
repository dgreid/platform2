// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <android/hidl/memory/1.0/IMapper.h>
#include <android/hidl/memory/1.0/IMemory.h>
#include <gtest/gtest.h>

using ::android::hardware::hidl_memory;
using ::android::hidl::memory::V1_0::IMapper;
using ::android::hidl::memory::V1_0::IMemory;
using ::android::sp;

TEST(SharedPointerMapperTest, EmptyMemory) {
  auto mapper = IMapper::getService("ashmem", false);
  hidl_memory memory;
  sp<IMemory> result = mapper->mapMemory(memory);
  ASSERT_EQ(nullptr, result->getPointer());
  ASSERT_EQ(0, result->getSize());
}
