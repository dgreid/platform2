// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "run_oci/run_oci_utils.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace run_oci {
namespace {

TEST(RunOciUtilsTest, TestOpenOciConfigSafely) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath config_file = temp_dir.GetPath().Append("config.json");

  // Call OpenOciConfigSafely when |config_file| does not exist (yet).
  // Verify it returns ENOENT.
  {
    brillo::SafeFD fd(OpenOciConfigSafely(config_file));
    EXPECT_FALSE(fd.is_valid());
  }

  // Create the file on a filesystem with exec. Verify OpenOciConfigSafely
  // works.
  const std::string content = "{}";
  ASSERT_EQ(content.size(),
            base::WriteFile(config_file, content.data(), content.size()));
  {
    brillo::SafeFD fd(OpenOciConfigSafely(config_file));
    EXPECT_TRUE(fd.is_valid());
    auto result = fd.ReadContents();
    ASSERT_FALSE(brillo::SafeFD::IsError(result.second));
    EXPECT_EQ(content, std::string(result.first.begin(), result.first.end()));
  }

  // TODO(yusukes): Verify OpenOciConfigSafely returns EPERM when the file is
  // not on an exec filesystem.
}

}  // namespace
}  // namespace run_oci
