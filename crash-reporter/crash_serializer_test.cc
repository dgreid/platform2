// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_serializer.h"

#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "crash-reporter/crash_sender_base.h"
#include "crash-reporter/crash_sender_paths.h"
#include "crash-reporter/crash_serializer.pb.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/test_util.h"

namespace crash_serializer {
namespace {

constexpr char kFakeClientId[] = "00112233445566778899aabbccddeeff";

}  // namespace

class CrashSerializerTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_dir_ = temp_dir_.GetPath();
    paths::SetPrefixForTesting(test_dir_);

    // Make sure the directory for the lock file exists.
    const base::FilePath lock_file_path =
        paths::Get(paths::kCrashSenderLockFile);
    const base::FilePath lock_file_directory = lock_file_path.DirName();
    ASSERT_TRUE(base::CreateDirectory(lock_file_directory));
  }

  void TearDown() override { paths::SetPrefixForTesting(base::FilePath()); }

  base::ScopedTempDir temp_dir_;
  base::FilePath test_dir_;
};

enum MissingFile {
  kNone,
  kPayloadFile,
  kLogFile,
  kTextFile,
  kBinFile,
  kCoreFile,
};

class CrashSerializerParameterizedTest
    : public CrashSerializerTest,
      public ::testing::WithParamInterface<
          std::tuple<bool, bool, MissingFile>> {
 protected:
  void SetUp() override {
    std::tie(absolute_paths_, fetch_core_, missing_file_) = GetParam();
    CrashSerializerTest::SetUp();
  }
  bool absolute_paths_;
  bool fetch_core_;
  MissingFile missing_file_;
};

