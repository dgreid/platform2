// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/save_oobe_config_usb.h"

#include <memory>
#include <string>

#include <base/environment.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "oobe_config/mock_save_oobe_config_usb.h"
#include "oobe_config/usb_utils.h"

using base::FilePath;
using base::ScopedTempDir;
using std::string;
using std::unique_ptr;
using testing::_;
using testing::Return;

namespace oobe_config {

class SaveOobeConfigUsbTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EXPECT_TRUE(fake_device_stateful_.CreateUniqueTempDir());
    EXPECT_TRUE(fake_usb_stateful_.CreateUniqueTempDir());
    EXPECT_TRUE(fake_device_ids_dir_.CreateUniqueTempDir());
    EXPECT_TRUE(everything_else_.CreateUniqueTempDir());
    EXPECT_TRUE(base::CreateDirectory(
        fake_usb_stateful_.GetPath().Append(kUnencryptedOobeConfigDir)));
    device_oobe_config_dir_ =
        fake_device_stateful_.GetPath().Append(kUnencryptedOobeConfigDir);

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

    // Create config file.
    config_file_ = fake_usb_stateful_.GetPath()
                       .Append(kUnencryptedOobeConfigDir)
                       .Append(kConfigFile);
    constexpr char dummy_config[] = "dummy config content";
    EXPECT_EQ(base::WriteFile(config_file_, dummy_config, strlen(dummy_config)),
              strlen(dummy_config));

    // Create enrollment domain file.
    enrollment_domain_file_ = fake_usb_stateful_.GetPath()
                                  .Append(kUnencryptedOobeConfigDir)
                                  .Append(kDomainFile);
    constexpr char dummy_domain[] = "test@";
    EXPECT_EQ(base::WriteFile(enrollment_domain_file_, dummy_domain,
                              strlen(dummy_domain)),
              strlen(dummy_domain));

    string source_path;
    std::unique_ptr<base::Environment> env(base::Environment::Create());
    EXPECT_TRUE(env->GetVar("SRC", &source_path));
    auto public_key = FilePath(source_path).Append("test.pub.key");
    auto private_key = FilePath(source_path).Append("test.pri.key");

    save_config_ = std::make_unique<MockSaveOobeConfigUsb>(
        fake_device_stateful_.GetPath(), fake_usb_stateful_.GetPath(),
        fake_device_ids_dir_.GetPath(), dev_id2, private_key, public_key);
    EXPECT_TRUE(save_config_);
  }

  ScopedTempDir fake_usb_stateful_;
  ScopedTempDir fake_device_stateful_;
  ScopedTempDir fake_device_ids_dir_;
  ScopedTempDir everything_else_;

  FilePath device_oobe_config_dir_;
  FilePath config_file_;
  FilePath enrollment_domain_file_;

  unique_ptr<MockSaveOobeConfigUsb> save_config_;
};

TEST_F(SaveOobeConfigUsbTest, Save) {
  EXPECT_TRUE(save_config_->Save());
  EXPECT_TRUE(base::PathExists(
      device_oobe_config_dir_.Append(kConfigFile).AddExtension("sig")));
  EXPECT_TRUE(base::PathExists(
      device_oobe_config_dir_.Append(kDomainFile).AddExtension("sig")));
  EXPECT_TRUE(base::PathExists(
      device_oobe_config_dir_.Append(kDomainFile).AddExtension("sig")));
  EXPECT_TRUE(
      base::PathExists(device_oobe_config_dir_.Append(kUsbDevicePathSigFile)));
  EXPECT_TRUE(base::PathExists(device_oobe_config_dir_.Append(kKeyFile)));
}

TEST_F(SaveOobeConfigUsbTest, SaveWithoutEnrollmentDomainFile) {
  EXPECT_TRUE(base::DeleteFile(enrollment_domain_file_));
  EXPECT_TRUE(save_config_->Save());
  EXPECT_TRUE(base::PathExists(
      device_oobe_config_dir_.Append(kConfigFile).AddExtension("sig")));
  EXPECT_FALSE(base::PathExists(
      device_oobe_config_dir_.Append(kDomainFile).AddExtension("sig")));
  EXPECT_TRUE(
      base::PathExists(device_oobe_config_dir_.Append(kUsbDevicePathSigFile)));
  EXPECT_TRUE(base::PathExists(device_oobe_config_dir_.Append(kKeyFile)));
}

TEST_F(SaveOobeConfigUsbTest, SaveFailNoConfig) {
  EXPECT_TRUE(base::DeleteFile(config_file_));
  EXPECT_FALSE(save_config_->Save());
}

TEST_F(SaveOobeConfigUsbTest, SaveFailNoDeviceId) {
  EXPECT_TRUE(
      base::DeleteFile(fake_device_ids_dir_.GetPath().Append("dev_id2_sym")));
  EXPECT_FALSE(save_config_->Save());
}

TEST_F(SaveOobeConfigUsbTest, SaveFailNoDeviceStateful) {
  EXPECT_TRUE(base::DeleteFile(fake_device_stateful_.GetPath()));
  EXPECT_FALSE(save_config_->Save());
}

TEST_F(SaveOobeConfigUsbTest, SaveFailNoUsbStateful) {
  EXPECT_TRUE(base::DeletePathRecursively(fake_usb_stateful_.GetPath()));
  EXPECT_FALSE(save_config_->Save());
}

TEST_F(SaveOobeConfigUsbTest, SaveFailNoUsbUnencrypted) {
  EXPECT_TRUE(base::DeletePathRecursively(
      fake_usb_stateful_.GetPath().Append(kUnencryptedOobeConfigDir)));
  EXPECT_FALSE(save_config_->Save());
}

}  // namespace oobe_config
