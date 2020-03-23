// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbfs/smb_filesystem.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace smbfs {
namespace {

constexpr char kSharePath[] = "smb://server/share";

class TestSmbFilesystem : public SmbFilesystem {
 public:
  TestSmbFilesystem() : SmbFilesystem(kSharePath) {}
};

}  // namespace

class SmbFilesystemTest : public testing::Test {};

TEST_F(SmbFilesystemTest, SetResolvedAddress) {
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

TEST_F(SmbFilesystemTest, MakeStatModeBits) {
  TestSmbFilesystem fs;

  // Check: "Other" permission bits are cleared.
  mode_t in_mode = S_IRWXO;
  mode_t out_mode = fs.MakeStatModeBits(in_mode);
  EXPECT_EQ(0, out_mode);

  // Check: Directories have user execute bit set.
  in_mode = S_IFDIR;
  out_mode = fs.MakeStatModeBits(in_mode);
  EXPECT_TRUE(out_mode & S_IXUSR);

  // Check: Files do not have user execute bit set.
  in_mode = S_IFREG;
  out_mode = fs.MakeStatModeBits(in_mode);
  EXPECT_FALSE(out_mode & S_IXUSR);

  // Check: Group bits equal user bits.
  in_mode = S_IRUSR | S_IWUSR;
  out_mode = fs.MakeStatModeBits(in_mode);
  EXPECT_EQ(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, out_mode);
}

}  // namespace smbfs
