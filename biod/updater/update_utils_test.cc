// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/updater/update_utils.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/file.h>
#include <base/files/scoped_temp_dir.h>
#include <cros_config/fake_cros_config.h>
#include <gtest/gtest.h>

#include "biod/biod_config.h"
#include "biod/utils.h"

namespace {

constexpr char kTestImageFileName[] = "nocturne_fp_v2.2.110-b936c0a3c.bin";

const base::FilePath kInitFilePath("/UNTOUCHED_PATH");

// (board_name, file_name)
// All |file_name|'s should be unique, so that tests can pull any
// combination of elements to test with.
// All |board_name|'s should be unique, so that tests can check for
// proper firmware name fetching when multiple valid firmwares are present.
// When |board_name| is "", use legacy update path.
const std::vector<std::pair<std::string, std::string>> kValidFirmwareNames = {
    std::make_pair("", kTestImageFileName),
    std::make_pair("", "unknown_fp_v123.123.123-123456789.bin"),
    std::make_pair("", "0_fp_0.bin"),
    std::make_pair("", "_fp_.bin"),
    std::make_pair("hatch_fp", "hatch_fp_v2.2.110-b936c0a3c.bin"),
    std::make_pair("dragonclaw", "dragonclaw_v1.0.4-b936c0a3c.bin"),
    std::make_pair("dragonguts", "dragonguts_v1.2.3-d00d8badf00d.bin"),
};

const std::vector<std::string> kInvalidFirmwareNames = {
    "nocturne_fp_v2.2.110-b936c0a3c.txt",
    "not_fpmcu_firmware.bin",
    "not_fpmcu_firmware.txt",
    "_fp_.txt",
    "file",
};

const std::vector<biod::updater::FindFirmwareFileStatus>
    kFindFirmwareFileStatuses = {
        biod::updater::FindFirmwareFileStatus::kFoundFile,
        biod::updater::FindFirmwareFileStatus::kNoDirectory,
        biod::updater::FindFirmwareFileStatus::kFileNotFound,
        biod::updater::FindFirmwareFileStatus::kMultipleFiles,
};

}  // namespace

namespace biod {
namespace updater {

class CrosFpUpdaterFindFirmwareTest : public ::testing::Test {
 protected:
  void SetUp() override { CHECK(ResetTestTempDir()); }

  void TearDown() override { EXPECT_TRUE(temp_dir_.Delete()); }

  bool ResetTestTempDir() {
    if (temp_dir_.IsValid()) {
      if (!temp_dir_.Delete()) {
        return false;
      }
    }
    return temp_dir_.CreateUniqueTempDir();
  }

  const base::FilePath& GetTestTempDir() const { return temp_dir_.GetPath(); }

