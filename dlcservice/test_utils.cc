// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/test_utils.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <imageloader/dbus-proxy-mocks.h>
#include <metrics/metrics_library_mock.h>
#include <update_engine/dbus-constants.h>
#include <update_engine/dbus-proxy-mocks.h>

#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/dlc.h"
#include "dlcservice/metrics.h"
#include "dlcservice/system_state.h"
#include "dlcservice/utils.h"

using std::string;
using std::vector;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace dlcservice {

const char kFirstDlc[] = "first-dlc";
const char kSecondDlc[] = "second-dlc";
const char kThirdDlc[] = "third-dlc";
const char kPackage[] = "package";
const char kDefaultOmahaUrl[] = "http://foo-url";

BaseTest::BaseTest() {
  // Create mocks with default behaviors.
  mock_image_loader_proxy_ =
      std::make_unique<StrictMock<ImageLoaderProxyMock>>();
  mock_image_loader_proxy_ptr_ = mock_image_loader_proxy_.get();

  mock_update_engine_proxy_ =
      std::make_unique<StrictMock<UpdateEngineProxyMock>>();
  mock_update_engine_proxy_ptr_ = mock_update_engine_proxy_.get();

  mock_session_manager_proxy_ =
      std::make_unique<StrictMock<SessionManagerProxyMock>>();
  mock_session_manager_proxy_ptr_ = mock_session_manager_proxy_.get();

  mock_boot_device_ = std::make_unique<MockBootDevice>();
  mock_boot_device_ptr_ = mock_boot_device_.get();
  EXPECT_CALL(*mock_boot_device_, GetBootDevice())
      .WillOnce(Return("/dev/sdb5"));
  ON_CALL(*mock_boot_device_, IsRemovableDevice(_))
      .WillByDefault(Return(false));
  EXPECT_CALL(*mock_boot_device_, IsRemovableDevice(_)).Times(1);
}

void BaseTest::SetUp() {
  loop_.SetAsCurrent();

  SetUpFilesAndDirectories();

  auto mock_metrics = std::make_unique<testing::StrictMock<MockMetrics>>();
  mock_metrics_ = mock_metrics.get();

  auto mock_system_properties =
      std::make_unique<testing::StrictMock<MockSystemProperties>>();
  mock_system_properties_ = mock_system_properties.get();

  SystemState::Initialize(
      std::move(mock_image_loader_proxy_), std::move(mock_update_engine_proxy_),
      std::move(mock_session_manager_proxy_), &mock_state_change_reporter_,
      std::make_unique<BootSlot>(std::move(mock_boot_device_)),
      std::move(mock_metrics), std::move(mock_system_properties),
      manifest_path_, preloaded_content_path_, content_path_, prefs_path_,
      users_path_, &clock_,
      /*for_test=*/true);
}

void BaseTest::SetUpFilesAndDirectories() {
  // Initialize DLC path.
  CHECK(scoped_temp_dir_.CreateUniqueTempDir());
  manifest_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "rootfs");
  preloaded_content_path_ =
      JoinPaths(scoped_temp_dir_.GetPath(), "preloaded_stateful");
  content_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "stateful");
  prefs_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "var_lib_dlcservice");
  users_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "users");
  mount_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "mount");
  base::FilePath mount_root_path = JoinPaths(mount_path_, "root");
  base::CreateDirectory(manifest_path_);
  base::CreateDirectory(preloaded_content_path_);
  base::CreateDirectory(content_path_);
  base::CreateDirectory(prefs_path_);
  base::CreateDirectory(users_path_);
  base::CreateDirectory(mount_root_path);
  testdata_path_ = JoinPaths(getenv("SRC"), "testdata");

  // Create DLC manifest sub-directories.
  for (auto&& id : {kFirstDlc, kSecondDlc, kThirdDlc}) {
    base::CreateDirectory(JoinPaths(manifest_path_, id, kPackage));
    base::CopyFile(JoinPaths(testdata_path_, id, kPackage, kManifestName),
                   JoinPaths(manifest_path_, id, kPackage, kManifestName));
  }
}

int64_t GetFileSize(const base::FilePath& path) {
  int64_t file_size;
  EXPECT_TRUE(base::GetFileSize(path, &file_size));
  return file_size;
}

base::FilePath BaseTest::SetUpDlcPreloadedImage(const DlcId& id) {
  imageloader::Manifest manifest;
  dlcservice::GetDlcManifest(manifest_path_, id, kPackage, &manifest);

  base::FilePath image_path =
      JoinPaths(preloaded_content_path_, id, kPackage, kDlcImageFileName);
  CreateFile(image_path, manifest.size());
  EXPECT_TRUE(base::PathExists(image_path));

  string data(manifest.size(), '1');
  WriteToImage(image_path, data);

  return image_path;
}

// Will create |path/|id|/|package|/dlc_[a|b]/dlc.img files.
void BaseTest::SetUpDlcWithSlots(const DlcId& id) {
  imageloader::Manifest manifest;
  dlcservice::GetDlcManifest(manifest_path_, id, kPackage, &manifest);

  // Create DLC content sub-directories and empty images.
  for (const auto& slot : {BootSlot::Slot::A, BootSlot::Slot::B}) {
    base::FilePath image_path =
        GetDlcImagePath(content_path_, id, kPackage, slot);
    CreateFile(image_path, manifest.preallocated_size());
  }
}

void BaseTest::InstallWithUpdateEngine(const vector<string>& ids) {
  for (const auto& id : ids) {
    imageloader::Manifest manifest;
    dlcservice::GetDlcManifest(manifest_path_, id, kPackage, &manifest);
    base::FilePath image_path = GetDlcImagePath(
        content_path_, id, kPackage, SystemState::Get()->active_boot_slot());

    string data(manifest.size(), '1');
    WriteToImage(image_path, data);
  }
}

void BaseTest::SetMountPath(const string& mount_path_expected) {
  ON_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillByDefault(
          DoAll(SetArgPointee<3>(mount_path_expected), Return(true)));
}

}  // namespace dlcservice
