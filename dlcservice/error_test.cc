// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/errors/error_codes.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dlcservice/error.h"

namespace dlcservice {

TEST(Error, Create) {
  brillo::ErrorPtr err = Error::Create(FROM_HERE, "error-code", "message");
  EXPECT_STREQ(brillo::errors::dbus::kDomain, err->GetDomain().c_str());
  EXPECT_STREQ("error-code", err->GetCode().c_str());
  EXPECT_STREQ("message", err->GetMessage().c_str());
}

TEST(Error, ToString) {
  brillo::ErrorPtr err = Error::Create(FROM_HERE, "error-code", "message");
  EXPECT_STREQ("Error Code=error-code, Error Message=message",
               Error::ToString(err).c_str());
}

}  // namespace dlcservice
