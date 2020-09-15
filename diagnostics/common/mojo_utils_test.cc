// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/mojo_utils.h"

#include <memory>
#include <utility>

#include <base/strings/string_piece.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/system/handle.h>

namespace diagnostics {

namespace {

class MojoUtilsTest : public testing::Test {
 protected:
  MojoUtilsTest() { mojo::core::Init(); }
};

}  // namespace

TEST_F(MojoUtilsTest, CreateMojoHandleAndRetrieveContent) {
  const base::StringPiece content("{\"key\": \"value\"}");

  mojo::ScopedHandle handle =
      CreateReadOnlySharedMemoryRegionMojoHandle(content);
  EXPECT_TRUE(handle.is_valid());

  auto shm_mapping =
      GetReadOnlySharedMemoryMappingFromMojoHandle(std::move(handle));
  ASSERT_TRUE(shm_mapping.IsValid());

  base::StringPiece actual(shm_mapping.GetMemoryAs<char>(),
                           shm_mapping.mapped_size());
  EXPECT_EQ(content, actual);
}

TEST_F(MojoUtilsTest, GetReadOnlySharedMemoryRegionFromMojoInvalidHandle) {
  mojo::ScopedHandle handle;
  EXPECT_FALSE(handle.is_valid());

  auto shm_mapping =
      GetReadOnlySharedMemoryMappingFromMojoHandle(std::move(handle));
  EXPECT_FALSE(shm_mapping.IsValid());
}

TEST_F(MojoUtilsTest, CreateReadOnlySharedMemoryFromEmptyContent) {
  mojo::ScopedHandle handle = CreateReadOnlySharedMemoryRegionMojoHandle("");
  // Cannot create valid handle using empty content line.
  EXPECT_FALSE(handle.is_valid());
}

}  // namespace diagnostics
