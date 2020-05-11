// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A simplified interface to the ML service. Used to implement the ml_cmdline
// tool.

#ifndef ML_SIMPLE_H_
#define ML_SIMPLE_H_

#include <string>

namespace ml {
namespace simple {

// Result of adding two numbers
struct AddResult {
  std::string status;
  double sum;
};

// Add two numbers. Returns result and a status message.
AddResult Add(double x, double y, bool use_nnapi);

}  // namespace simple
}  // namespace ml

#endif  // ML_SIMPLE_H_
