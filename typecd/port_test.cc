// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "typecd/test_constants.h"

namespace typecd {

class PortTest : public ::testing::Test {};

// Check that basic Port creation, partner addition/deletion works.
TEST_F(PortTest, TestBasicAdd) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);
  EXPECT_NE(nullptr, port);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  EXPECT_NE(nullptr, port->partner_);
  port->RemovePartner();
  EXPECT_EQ(nullptr, port->partner_);
}

}  // namespace typecd
