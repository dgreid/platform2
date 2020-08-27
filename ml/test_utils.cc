// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/test_utils.h"

namespace ml {

std::string GetTestModelDir() {
  const char* const temp_dir = getenv("T");
  CHECK_NE(temp_dir, nullptr);
  return std::string(temp_dir) + "/ml_models/";
}

}  // namespace ml
