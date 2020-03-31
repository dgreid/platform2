// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/firmware_file.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <gtest/gtest.h>

#include "modemfwd/scoped_temp_file.h"

namespace modemfwd {
namespace {

constexpr char kFirmwareVersion[] = "1.0";

class FirmwareFileTest : public testing::Test {
 public:
  FirmwareFileTest() : temp_file_(ScopedTempFile::Create()) {
    CHECK(temp_file_);
  }

 protected:
  std::unique_ptr<ScopedTempFile> temp_file_;
  FirmwareFile firmware_file_;
};

TEST_F(FirmwareFileTest, PrepareFromUncompressedFile) {
  FirmwareFileInfo file_info(temp_file_->path(), kFirmwareVersion,
                             FirmwareFileInfo::Compression::NONE);
  EXPECT_TRUE(firmware_file_.PrepareFrom(file_info));
  EXPECT_EQ(file_info.firmware_path, firmware_file_.path_for_logging());
  EXPECT_EQ(file_info.firmware_path, firmware_file_.path_on_filesystem());
}

TEST_F(FirmwareFileTest, PrepareFromCompressedFileDecompressFailed) {
  static const uint8_t kInvalidContent[] = {0x00, 0x01};

  base::FilePath compressed_file_path = temp_file_->path().AddExtension(".xz");

  ASSERT_EQ(base::WriteFile(compressed_file_path,
                            reinterpret_cast<const char*>(kInvalidContent),
                            base::size(kInvalidContent)),
            base::size(kInvalidContent));
  FirmwareFileInfo file_info(compressed_file_path, kFirmwareVersion,
                             FirmwareFileInfo::Compression::XZ);
  EXPECT_FALSE(firmware_file_.PrepareFrom(file_info));
  EXPECT_EQ(base::FilePath(), firmware_file_.path_for_logging());
  EXPECT_EQ(base::FilePath(), firmware_file_.path_on_filesystem());
}

TEST_F(FirmwareFileTest, PrepareFromCompressedFileDecompressSucceeded) {
  // Generated from `echo test | xz | xxd -i`
  static const uint8_t kCompressedContent[] = {
      0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00, 0x00, 0x04, 0xe6, 0xd6, 0xb4,
      0x46, 0x02, 0x00, 0x21, 0x01, 0x16, 0x00, 0x00, 0x00, 0x74, 0x2f,
      0xe5, 0xa3, 0x01, 0x00, 0x04, 0x74, 0x65, 0x73, 0x74, 0x0a, 0x00,
      0x00, 0x00, 0x00, 0x9d, 0xed, 0x31, 0x1d, 0x0f, 0x9f, 0xd7, 0xe6,
      0x00, 0x01, 0x1d, 0x05, 0xb8, 0x2d, 0x80, 0xaf, 0x1f, 0xb6, 0xf3,
      0x7d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x59, 0x5a,
  };

  base::FilePath compressed_file_path = temp_file_->path().AddExtension(".xz");
  ASSERT_EQ(base::WriteFile(compressed_file_path,
                            reinterpret_cast<const char*>(kCompressedContent),
                            base::size(kCompressedContent)),
            base::size(kCompressedContent));

  FirmwareFileInfo file_info(compressed_file_path, kFirmwareVersion,
                             FirmwareFileInfo::Compression::XZ);
  EXPECT_TRUE(firmware_file_.PrepareFrom(file_info));
  EXPECT_EQ(file_info.firmware_path, firmware_file_.path_for_logging());
  EXPECT_NE(file_info.firmware_path, firmware_file_.path_on_filesystem());
  EXPECT_EQ(file_info.firmware_path.BaseName().RemoveFinalExtension(),
            firmware_file_.path_on_filesystem().BaseName());

  std::string content;
  EXPECT_TRUE(
      base::ReadFileToString(firmware_file_.path_on_filesystem(), &content));
  EXPECT_EQ("test\n", content);
}

}  // namespace
}  // namespace modemfwd
