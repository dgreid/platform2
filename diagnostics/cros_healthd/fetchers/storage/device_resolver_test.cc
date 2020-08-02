// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "diagnostics/cros_healthd/fetchers/storage/device_resolver.h"

#include <base/files/file_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr char kFakeRoot[] = "cros_healthd/fetchers/storage/testdata/";

}  //  namespace

TEST(StorageDeviceResolverTest, GoodData) {
  auto resolver_or =
      StorageDeviceResolver::Create(base::FilePath(kFakeRoot), "mmcblk0");
  ASSERT_TRUE(resolver_or.ok());
  auto resolver = std::move(resolver_or).value();

  EXPECT_EQ(mojo_ipc::StorageDevicePurpose::kUnknown,
            resolver->GetDevicePurpose("nvme0n1"));
  EXPECT_EQ(mojo_ipc::StorageDevicePurpose::kBootDevice,
            resolver->GetDevicePurpose("mmcblk0"));
  EXPECT_EQ(mojo_ipc::StorageDevicePurpose::kSwapDevice,
            resolver->GetDevicePurpose("nvme0n2"));
}

TEST(StorageDeviceResolverTest, MissingFile) {
  auto resolver_or = StorageDeviceResolver::Create(
      base::FilePath("NONSENSE PATH"), "NONSENSE ROOT");
  ASSERT_FALSE(resolver_or.ok());
}

}  // namespace diagnostics
