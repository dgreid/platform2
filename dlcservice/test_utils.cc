// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/test_utils.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <imageloader/dbus-proxy-mocks.h>
#include <update_engine/dbus-constants.h>
#include <update_engine/dbus-proxy-mocks.h>

#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/boot/mock_boot_device.h"
#include "dlcservice/dlc.h"
#include "dlcservice/system_state.h"
#include "dlcservice/utils.h"

using std::string;
using testing::_;
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
}

void BaseTest::SetUp() {
  SetUpFilesAndDirectories();

  auto mock_boot_device = std::make_unique<MockBootDevice>();
  EXPECT_CALL(*mock_boot_device, GetBootDevice()).WillOnce(Return("/dev/sdb5"));
  EXPECT_CALL(*mock_boot_device, IsRemovableDevice(_)).WillOnce(Return(false));

  SystemState::Initialize(
      std::move(mock_image_loader_proxy_), std::move(mock_update_engine_proxy_),
      std::make_unique<BootSlot>(move(mock_boot_device)), manifest_path_,
      preloaded_content_path_, content_path_, prefs_path_, /*for_test=*/true);
}

void BaseTest::SetUpFilesAndDirectories() {
  // Initialize DLC path.
  CHECK(scoped_temp_dir_.CreateUniqueTempDir());
  manifest_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "rootfs");
  preloaded_content_path_ =
      JoinPaths(scoped_temp_dir_.GetPath(), "preloaded_stateful");
  content_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "stateful");
  prefs_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "var_lib_dlcservice");
  mount_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "mount");
  base::FilePath mount_root_path = JoinPaths(mount_path_, "root");
  base::CreateDirectory(manifest_path_);
  base::CreateDirectory(preloaded_content_path_);
  base::CreateDirectory(content_path_);
  base::CreateDirectory(prefs_path_);
  base::CreateDirectory(mount_root_path);
  testdata_path_ = JoinPaths(getenv("SRC"), "testdata");

  // Create DLC manifest sub-directories.
  for (auto&& id : {kFirstDlc, kSecondDlc, kThirdDlc}) {
    base::CreateDirectory(JoinPaths(manifest_path_, id, kPackage));
    base::CopyFile(JoinPaths(testdata_path_, id, kPackage, kManifestName),
                   JoinPaths(manifest_path_, id, kPackage, kManifestName));
  }
}

int64_t BaseTest::GetFileSize(const base::FilePath& path) {
  int64_t file_size;
  EXPECT_TRUE(base::GetFileSize(path, &file_size));
  return file_size;
}

void BaseTest::ResizeImageFile(const base::FilePath& image_path,
                               int64_t image_size) {
  constexpr uint32_t file_flags =
      base::File::FLAG_WRITE | base::File::FLAG_OPEN;
  base::File file(image_path, file_flags);
  EXPECT_TRUE(file.SetLength(image_size));
}

void BaseTest::CreateImageFileWithRightSize(const base::FilePath& image_path,
                                            const base::FilePath& manifest_path,
                                            const DlcId& id,
                                            const string& package) {
  imageloader::Manifest manifest;
  dlcservice::GetDlcManifest(manifest_path, id, package, &manifest);
  int64_t image_size = manifest.preallocated_size();

  constexpr uint32_t file_flags =
      base::File::FLAG_WRITE | base::File::FLAG_READ | base::File::FLAG_CREATE;
  base::File file(image_path, file_flags);
  EXPECT_TRUE(file.SetLength(image_size));
}

// Will create |path|/|id|/|package|/dlc.img file.
void BaseTest::SetUpDlcWithoutSlots(const DlcId& id) {
  base::FilePath image_path =
      JoinPaths(preloaded_content_path_, id, kPackage, kDlcImageFileName);
  base::CreateDirectory(image_path.DirName());
  CreateImageFileWithRightSize(image_path, manifest_path_, id, kPackage);
}

// Will create |path/|id|/|package|/dlc_[a|b]/dlc.img files.
void BaseTest::SetUpDlcWithSlots(const DlcId& id) {
  // Create DLC content sub-directories and empty images.
  for (const auto& slot : {BootSlot::Slot::A, BootSlot::Slot::B}) {
    base::FilePath image_path =
        GetDlcImagePath(content_path_, id, kPackage, slot);
    base::CreateDirectory(image_path.DirName());
    CreateImageFileWithRightSize(image_path, manifest_path_, id, kPackage);
  }
}

void BaseTest::SetMountPath(const string& mount_path_expected) {
  ON_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillByDefault(
          DoAll(SetArgPointee<3>(mount_path_expected), Return(true)));
}

}  // namespace dlcservice
