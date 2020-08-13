// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/bootlockbox/boot_attributes.h"

#include <map>
#include <memory>
#include <string>

#include <base/compiler_specific.h>
#include <base/files/file_path.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/bootlockbox/mock_boot_lockbox.h"
#include "cryptohome/install_attributes.pb.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"

using base::FilePath;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace cryptohome {
namespace {

class BootAttributesTest : public testing::Test {
 public:
  BootAttributesTest() : fake_signature_("fake signature") {
  }

  void SetUp() override {
    ON_CALL(mock_boot_lockbox_, Sign(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(fake_signature_), Return(true)));

    ON_CALL(mock_boot_lockbox_, Verify(_, _))
        .WillByDefault(Return(true));

    ON_CALL(mock_platform_, ReadFile(_, _))
        .WillByDefault(Invoke(this, &BootAttributesTest::FakeReadFile));

    ON_CALL(mock_platform_, ReadFileToSecureBlob(_, _))
        .WillByDefault(
            Invoke(this, &BootAttributesTest::FakeReadFileToSecureBlob));

    ON_CALL(mock_platform_, WriteFile(_, _))
        .WillByDefault(Invoke(this, &BootAttributesTest::FakeWriteFile));

    CreateFakeFiles();

    boot_attributes_.reset(
        new BootAttributes(&mock_boot_lockbox_, &mock_platform_));
  }

  void CreateFakeFiles() {
    SerializedInstallAttributes message;
    message.set_version(1);
    SerializedInstallAttributes::Attribute* attr = message.add_attributes();
    attr->set_name("test1");
    attr->set_value("1234");

    brillo::Blob blob(message.ByteSizeLong());
    message.SerializeWithCachedSizesToArray(blob.data());
    files_[FilePath(BootAttributes::kAttributeFile)] = blob;

    blob.clear();
    files_[FilePath(BootAttributes::kSignatureFile)] = blob;
  }

  bool FakeReadFile(const FilePath& filename, brillo::Blob* blob) {
    *blob = files_[filename];
    return true;
  }

  bool FakeReadFileToSecureBlob(const FilePath& filename,
                                brillo::SecureBlob* sblob) {
    brillo::Blob temp = files_[filename];
    sblob->assign(temp.begin(), temp.end());
    return true;
  }

  bool FakeWriteFile(const FilePath& filename, brillo::Blob blob) {
    files_[filename] = blob;
    return true;
  }

 protected:
  const brillo::SecureBlob fake_signature_;

  NiceMock<MockBootLockbox> mock_boot_lockbox_;
  NiceMock<MockPlatform> mock_platform_;

  std::unique_ptr<BootAttributes> boot_attributes_;

  std::map<FilePath, brillo::Blob> files_;
};

TEST_F(BootAttributesTest, BasicOperations) {
  std::string value;

  // Empty initially.
  EXPECT_FALSE(boot_attributes_->Get("test1", &value));

  // Load values from the file.
  EXPECT_TRUE(boot_attributes_->Load());
  EXPECT_TRUE(boot_attributes_->Get("test1", &value));
  EXPECT_EQ("1234", value);

  // The value should still be unavailable after Set().
  boot_attributes_->Set("test2", "5678");
  EXPECT_FALSE(boot_attributes_->Get("test2", &value));

  // The value becomes available after FlushAndSign().
  EXPECT_TRUE(boot_attributes_->FlushAndSign());
  EXPECT_TRUE(boot_attributes_->Get("test2", &value));
  EXPECT_EQ("5678", value);

  // Overwrite a value
  boot_attributes_->Set("test1", "abcd");
  EXPECT_TRUE(boot_attributes_->FlushAndSign());
  EXPECT_TRUE(boot_attributes_->Get("test1", &value));
  EXPECT_EQ("abcd", value);

  // Verify the attribute file content.
  brillo::Blob blob = files_[FilePath(BootAttributes::kAttributeFile)];
  SerializedInstallAttributes message;
  message.ParseFromString(
      std::string(reinterpret_cast<char*>(blob.data()), blob.size()));
  EXPECT_EQ(BootAttributes::kAttributeFileVersion, message.version());

  EXPECT_EQ(2, message.attributes_size());
  bool has_test1 = false;
  bool has_test2 = false;
  for (int i = 0; i < message.attributes_size(); ++i) {
    const SerializedInstallAttributes::Attribute& attr = message.attributes(i);
    if (attr.name() == "test1") {
      has_test1 = true;
      EXPECT_EQ("abcd", attr.value());
    } else if (attr.name() == "test2") {
      has_test2 = true;
      EXPECT_EQ("5678", attr.value());
    } else {
      FAIL();
    }
  }
  EXPECT_TRUE(has_test1);
  EXPECT_TRUE(has_test2);

  // Verify the signature file content.
  blob = files_[FilePath(BootAttributes::kSignatureFile)];
  EXPECT_EQ("fake signature", std::string(
      reinterpret_cast<char*>(blob.data()), blob.size()));
}

TEST_F(BootAttributesTest, SignFailed) {
  EXPECT_CALL(mock_boot_lockbox_, Sign(_, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_platform_, WriteFile(_, _))
      .Times(0);

  boot_attributes_->Set("test", "1234");
  EXPECT_FALSE(boot_attributes_->FlushAndSign());
}

TEST_F(BootAttributesTest, WriteFileFailed) {
  EXPECT_CALL(mock_platform_, WriteFile(_, _))
      .WillRepeatedly(Return(false));

  boot_attributes_->Set("test", "1234");
  EXPECT_FALSE(boot_attributes_->FlushAndSign());
}

TEST_F(BootAttributesTest, ReadFileFailed) {
  EXPECT_CALL(mock_platform_, ReadFile(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(mock_platform_, ReadFileToSecureBlob(_, _))
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(boot_attributes_->Load());
}

TEST_F(BootAttributesTest, VerifyFailed) {
  EXPECT_CALL(mock_boot_lockbox_, Verify(_, _))
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(boot_attributes_->Load());
}

}  // namespace
}  // namespace cryptohome
