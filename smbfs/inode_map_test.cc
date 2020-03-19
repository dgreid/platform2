// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbfs/inode_map.h"

#include <base/files/file_path.h>
#include <gtest/gtest.h>

namespace smbfs {
namespace {

constexpr ino_t kRootInode = 7;
constexpr char kFilePath1[] = "/foo";
constexpr char kFilePath2[] = "/foo/bar";

TEST(InodeMapTest, TestRootInode) {
  InodeMap map(kRootInode);

  EXPECT_EQ(base::FilePath("/"), map.GetPath(kRootInode));
  EXPECT_EQ(kRootInode, map.IncInodeRef(base::FilePath("/")));
}

TEST(InodeMapTest, TestInsertLookup) {
  InodeMap map(kRootInode);

  ino_t inode1 = map.IncInodeRef(base::FilePath(kFilePath1));
  EXPECT_NE(inode1, kRootInode);
  EXPECT_EQ(inode1, map.IncInodeRef(base::FilePath(kFilePath1)));
  EXPECT_EQ(base::FilePath(kFilePath1), map.GetPath(inode1));

  ino_t inode2 = map.IncInodeRef(base::FilePath(kFilePath2));
  EXPECT_NE(inode2, kRootInode);
  EXPECT_NE(inode2, inode1);
  EXPECT_EQ(inode2, map.IncInodeRef(base::FilePath(kFilePath2)));
  EXPECT_EQ(base::FilePath(kFilePath2), map.GetPath(inode2));
}

TEST(InodeMapTest, TestInsertLookupNonExistent) {
  InodeMap map(kRootInode);

  EXPECT_EQ(base::FilePath(), map.GetPath(kRootInode + 1));
}

TEST(InodeMapTest, TestInsertEmpty) {
  InodeMap map(kRootInode);
  EXPECT_DEATH(map.IncInodeRef(base::FilePath()), ".*path\\.empty.*");
}

TEST(InodeMapTest, TestInsertNonAbsolute) {
  InodeMap map(kRootInode);
  EXPECT_DEATH(map.IncInodeRef(base::FilePath("foo")), ".*path\\.IsAbsolute.*");
}

TEST(InodeMapTest, TestInsertRelative) {
  InodeMap map(kRootInode);
  EXPECT_DEATH(map.IncInodeRef(base::FilePath("/foo/../bar")),
               ".*path\\.ReferencesParent.*");
}

TEST(InodeMapTest, TestForget) {
  InodeMap map(kRootInode);

  // Create inode with refcount of 3.
  ino_t inode1 = map.IncInodeRef(base::FilePath(kFilePath1));
  map.IncInodeRef(base::FilePath(kFilePath1));
  map.IncInodeRef(base::FilePath(kFilePath1));
  EXPECT_EQ(base::FilePath(kFilePath1), map.GetPath(inode1));

  // Create inode with refcount of 2.
  ino_t inode2 = map.IncInodeRef(base::FilePath(kFilePath2));
  map.IncInodeRef(base::FilePath(kFilePath2));
  EXPECT_EQ(base::FilePath(kFilePath2), map.GetPath(inode2));

  bool removed = map.Forget(inode1, 2);
  EXPECT_EQ(base::FilePath(kFilePath1), map.GetPath(inode1));
  EXPECT_FALSE(removed);

  removed = map.Forget(inode1, 1);
  EXPECT_EQ(base::FilePath(), map.GetPath(inode1));
  EXPECT_TRUE(removed);

  // Previous Forget() calls shouldn't affect |inode2|.
  EXPECT_EQ(base::FilePath(kFilePath2), map.GetPath(inode2));

  removed = map.Forget(inode2, 2);
  EXPECT_EQ(base::FilePath(), map.GetPath(inode2));
  EXPECT_TRUE(removed);
}

TEST(InodeMapTest, TestForgetRoot) {
  InodeMap map(kRootInode);

  // Forgetting the root inode should do nothing.
  bool removed = map.Forget(kRootInode, 1);
  EXPECT_EQ(base::FilePath("/"), map.GetPath(kRootInode));
  EXPECT_FALSE(removed);
}

TEST(InodeMapTest, TestForgetTooMany) {
  InodeMap map(kRootInode);

  ino_t inode1 = map.IncInodeRef(base::FilePath(kFilePath1));
  EXPECT_DEATH(map.Forget(inode1, 2), "Check failed.*");
}

}  // namespace
}  // namespace smbfs
