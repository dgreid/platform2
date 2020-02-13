// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbfs/smb_filesystem.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace smbfs {
namespace {

constexpr char kSharePath[] = "smb://server/share";

class TestSmbFilesystem : public SmbFilesystem {
 public:
  TestSmbFilesystem() : SmbFilesystem(kSharePath) {}
};

TEST(SmbFilesystem, SetResolvedAddress) {
  TestSmbFilesystem fs;

  // Initial value is share path.
  EXPECT_EQ(kSharePath, fs.resolved_share_path());

  fs.SetResolvedAddress({1, 2, 3, 4});
  EXPECT_EQ("smb://1.2.3.4/share", fs.resolved_share_path());
  fs.SetResolvedAddress({127, 0, 0, 1});
  EXPECT_EQ("smb://127.0.0.1/share", fs.resolved_share_path());

  // Invalid address does nothing.
  fs.SetResolvedAddress({1, 2, 3});
  EXPECT_EQ("smb://127.0.0.1/share", fs.resolved_share_path());

  // Empty address resets to original share path.
  fs.SetResolvedAddress({});
  EXPECT_EQ(kSharePath, fs.resolved_share_path());
}

}  // namespace
}  // namespace smbfs
