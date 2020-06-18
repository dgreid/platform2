// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <utility>

#include <base/files/file_path.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/utils/storage/caching_device_adapter.h"
#include "diagnostics/cros_healthd/utils/storage/mock/mock_device_adapter.h"

using testing::Return;
using testing::StrictMock;

namespace diagnostics {

// Tests whether the caching layer works properly, i.e. pass-through the initial
// call but returns the rest from the remembered value.
TEST(CachingDeviceAdapterTest, CheckCaching) {
  constexpr char kDevName[] = "test";
  constexpr char kModel[] = "test_model";

  auto mock_adapter = std::make_unique<StrictMock<MockDeviceAdapter>>();
  EXPECT_CALL(*mock_adapter, GetDeviceName())
      .Times(1)
      .WillOnce(Return(kDevName));
  EXPECT_CALL(*mock_adapter, GetModel())
      .Times(1)
      .WillOnce(Return(StatusOr<std::string>(kModel)));

  CachingDeviceAdapter adapter(std::move(mock_adapter));

  EXPECT_EQ(kDevName, adapter.GetDeviceName());
  EXPECT_EQ(kModel, adapter.GetModel().value());

  // If caching doesn't work properly, second attempt will violate WillOnce.
  EXPECT_EQ(kDevName, adapter.GetDeviceName());
  EXPECT_EQ(kModel, adapter.GetModel().value());
}

}  // namespace diagnostics
