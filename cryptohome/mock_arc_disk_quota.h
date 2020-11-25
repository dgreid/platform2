// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_ARC_DISK_QUOTA_H_
#define CRYPTOHOME_MOCK_ARC_DISK_QUOTA_H_

#include "cryptohome/arc_disk_quota.h"

#include <string>

#include <gmock/gmock.h>

namespace cryptohome {

class MockArcDiskQuota : public ArcDiskQuota {
 public:
  MockArcDiskQuota()
      : ArcDiskQuota(nullptr, nullptr, base::FilePath("/home")) {}
  ~MockArcDiskQuota() override {}

  MOCK_METHOD(void, Initialize, (), (override));
  MOCK_METHOD(bool, IsQuotaSupported, (), (const, override));
  MOCK_METHOD(int64_t, GetCurrentSpaceForUid, (uid_t), (const, override));
  MOCK_METHOD(int64_t, GetCurrentSpaceForGid, (gid_t), (const, override));
  MOCK_METHOD(int64_t, GetCurrentSpaceForProjectId, (int), (const, override));
  MOCK_METHOD(bool,
              SetProjectId,
              (int,
               SetProjectIdAllowedPathType,
               const base::FilePath&,
               const std::string&),
              (const, override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_ARC_DISK_QUOTA_H_
