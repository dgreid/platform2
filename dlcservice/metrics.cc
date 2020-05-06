// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>

#include "dlcservice/metrics.h"

using std::map;
using std::string;

namespace dlcservice {

void Metrics::Init() {
  metrics_library_->Init();
}

}  // namespace dlcservice
