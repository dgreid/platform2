// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_EC_UTIL_H_
#define TYPECD_EC_UTIL_H_

namespace typecd {

// List of possible Type C Operating modes. These are selected by typecd based
// on connected peripheral(s) and device policy.
enum TypeCMode {
  TYPEC_MODE_NONE = -1,
  TYPEC_MODE_DP = 0,
  TYPEC_MODE_TBT,
  TYPEC_MODE_USB4,
};

// Interface used by Type C daemon to communicate with Chrome EC for
// controlling specific Type C behaviour. Depending on the running environment
// (e.g production Chromebook, unit tests) this interface can be implemented by
// a variety of back-ends (e.g D-BUS calls to an entity controlling the Chrome
// OS EC, ioctls directly to the Chrome OS EC, calls to Linux kernel sysfs,
// Mock implementation etc.).
class ECUtil {
 public:
  // Returns whether the system supports Type C Mode Entry from the Application
  // Processor.
  virtual bool ModeEntrySupported() = 0;

  // Instruct the system to enter mode |mode| on port with index |port|.
  virtual bool EnterMode(int port, TypeCMode mode) = 0;

  // Instruct the system to exit the current operating mode on port with index
  // |port|.
  virtual bool ExitMode(int port) = 0;

  virtual ~ECUtil() = default;
};

}  // namespace typecd

#endif  // TYPECD_EC_UTIL_H_
