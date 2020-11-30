// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/load_oobe_config_usb.h"

#include <sys/mount.h>

#include <memory>
#include <string>

#include <base/environment.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "oobe_config/mock_load_oobe_config_usb.h"
#include "oobe_config/mock_save_oobe_config_usb.h"
#include "oobe_config/save_oobe_config_usb.h"
#include "oobe_config/usb_utils.h"

using base::FilePath;
using base::ScopedTempDir;
using std::string;
using std::unique_ptr;
using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::Return;

namespace oobe_config {

namespace {
constexpr char kDummyConfig[] = "dummy config content";
constexpr char kDummyDomain[] = "test@";
}  // namespace

class LoadOobeConfigUsbTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // TODO(ahassani): Create an actual ext4 image as a usb device that can be
    // mounted and used instead of fake creating a directory here.
    EXPECT_TRUE(fake_device_stateful_.CreateUniqueTempDir());
    EXPECT_TRUE(fake_usb_stateful_.CreateUniqueTempDir());
    EXPECT_TRUE(fake_device_ids_dir_.CreateUniqueTempDir());
    EXPECT_TRUE(fake_store_dir_.CreateUniqueTempDir());
    EXPECT_TRUE(everything_else_.CreateUniqueTempDir());
    device_oobe_config_dir_ =
        fake_device_stateful_.GetPath().Append(kUnencryptedOobeConfigDir);
    usb_oobe_config_dir_ =
        fake_usb_stateful_.GetPath().Append(kUnencryptedOobeConfigDir);
    EXPECT_TRUE(base::CreateDirectory(device_oobe_config_dir_));
    EXPECT_TRUE(base::CreateDirectory(usb_oobe_config_dir_));

    // Create config file.
    config_file_ = usb_oobe_config_dir_.Append(kConfigFile);
    EXPECT_EQ(base::WriteFile(config_file_, kDummyConfig, strlen(kDummyConfig)),
              strlen(kDummyConfig));

    // Create enrollment domain file.
    enrollment_domain_file_ = fake_usb_stateful_.GetPath()
                                  .Append(kUnencryptedOobeConfigDir)
                                  .Append(kDomainFile);
    EXPECT_EQ(base::WriteFile(enrollment_domain_file_, kDummyDomain,
                              strlen(kDummyDomain)),
              strlen(kDummyDomain));

    // Creating device paths.
    FilePath dev_id1, dev_id2;
    EXPECT_TRUE(
        base::CreateTemporaryFileInDir(everything_else_.GetPath(), &dev_id1));
    EXPECT_TRUE(
        base::CreateTemporaryFileInDir(everything_else_.GetPath(), &dev_id2));
    FilePath dev_id1_sym = fake_device_ids_dir_.GetPath().Append("dev_id1_sym");
    FilePath dev_id2_sym = fake_device_ids_dir_.GetPath().Append("dev_id2_sym");
    EXPECT_TRUE(base::CreateSymbolicLink(dev_id1, dev_id1_sym));
    EXPECT_TRUE(base::CreateSymbolicLink(dev_id2, dev_id2_sym));

    string source_path;
    std::unique_ptr<base::Environment> env(base::Environment::Create());
    EXPECT_TRUE(env->GetVar("SRC", &source_path));
    auto public_key = FilePath(source_path).Append("test.pub.key");
    auto private_key = FilePath(source_path).Append("test.pri.key");

    // Run a save so we can verify we can load it.
    MockSaveOobeConfigUsb save_config(
        fake_device_stateful_.GetPath(), fake_usb_stateful_.GetPath(),
        fake_device_ids_dir_.GetPath(), dev_id2, private_key, public_key);
    EXPECT_TRUE(save_config.Save());

    load_config_ = std::make_unique<MockLoadOobeConfigUsb>(
        fake_device_stateful_.GetPath(), fake_device_ids_dir_.GetPath(),
        fake_store_dir_.GetPath());
    EXPECT_TRUE(load_config_);

