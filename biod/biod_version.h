// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_BIOD_VERSION_H_
#define BIOD_BIOD_VERSION_H_

#include <base/logging.h>

#ifndef VCSID
#define VCSID "<not set>"
#endif

namespace biod {

static inline void LogVersion() {
  LOG(INFO) << "vcsid " << VCSID;
}

}  // namespace biod

#endif  // BIOD_BIOD_VERSION_H_
