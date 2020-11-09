// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_SETUP_ANDROID_SDK_VERSION_H_
#define ARC_SETUP_ANDROID_SDK_VERSION_H_

#include <climits>

namespace arc {

enum class AndroidSdkVersion {
  UNKNOWN = 0,
  ANDROID_M = 23,
  ANDROID_N_MR1 = 25,
  ANDROID_P = 28,
  ANDROID_R = 30,
};

}  // namespace arc

#endif  // ARC_SETUP_ANDROID_SDK_VERSION_H_
