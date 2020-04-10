// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/macros.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"

namespace diagnostics {

class CrosHealthdMojoAdapterTest : public testing::Test {
 protected:
  CrosHealthdMojoAdapterTest() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(CrosHealthdMojoAdapterTest);
};

}  // namespace diagnostics
