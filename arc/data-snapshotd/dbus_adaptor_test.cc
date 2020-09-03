// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "arc/data-snapshotd/dbus_adaptor.h"

namespace arc {
namespace data_snapshotd {

namespace {

constexpr char kRandomDir[] = "data";
constexpr char kRandomFile[] = "hash";
constexpr char kContent[] = "content";

}  // namespace

class DBusAdaptorTest : public testing::Test {
 public:
  DBusAdaptorTest() {
    EXPECT_TRUE(root_tempdir_.CreateUniqueTempDir());
    dbus_adaptor_ = DBusAdaptor::CreateForTesting(root_tempdir_.GetPath());
  }
  DBusAdaptor* dbus_adaptor() { return dbus_adaptor_.get(); }
  const base::FilePath& last_snapshot_dir() const {
    return dbus_adaptor_->get_last_snapshot_directory();
  }
  const base::FilePath& previous_snapshot_dir() const {
    return dbus_adaptor_->get_previous_snapshot_directory();
  }
  // Creates |dir| and fills in with random content.
  void CreateDir(const base::FilePath& dir) {
    EXPECT_TRUE(base::CreateDirectory(dir));
    EXPECT_TRUE(base::CreateDirectory(dir.Append(kRandomDir)));
    EXPECT_TRUE(
        base::WriteFile(dir.Append(kRandomFile), kContent, strlen(kContent)));
  }

 private:
  std::unique_ptr<DBusAdaptor> dbus_adaptor_;
  base::ScopedTempDir root_tempdir_;
};

TEST_F(DBusAdaptorTest, ClearSnapshotBasic) {
  CreateDir(last_snapshot_dir());
  EXPECT_TRUE(base::DirectoryExists(last_snapshot_dir()));

  CreateDir(previous_snapshot_dir());
  EXPECT_TRUE(base::DirectoryExists(previous_snapshot_dir()));

  EXPECT_TRUE(dbus_adaptor()->ClearSnapshot(nullptr, false /* last */));
  EXPECT_FALSE(base::DirectoryExists(previous_snapshot_dir()));

  EXPECT_TRUE(dbus_adaptor()->ClearSnapshot(nullptr, false /* last */));

  EXPECT_TRUE(dbus_adaptor()->ClearSnapshot(nullptr, true /* last */));
  EXPECT_FALSE(base::DirectoryExists(last_snapshot_dir()));

  EXPECT_TRUE(dbus_adaptor()->ClearSnapshot(nullptr, true /* last */));
}

}  // namespace data_snapshotd
}  // namespace arc