TEST_P(CrashSerializerParameterizedTest, TestSerializeCrash) {
  const base::FilePath system_dir = paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(system_dir));

  const base::FilePath payload_file_relative("0.0.0.0.payload");
  const base::FilePath payload_file_absolute =
      system_dir.Append(payload_file_relative);
  const std::string payload_contents = "foobar_payload";
  if (missing_file_ != kPayloadFile) {
    ASSERT_TRUE(test_util::CreateFile(payload_file_absolute, payload_contents));
  }
  const base::FilePath& payload_file =
      absolute_paths_ ? payload_file_absolute : payload_file_relative;

  const base::FilePath log_file_relative("0.0.0.0.log");
  const base::FilePath log_file_absolute = system_dir.Append(log_file_relative);
  const std::string log_contents = "foobar_log";
  if (missing_file_ != kLogFile) {
    ASSERT_TRUE(test_util::CreateFile(log_file_absolute, log_contents));
  }
  const base::FilePath& log_file =
      absolute_paths_ ? log_file_absolute : log_file_relative;

  const base::FilePath text_var_file_relative("data.txt");
  const base::FilePath text_var_file_absolute =
      system_dir.Append(text_var_file_relative);
  const std::string text_var_contents = "upload_text_contents";
  if (missing_file_ != kTextFile) {
    ASSERT_TRUE(
        test_util::CreateFile(text_var_file_absolute, text_var_contents));
  }
  const base::FilePath& text_var_file =
      absolute_paths_ ? text_var_file_absolute : text_var_file_relative;

  const base::FilePath file_var_file_relative("data.bin");
  const base::FilePath file_var_file_absolute =
      system_dir.Append(file_var_file_relative);
  const std::string file_var_contents = "upload_file_contents";
  if (missing_file_ != kBinFile) {
    ASSERT_TRUE(
        test_util::CreateFile(file_var_file_absolute, file_var_contents));
  }
  const base::FilePath& file_var_file =
      absolute_paths_ ? file_var_file_absolute : file_var_file_relative;

  const base::FilePath core_file_relative("0.0.0.0.core");
  const base::FilePath core_file_absolute =
      system_dir.Append(core_file_relative);
  const std::string core_contents = "corey_mccoreface";
  if (missing_file_ != kCoreFile) {
    ASSERT_TRUE(test_util::CreateFile(core_file_absolute, core_contents));
  }

  brillo::KeyValueStore metadata;
  metadata.SetString("exec_name", "fake_exec_name");
  metadata.SetString("ver", "fake_chromeos_ver");
  metadata.SetString("upload_var_prod", "fake_product");
  metadata.SetString("upload_var_ver", "fake_version");
  metadata.SetString("sig", "fake_sig");
  metadata.SetString("upload_var_guid", "SHOULD_NOT_BE_USED");
  metadata.SetString("upload_var_foovar", "bar");
  metadata.SetString("upload_var_in_progress_integration_test", "test.Test");
  metadata.SetString("upload_var_collector", "fake_collector");
  metadata.SetString("upload_text_footext", text_var_file.value());
  metadata.SetString("upload_file_log", log_file.value());
  metadata.SetString("upload_file_foofile", file_var_file.value());
  metadata.SetString("error_type", "fake_error");

  util::CrashDetails details = {
      .meta_file = base::FilePath(system_dir).Append("0.0.0.0.meta"),
      .payload_file = payload_file,
      .payload_kind = "fake_payload",
      .client_id = kFakeClientId,
      .metadata = metadata,
  };

  Serializer::Options options;
  options.fetch_coredumps = fetch_core_;

  Serializer serializer(std::make_unique<test_util::AdvancingClock>(), options);

  crash::CrashInfo info;
  std::vector<crash::CrashBlob> blobs;
  base::FilePath core_path;
  EXPECT_EQ(serializer.SerializeCrash(details, &info, &blobs, &core_path),
            missing_file_ != kPayloadFile);

  if (missing_file_ == kPayloadFile) {
    return;
  }

  // We'd really like to set up a proto with the expected values and
  // EXPECT_THAT(info, EqualsProto(expected_info)), but EqualsProto is
  // unavailable in chromium OS, so do it one field at a time instead.
  EXPECT_EQ(info.exec_name(), "fake_exec_name");
  EXPECT_EQ(info.prod(), "fake_product");
  EXPECT_EQ(info.ver(), "fake_version");
  EXPECT_EQ(info.sig(), "fake_sig");
  EXPECT_EQ(info.in_progress_integration_test(), "test.Test");
  EXPECT_EQ(info.collector(), "fake_collector");

  int num_fields = 8;
  if (missing_file_ != kTextFile) {
    num_fields++;
  }

  ASSERT_EQ(info.fields_size(), num_fields);

  int field_idx = 0;
  EXPECT_EQ(info.fields(field_idx).key(), "board");
  EXPECT_EQ(info.fields(field_idx).text(), "undefined");
  field_idx++;

  EXPECT_EQ(info.fields(field_idx).key(), "hwclass");
  EXPECT_EQ(info.fields(field_idx).text(), "undefined");
  field_idx++;

  EXPECT_EQ(info.fields(field_idx).key(), "sig2");
  EXPECT_EQ(info.fields(field_idx).text(), "fake_sig");
  field_idx++;

  EXPECT_EQ(info.fields(field_idx).key(), "image_type");
  EXPECT_EQ(info.fields(field_idx).text(), "");
  field_idx++;

  EXPECT_EQ(info.fields(field_idx).key(), "boot_mode");
  EXPECT_EQ(info.fields(field_idx).text(), "missing-crossystem");
  field_idx++;

  EXPECT_EQ(info.fields(field_idx).key(), "error_type");
  EXPECT_EQ(info.fields(field_idx).text(), "fake_error");
  field_idx++;

  EXPECT_EQ(info.fields(field_idx).key(), "guid");
  EXPECT_EQ(info.fields(field_idx).text(), "00112233445566778899aabbccddeeff");
  field_idx++;

  if (missing_file_ != kTextFile) {
    EXPECT_EQ(info.fields(field_idx).key(), "footext");
    EXPECT_EQ(info.fields(field_idx).text(), "upload_text_contents");
    field_idx++;
  }

  EXPECT_EQ(info.fields(field_idx).key(), "foovar");
  EXPECT_EQ(info.fields(field_idx).text(), "bar");
  field_idx++;

  int num_blobs = 1;
  if (missing_file_ != kBinFile) {
    num_blobs++;
  }
  if (missing_file_ != kLogFile) {
    num_blobs++;
  }

  ASSERT_EQ(blobs.size(), num_blobs);

  int blob_idx = 0;
  EXPECT_EQ(blobs[blob_idx].key(), "upload_file_fake_payload");
  EXPECT_EQ(blobs[blob_idx].blob(), "foobar_payload");
  EXPECT_EQ(blobs[blob_idx].filename(), payload_file_relative.value());
  blob_idx++;

  if (missing_file_ != kBinFile) {
    EXPECT_EQ(blobs[blob_idx].key(), "foofile");
    EXPECT_EQ(blobs[blob_idx].blob(), "upload_file_contents");
    EXPECT_EQ(blobs[blob_idx].filename(), file_var_file_relative.value());
    blob_idx++;
  }

  if (missing_file_ != kLogFile) {
    EXPECT_EQ(blobs[blob_idx].key(), "log");
    EXPECT_EQ(blobs[blob_idx].blob(), "foobar_log");
    EXPECT_EQ(blobs[blob_idx].filename(), log_file_relative.value());
    blob_idx++;
  }

  if (missing_file_ != kCoreFile && fetch_core_) {
    EXPECT_EQ(core_path, core_file_absolute);
  } else {
    EXPECT_EQ(core_path, base::FilePath());
  }
}

INSTANTIATE_TEST_SUITE_P(CrashSerializerParameterizedTestInstantiation,
                         CrashSerializerParameterizedTest,
                         testing::Combine(testing::Bool(),
                                          testing::Bool(),
                                          testing::Values(kNone,
                                                          kPayloadFile,
                                                          kLogFile,
                                                          kTextFile,
                                                          kBinFile,
                                                          kCoreFile)));

}  // namespace crash_serializer
