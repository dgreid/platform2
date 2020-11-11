// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <base/files/file_util.h>
#include <chromeos-config/libcros_config/fake_cros_config.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_config_loader_impl.h"
#include "runtime_probe/system_property_impl.h"

namespace runtime_probe {

namespace {

constexpr char kUsrLocal[] = "usr/local";

base::FilePath GetTestDataPath() {
  char* src_env = std::getenv("SRC");
  CHECK_NE(src_env, nullptr)
      << "Expect to have the envvar |SRC| set when testing.";
  return base::FilePath(src_env).Append("testdata");
}

class MockSystemProperty : public SystemProperty {
 public:
  MOCK_METHOD(bool,
              GetInt,
              (const std::string& key, int* value_out),
              (override));
  MOCK_METHOD(bool, SetInt, (const std::string& key, int value), (override));
  MOCK_METHOD(bool,
              GetString,
              (const std::string& key, std::string* value_out),
              (override));
  MOCK_METHOD(bool,
              SetString,
              (const std::string& key, const std::string& value),
              (override));
};

class ProbeConfigLoaderImplTest : public ::testing::Test {
 protected:
  void SetUp() {
    PCHECK(scoped_temp_dir_.CreateUniqueTempDir());

    auto cros_config = std::make_unique<brillo::FakeCrosConfig>();
    auto mock_system_property = std::make_unique<MockSystemProperty>();
    cros_config_ = cros_config.get();
    mock_system_property_ = mock_system_property.get();
    testdata_root_ = GetTestDataPath();

    probe_config_loader_ = std::make_unique<ProbeConfigLoaderImpl>();
    probe_config_loader_->SetCrosConfigForTesting(std::move(cros_config));
    probe_config_loader_->SetSystemProertyForTesting(
        std::move(mock_system_property));
    probe_config_loader_->SetRootForTest(GetRootDir());
  }

  // Sets model names to the given value.
  void SetModel(const std::string& val) {
    cros_config_->SetString(kCrosConfigModelNamePath, kCrosConfigModelNameKey,
                            val);
  }

  // Sets cros_debug flag to the given value.
  void SetCrosDebugFlag(int value) {
    EXPECT_CALL(*mock_system_property_,
                GetInt(std::string("cros_debug"), ::testing::_))
        .WillRepeatedly(::testing::DoAll(::testing::SetArgPointee<1>(value),
                                         ::testing::Return(true)));
  }

  // Gets the root directory path used for unit test.
  const base::FilePath& GetRootDir() const {
    return scoped_temp_dir_.GetPath();
  }

  // Creates parent directories as needed before copying the file.
  bool CreateDirectoryAndCopyFile(const base::FilePath& from_path,
                                  const base::FilePath& to_path) const {
    PCHECK(base::CreateDirectoryAndGetError(to_path.DirName(), nullptr));
    PCHECK(base::CopyFile(from_path, to_path));
    return true;
  }

  std::unique_ptr<ProbeConfigLoaderImpl> probe_config_loader_;
  MockSystemProperty* mock_system_property_;
  brillo::FakeCrosConfig* cros_config_;
  base::FilePath testdata_root_;

