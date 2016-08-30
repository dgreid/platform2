// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <login_manager/system_utils_impl.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <gtest/gtest.h>

namespace login_manager {

TEST(SystemUtilsTest, CorrectFileWrite) {
  base::ScopedTempDir tmpdir;
  base::FilePath scratch;
  ASSERT_TRUE(tmpdir.CreateUniqueTempDir());
  ASSERT_TRUE(base::CreateTemporaryFileInDir(tmpdir.path(), &scratch));

  std::string old_data("what");
  std::string new_data("ho, neighbor");

  ASSERT_EQ(old_data.length(),
            base::WriteFile(scratch, old_data.c_str(), old_data.length()));

  SystemUtilsImpl utils;
  ASSERT_TRUE(utils.AtomicFileWrite(scratch, new_data));
  std::string written_data;
  ASSERT_TRUE(base::ReadFileToString(scratch, &written_data));
  ASSERT_EQ(new_data, written_data);
}

TEST(SystemUtilsTest, CreateTemporaryDirIn) {
  base::ScopedTempDir tmpdir;
  ASSERT_TRUE(tmpdir.CreateUniqueTempDir());

  SystemUtilsImpl utils;
  base::FilePath scratch1, scratch2;
  ASSERT_TRUE(utils.CreateTemporaryDirIn(tmpdir.path(), &scratch1));
  ASSERT_TRUE(utils.CreateTemporaryDirIn(tmpdir.path(), &scratch2));

  EXPECT_TRUE(base::DirectoryExists(scratch1));
  EXPECT_TRUE(base::DirectoryExists(scratch2));
  EXPECT_TRUE(tmpdir.path().IsParent(scratch1));
  EXPECT_TRUE(tmpdir.path().IsParent(scratch2));
  EXPECT_NE(scratch1, scratch2);
}

TEST(SystemUtilsTest, RenameDir) {
  base::ScopedTempDir tmpdir1, tmpdir2;
  ASSERT_TRUE(tmpdir1.CreateUniqueTempDir());
  ASSERT_TRUE(tmpdir2.CreateUniqueTempDir());
  base::FilePath scratch;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(tmpdir1.path(), &scratch));
  ASSERT_FALSE(base::IsDirectoryEmpty(tmpdir1.path()));
  ASSERT_TRUE(base::IsDirectoryEmpty(tmpdir2.path()));

  // Check that renaming to an existing empty directory is allowed.
  SystemUtilsImpl utils;
  EXPECT_TRUE(utils.RenameDir(tmpdir1.path(), tmpdir2.path()));

  EXPECT_FALSE(base::DirectoryExists(tmpdir1.path()));
  EXPECT_TRUE(base::DirectoryExists(tmpdir2.path()));
  EXPECT_FALSE(base::IsDirectoryEmpty(tmpdir2.path()));

  tmpdir1.Take();  // |tmpdir1| no longer exists.
}

TEST(SystemUtilsTest, IsDirectoryEmpty) {
  SystemUtilsImpl utils;
  base::ScopedTempDir tmpdir;
  ASSERT_TRUE(tmpdir.CreateUniqueTempDir());
  EXPECT_TRUE(utils.IsDirectoryEmpty(tmpdir.path()));

  base::FilePath scratch;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(tmpdir.path(), &scratch));
  EXPECT_FALSE(utils.IsDirectoryEmpty(tmpdir.path()));

  EXPECT_TRUE(utils.IsDirectoryEmpty(tmpdir.path().Append("non-existent")));
}

}  // namespace login_manager