    ON_CALL(*load_config_, VerifyPublicKey()).WillByDefault(Return(true));
    ON_CALL(*load_config_, MountUsbDevice(_, _))
        .WillByDefault(
            DoAll(Invoke([this](const FilePath& device_path,
                                const FilePath& mount_point) {
                    EXPECT_TRUE(base::CopyDirectory(
                        fake_usb_stateful_.GetPath().Append("unencrypted"),
                        mount_point, true));
                  }),
                  Return(true)));
    ON_CALL(*load_config_, UnmountUsbDevice(_)).WillByDefault(Return(true));
  }

  bool TestGetOobeConfigJson() { return load_config_->Load(); }

  ScopedTempDir fake_device_stateful_;
  ScopedTempDir fake_usb_stateful_;
  ScopedTempDir fake_device_ids_dir_;
  ScopedTempDir fake_store_dir_;
  ScopedTempDir everything_else_;

  FilePath device_oobe_config_dir_;
  FilePath usb_oobe_config_dir_;
  FilePath config_file_;
  FilePath enrollment_domain_file_;

  unique_ptr<MockLoadOobeConfigUsb> load_config_;
};

TEST_F(LoadOobeConfigUsbTest, Simple) {
  EXPECT_TRUE(load_config_->Load());
  EXPECT_EQ(load_config_->config_, kDummyConfig);
  EXPECT_EQ(load_config_->enrollment_domain_, kDummyDomain);
}

TEST_F(LoadOobeConfigUsbTest, FailNoConfig) {
  EXPECT_TRUE(base::DeleteFile(config_file_));
  EXPECT_FALSE(TestGetOobeConfigJson());
}

TEST_F(LoadOobeConfigUsbTest, FailNoConfigSignature) {
  EXPECT_TRUE(base::DeleteFile(
      device_oobe_config_dir_.Append(kConfigFile).AddExtension("sig")));
  EXPECT_FALSE(TestGetOobeConfigJson());
}

TEST_F(LoadOobeConfigUsbTest, FailNoEnrollment) {
  EXPECT_TRUE(base::DeleteFile(enrollment_domain_file_));
  EXPECT_FALSE(TestGetOobeConfigJson());
}

TEST_F(LoadOobeConfigUsbTest, FailNoEnrollmentDomainSignature) {
  EXPECT_TRUE(base::DeleteFile(
      device_oobe_config_dir_.Append(kDomainFile).AddExtension("sig")));
  EXPECT_FALSE(TestGetOobeConfigJson());
}

TEST_F(LoadOobeConfigUsbTest, FailNoUsbDevicePathSignature) {
  EXPECT_TRUE(
      base::DeleteFile(device_oobe_config_dir_.Append(kUsbDevicePathSigFile)));
  EXPECT_FALSE(TestGetOobeConfigJson());
}

TEST_F(LoadOobeConfigUsbTest, FailNoPublicKey) {
  EXPECT_TRUE(base::DeleteFile(device_oobe_config_dir_.Append(kKeyFile)));
  EXPECT_FALSE(TestGetOobeConfigJson());
}

TEST_F(LoadOobeConfigUsbTest, FailVerifyPublicKey) {
  EXPECT_CALL(*load_config_, VerifyPublicKey()).WillOnce(Return(false));
  EXPECT_FALSE(TestGetOobeConfigJson());
}

TEST_F(LoadOobeConfigUsbTest, FailLocateUsbDevice) {
  EXPECT_TRUE(
      base::DeleteFile(fake_device_ids_dir_.GetPath().Append("dev_id2_sym")));
  EXPECT_FALSE(TestGetOobeConfigJson());
}

TEST_F(LoadOobeConfigUsbTest, FailMountUsbDevice) {
  EXPECT_CALL(*load_config_, MountUsbDevice(_, _)).WillOnce(Return(false));
  EXPECT_FALSE(TestGetOobeConfigJson());
}

// Ignores umount
TEST_F(LoadOobeConfigUsbTest, UnountUsbDevice) {
  EXPECT_CALL(*load_config_, UnmountUsbDevice(_)).WillOnce(Return(false));
  EXPECT_TRUE(TestGetOobeConfigJson());
}

TEST_F(LoadOobeConfigUsbTest, Cleanup) {
  EXPECT_TRUE(TestGetOobeConfigJson());
  EXPECT_TRUE(base::DirectoryExists(device_oobe_config_dir_));
  load_config_->CleanupFilesOnDevice();
  EXPECT_FALSE(base::DirectoryExists(device_oobe_config_dir_));
}

}  // namespace oobe_config