 private:
  base::ScopedTempDir scoped_temp_dir_;
};

}  // namespace

TEST(ProbeConfigLoaderImplTestConstructor, DefaultConstructor) {
  auto probe_config_loader_ = std::make_unique<ProbeConfigLoaderImpl>();
  EXPECT_NE(probe_config_loader_, nullptr);
}

TEST_F(ProbeConfigLoaderImplTest, LoadFromFile_WithoutCrosDebug) {
  SetCrosDebugFlag(0);
  const base::FilePath rel_path{kRuntimeProbeConfigName};
  const auto rel_file_path = testdata_root_.Append(kRuntimeProbeConfigName);

  const auto probe_config = probe_config_loader_->LoadFromFile(rel_file_path);
  EXPECT_FALSE(probe_config);
}

TEST_F(ProbeConfigLoaderImplTest, LoadFromFile_WithCrosDebug_RelativePath) {
  SetCrosDebugFlag(1);
  const base::FilePath rel_path{kRuntimeProbeConfigName};
  const auto rel_file_path = testdata_root_.Append(kRuntimeProbeConfigName);
  const auto abs_file_path = base::MakeAbsoluteFilePath(rel_file_path);

  const auto probe_config = probe_config_loader_->LoadFromFile(rel_file_path);
  EXPECT_TRUE(probe_config);
  EXPECT_EQ(probe_config->path, abs_file_path);
  EXPECT_FALSE(probe_config->config.DictEmpty());
  EXPECT_EQ(probe_config->sha1_hash,
            "B4B67B8FB7B094783926CC581850C492C5A246A4");
}

TEST_F(ProbeConfigLoaderImplTest, LoadFromFile_WithCrosDebug_AbsolutePath) {
  SetCrosDebugFlag(1);
  const base::FilePath rel_path{kRuntimeProbeConfigName};
  const auto rel_file_path = testdata_root_.Append(kRuntimeProbeConfigName);
  const auto abs_file_path = base::MakeAbsoluteFilePath(rel_file_path);

  const auto probe_config = probe_config_loader_->LoadFromFile(abs_file_path);
  EXPECT_TRUE(probe_config);
  EXPECT_EQ(probe_config->path, abs_file_path);
  EXPECT_FALSE(probe_config->config.DictEmpty());
  EXPECT_EQ(probe_config->sha1_hash,
            "B4B67B8FB7B094783926CC581850C492C5A246A4");
}
TEST_F(ProbeConfigLoaderImplTest, LoadFromFile_MissingFile) {
  SetCrosDebugFlag(1);
  const base::FilePath rel_path{"missing_file.json"};

  const auto probe_config = probe_config_loader_->LoadFromFile(rel_path);
  EXPECT_FALSE(probe_config);
}

TEST_F(ProbeConfigLoaderImplTest, LoadFromFile_InvalidFile) {
  SetCrosDebugFlag(1);
  const base::FilePath rel_path{"invalid_config.json"};
  const char invalid_probe_config[] = "foo\nbar";
  PCHECK(WriteFile(GetRootDir().Append(rel_path), invalid_probe_config));

  const auto probe_config = probe_config_loader_->LoadFromFile(rel_path);
  EXPECT_FALSE(probe_config);
}

TEST_F(ProbeConfigLoaderImplTest, GetDefaultPaths_WithoutCrosDebug) {
  const char model_name[] = "ModelFoo";
  SetCrosDebugFlag(0);
  SetModel(model_name);
  const auto default_paths = probe_config_loader_->GetDefaultPaths();
  EXPECT_THAT(default_paths, ::testing::ElementsAreArray({
                                 GetRootDir()
                                     .Append(kRuntimeProbeConfigDir)
                                     .Append(model_name)
                                     .Append(kRuntimeProbeConfigName),
                                 GetRootDir()
                                     .Append(kRuntimeProbeConfigDir)
                                     .Append(kRuntimeProbeConfigName),
                             }));
}

TEST_F(ProbeConfigLoaderImplTest, GetDefaultPaths_WithCrosDebug) {
  const char model_name[] = "ModelFoo";
  SetCrosDebugFlag(1);
  SetModel(model_name);
  const auto default_paths = probe_config_loader_->GetDefaultPaths();
  EXPECT_THAT(default_paths, ::testing::ElementsAreArray({
                                 GetRootDir()
                                     .Append(kUsrLocal)
                                     .Append(kRuntimeProbeConfigDir)
                                     .Append(model_name)
                                     .Append(kRuntimeProbeConfigName),
                                 GetRootDir()
                                     .Append(kUsrLocal)
                                     .Append(kRuntimeProbeConfigDir)
                                     .Append(kRuntimeProbeConfigName),
                                 GetRootDir()
                                     .Append(kRuntimeProbeConfigDir)
                                     .Append(model_name)
                                     .Append(kRuntimeProbeConfigName),
                                 GetRootDir()
                                     .Append(kRuntimeProbeConfigDir)
                                     .Append(kRuntimeProbeConfigName),
                             }));
}

TEST_F(ProbeConfigLoaderImplTest, LoadDefault_WithoutCrosDebug) {
  const char model_name[] = "ModelFoo";
  SetCrosDebugFlag(0);
  SetModel(model_name);
  const base::FilePath config_a_path{"probe_config.json"};
  const base::FilePath config_b_path{"probe_config_b.json"};
  const base::FilePath rootfs_config_path =
      GetRootDir().Append(kRuntimeProbeConfigDir);
  const base::FilePath stateful_partition_config_path =
      GetRootDir().Append(kUsrLocal).Append(kRuntimeProbeConfigDir);

  // Copy config_a to rootfs.
  CreateDirectoryAndCopyFile(
      testdata_root_.Append(config_a_path),
      rootfs_config_path.Append(model_name).Append(kRuntimeProbeConfigName));
  // Copy config_b to stateful partition.
  CreateDirectoryAndCopyFile(testdata_root_.Append(config_b_path),
                             stateful_partition_config_path.Append(model_name)
                                 .Append(kRuntimeProbeConfigName));

  const auto probe_config = probe_config_loader_->LoadDefault();
  EXPECT_TRUE(probe_config);
  EXPECT_EQ(
      probe_config->path,
      rootfs_config_path.Append(model_name).Append(kRuntimeProbeConfigName));
  EXPECT_FALSE(probe_config->config.DictEmpty());
  EXPECT_EQ(probe_config->sha1_hash,
            "B4B67B8FB7B094783926CC581850C492C5A246A4");
}

TEST_F(ProbeConfigLoaderImplTest, LoadDefault_WithCrosDebug) {
  const char model_name[] = "ModelFoo";
  SetCrosDebugFlag(1);
  SetModel(model_name);
  const base::FilePath config_a_path{"probe_config.json"};
  const base::FilePath config_b_path{"probe_config_b.json"};
  const base::FilePath rootfs_config_path =
      GetRootDir().Append(kRuntimeProbeConfigDir);
  const base::FilePath stateful_partition_config_path =
      GetRootDir().Append(kUsrLocal).Append(kRuntimeProbeConfigDir);

  // Copy config_a to rootfs.
  CreateDirectoryAndCopyFile(
      testdata_root_.Append(config_a_path),
      rootfs_config_path.Append(model_name).Append(kRuntimeProbeConfigName));
  // Copy config_b to stateful partition.
  CreateDirectoryAndCopyFile(testdata_root_.Append(config_b_path),
                             stateful_partition_config_path.Append(model_name)
                                 .Append(kRuntimeProbeConfigName));

  const auto probe_config = probe_config_loader_->LoadDefault();
  EXPECT_TRUE(probe_config);
  EXPECT_EQ(probe_config->path,
            stateful_partition_config_path.Append(model_name)
                .Append(kRuntimeProbeConfigName));
  EXPECT_FALSE(probe_config->config.DictEmpty());
  EXPECT_EQ(probe_config->sha1_hash,
            "BC65881109108FB248B76554378AC493CD4D5C6D");
}

TEST_F(ProbeConfigLoaderImplTest, LoadDefault_MissingFile) {
  const char model_name[] = "ModelFoo";
  SetCrosDebugFlag(0);
  SetModel(model_name);

  const auto probe_config = probe_config_loader_->LoadDefault();
  EXPECT_FALSE(probe_config);
}

}  // namespace runtime_probe
