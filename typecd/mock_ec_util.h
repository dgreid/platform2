// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_MOCK_EC_UTIL_H_
#define TYPECD_MOCK_EC_UTIL_H_

#include <gmock/gmock.h>

#include "typecd/ec_util.h"

namespace typecd {

class MockECUtil : public ECUtil {
 public:
  MOCK_METHOD(bool, ModeEntrySupported, (), (override));
  MOCK_METHOD(bool, EnterMode, (int, TypeCMode), (override));
  MOCK_METHOD(bool, ExitMode, (int), (override));
};

}  // namespace typecd

#endif  // TYPECD_MOCK_EC_UTIL_H_
