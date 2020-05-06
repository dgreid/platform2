// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_DISK_CLEANUP_ROUTINES_H_
#define CRYPTOHOME_MOCK_DISK_CLEANUP_ROUTINES_H_

#include "cryptohome/disk_cleanup_routines.h"

#include <string>

#include <gmock/gmock.h>

namespace cryptohome {

class MockDiskCleanupRoutines : public DiskCleanupRoutines {
 public:
  MockDiskCleanupRoutines();
  virtual ~MockDiskCleanupRoutines();

  MOCK_METHOD(bool, DeleteUserCache, (const std::string&), (override));
  MOCK_METHOD(bool, DeleteUserGCache, (const std::string&), (override));
  MOCK_METHOD(bool, DeleteUserAndroidCache, (const std::string&), (override));
  MOCK_METHOD(bool, DeleteUserProfile, (const std::string&), (override));
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_DISK_CLEANUP_ROUTINES_H_