  bool TouchFile(const base::FilePath& abspath) const {
    if (!GetTestTempDir().IsParent(abspath)) {
      LOG(ERROR) << "Asked to TouchFile outside test environment.";
      return false;
    }

    base::File file(abspath,
                    base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    EXPECT_TRUE(file.IsValid());
    file.Close();

    EXPECT_TRUE(base::PathExists(abspath));
    return true;
  }

  bool RemoveFile(const base::FilePath& abspath) const {
    return base::DeletePathRecursively(abspath);
  }

  CrosFpUpdaterFindFirmwareTest() = default;
  CrosFpUpdaterFindFirmwareTest(const CrosFpUpdaterFindFirmwareTest&) = delete;
  CrosFpUpdaterFindFirmwareTest& operator=(
      const CrosFpUpdaterFindFirmwareTest&) = delete;

  ~CrosFpUpdaterFindFirmwareTest() override = default;

  base::ScopedTempDir temp_dir_;
};

TEST_F(CrosFpUpdaterFindFirmwareTest, InvalidPathBlank) {
  brillo::FakeCrosConfig cros_config;
  base::FilePath out_file_path(kInitFilePath);
  // Given an empty directory path, searching for a firmware file
  auto status =
      FindFirmwareFile(base::FilePath(""), &cros_config, &out_file_path);
  // fails with a no directory error
  EXPECT_EQ(status, FindFirmwareFileStatus::kNoDirectory);
  // without modifying the output file path.
  EXPECT_EQ(out_file_path, kInitFilePath);
}

TEST_F(CrosFpUpdaterFindFirmwareTest, InvalidPathOddChars) {
  brillo::FakeCrosConfig cros_config;
  base::FilePath out_file_path(kInitFilePath);
  // Given "--" as directory path, searching for a firmware file
  auto status =
      FindFirmwareFile(base::FilePath("--"), &cros_config, &out_file_path);
  // fails with a no directory error
  EXPECT_EQ(status, FindFirmwareFileStatus::kNoDirectory);
  // without modifying the output file path.
  EXPECT_EQ(out_file_path, kInitFilePath);
}

TEST_F(CrosFpUpdaterFindFirmwareTest, DirectoryWithoutFirmware) {
  brillo::FakeCrosConfig cros_config;
  base::FilePath out_file_path(kInitFilePath);
  // Given a directory with no firmware files, searching for a firmware file
  auto status =
      FindFirmwareFile(GetTestTempDir(), &cros_config, &out_file_path);
  // fails with a file not found error
  EXPECT_EQ(status, FindFirmwareFileStatus::kFileNotFound);
  // without modifying the output file path.
  EXPECT_EQ(out_file_path, kInitFilePath);
}

TEST_F(CrosFpUpdaterFindFirmwareTest, OneGoodFirmwareFilePattern) {
  for (const auto& good_fw : kValidFirmwareNames) {
    brillo::FakeCrosConfig cros_config;
    base::FilePath fw_file_path, out_file_path;
    CHECK(ResetTestTempDir());

    // Given a directory with one correctly named firmware file
    fw_file_path = GetTestTempDir().Append(good_fw.second);
    EXPECT_TRUE(TouchFile(fw_file_path));
    // and a cros-config with an appropriate fingerprint board name,
    if (!good_fw.first.empty()) {
      cros_config.SetString(kCrosConfigFPPath, kCrosConfigFPBoard,
                            good_fw.first);
    }

    // searching for a firmware file
    auto status =
        FindFirmwareFile(GetTestTempDir(), &cros_config, &out_file_path);
    // succeeds
    EXPECT_EQ(status, FindFirmwareFileStatus::kFoundFile);
    // and returns the path to the original firmware file.
    EXPECT_EQ(out_file_path, fw_file_path);
  }
}

TEST_F(CrosFpUpdaterFindFirmwareTest, OneBadFirmwareFilePattern) {
  for (const auto& bad_fw_name : kInvalidFirmwareNames) {
    brillo::FakeCrosConfig cros_config;
    base::FilePath fw_file_path, out_file_path(kInitFilePath);
    CHECK(ResetTestTempDir());

    // Given a directory with one incorrectly named firmware file,
    fw_file_path = GetTestTempDir().Append(bad_fw_name);
    EXPECT_TRUE(TouchFile(fw_file_path));

    // searching for a firmware file
    auto status =
        FindFirmwareFile(GetTestTempDir(), &cros_config, &out_file_path);
    // fails with a file not found error
    EXPECT_EQ(status, FindFirmwareFileStatus::kFileNotFound);
    // without modifying the output file path.
    EXPECT_EQ(out_file_path, kInitFilePath);
  }
}

TEST_F(CrosFpUpdaterFindFirmwareTest, MultipleValidFiles) {
  // Given a directory with multiple correctly named firmware files
  for (const auto& good_fw : kValidFirmwareNames) {
    EXPECT_TRUE(TouchFile(GetTestTempDir().Append(good_fw.second)));
  }

  for (const auto& good_fw : kValidFirmwareNames) {
    brillo::FakeCrosConfig cros_config;
    base::FilePath fw_file_path, out_file_path;

    // and a cros-config fingerprint board name,
    if (good_fw.first.empty()) {
      continue;
    }
    cros_config.SetString(kCrosConfigFPPath, kCrosConfigFPBoard, good_fw.first);

    // searching for a firmware file
    auto status =
        FindFirmwareFile(GetTestTempDir(), &cros_config, &out_file_path);
    // succeeds
    EXPECT_EQ(status, FindFirmwareFileStatus::kFoundFile);
    // and returns the path to the corresponding firmware file.
    EXPECT_EQ(out_file_path, GetTestTempDir().Append(good_fw.second));
  }
}

TEST_F(CrosFpUpdaterFindFirmwareTest, MultipleValidFilesExceptSpecifc) {
  // Given a directory with multiple correctly named firmware files and
  for (const auto& good_fw : kValidFirmwareNames) {
    EXPECT_TRUE(TouchFile(GetTestTempDir().Append(good_fw.second)));
  }

  for (const auto& good_fw : kValidFirmwareNames) {
    brillo::FakeCrosConfig cros_config;
    base::FilePath fw_file_path, out_file_path(kInitFilePath);
    const auto good_file_path = GetTestTempDir().Append(good_fw.second);

    // a cros-config fingerprint board name,
    if (good_fw.first.empty()) {
      continue;
    }
    cros_config.SetString(kCrosConfigFPPath, kCrosConfigFPBoard, good_fw.first);

    // but missing the board specific firmware file,
    EXPECT_TRUE(RemoveFile(good_file_path));

    // searching for a firmware file
    auto status =
        FindFirmwareFile(GetTestTempDir(), &cros_config, &out_file_path);
    // fails with a file not found error
    EXPECT_EQ(status, FindFirmwareFileStatus::kFileNotFound);
    // without modifying the output file path.
    EXPECT_EQ(out_file_path, kInitFilePath);

    EXPECT_TRUE(TouchFile(good_file_path));
  }
}

TEST_F(CrosFpUpdaterFindFirmwareTest, MultipleFilesError) {
  brillo::FakeCrosConfig cros_config;
  base::FilePath out_file_path(kInitFilePath);

  // Given a directory with two correctly named firmware files,
  EXPECT_GE(kValidFirmwareNames.size(), 2);
  EXPECT_TRUE(
      TouchFile(GetTestTempDir().Append(kValidFirmwareNames[0].second)));
  EXPECT_TRUE(
      TouchFile(GetTestTempDir().Append(kValidFirmwareNames[1].second)));

  // searching for a firmware file
  auto status =
      FindFirmwareFile(GetTestTempDir(), &cros_config, &out_file_path);

  // fails with a multiple files error
  EXPECT_EQ(status, FindFirmwareFileStatus::kMultipleFiles);
  // without modifying the output file path.
  EXPECT_EQ(out_file_path, kInitFilePath);
}

TEST_F(CrosFpUpdaterFindFirmwareTest, OneGoodAndOneBadFirmwareFilePattern) {
  brillo::FakeCrosConfig cros_config;
  base::FilePath out_file_path, good_file_path, bad_file_path;

  // Given a directory with one correctly named and one incorrectly named
  // firmware file,
  good_file_path = GetTestTempDir().Append(kValidFirmwareNames[0].second);
  bad_file_path = GetTestTempDir().Append(kInvalidFirmwareNames[0]);
  EXPECT_TRUE(TouchFile(good_file_path));
  EXPECT_TRUE(TouchFile(bad_file_path));

  // searching for a firmware file
  auto status =
      FindFirmwareFile(GetTestTempDir(), &cros_config, &out_file_path);
  // succeeds
  EXPECT_EQ(status, FindFirmwareFileStatus::kFoundFile);
  // and returns the path to the single correctly named firmware file.
  EXPECT_EQ(out_file_path, good_file_path);
}

TEST_F(CrosFpUpdaterFindFirmwareTest, NonblankStatusMessages) {
  // Given a FindFirmwareFile status
  for (auto status : kFindFirmwareFileStatuses) {
    // when we ask for the human readable string
    std::string msg = FindFirmwareFileStatusToString(status);
    // expect it to not be "".
    EXPECT_FALSE(msg.empty()) << "Status " << to_utype(status)
                              << " converts to a blank status string.";
  }
}

TEST_F(CrosFpUpdaterFindFirmwareTest, UniqueStatusMessages) {
  // Given a set of all FindFirmwareFile status messages
  std::unordered_set<std::string> status_msgs;
  for (auto status : kFindFirmwareFileStatuses) {
    status_msgs.insert(FindFirmwareFileStatusToString(status));
  }

  // expect the set to contain the same number of unique messages
  // as there are original statuses.
  EXPECT_EQ(status_msgs.size(), kFindFirmwareFileStatuses.size())
      << "There are one or more non-unique status messages.";
}

}  // namespace updater
}  // namespace biod
