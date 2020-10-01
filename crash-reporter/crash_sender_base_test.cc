// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_sender_base.h"

#include <string>

#include <base/files/scoped_temp_dir.h>
#include <base/guid.h>
#include <brillo/key_value_store.h>
#include <gtest/gtest.h>

#include "crash-reporter/crash_sender_paths.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/test_util.h"

namespace util {
namespace {

constexpr char kFakeClientId[] = "00112233445566778899aabbccddeeff";

// Creates the client ID file and stores the fake client ID in it.
bool CreateClientIdFile() {
  return test_util::CreateFile(
      paths::GetAt(paths::kCrashSenderStateDirectory, paths::kClientId),
      kFakeClientId);
}

class CrashSenderBaseTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_dir_ = temp_dir_.GetPath();
    paths::SetPrefixForTesting(test_dir_);
  }

  void TearDown() override { paths::SetPrefixForTesting(base::FilePath()); }

  base::ScopedTempDir temp_dir_;
  base::FilePath test_dir_;
};

TEST_F(CrashSenderBaseTest, GetBaseNameFromMetadata) {
  brillo::KeyValueStore metadata;
  metadata.LoadFromString("");
  EXPECT_EQ("", GetBaseNameFromMetadata(metadata, "payload").value());

  metadata.LoadFromString("payload=test.log\n");
  EXPECT_EQ("test.log", GetBaseNameFromMetadata(metadata, "payload").value());

  metadata.LoadFromString("payload=/foo/test.log\n");
  EXPECT_EQ("test.log", GetBaseNameFromMetadata(metadata, "payload").value());
}

TEST_F(CrashSenderBaseTest, GetKindFromPayloadPath) {
  EXPECT_EQ("", GetKindFromPayloadPath(base::FilePath()));
  EXPECT_EQ("", GetKindFromPayloadPath(base::FilePath("foo")));
  EXPECT_EQ("log", GetKindFromPayloadPath(base::FilePath("foo.log")));
  // "dmp" is a special case.
  EXPECT_EQ("minidump", GetKindFromPayloadPath(base::FilePath("foo.dmp")));

  // ".gz" should be ignored.
  EXPECT_EQ("log", GetKindFromPayloadPath(base::FilePath("foo.log.gz")));
  EXPECT_EQ("minidump", GetKindFromPayloadPath(base::FilePath("foo.dmp.gz")));
  EXPECT_EQ("", GetKindFromPayloadPath(base::FilePath("foo.gz")));

  // The directory name should not affect the function.
  EXPECT_EQ("minidump",
            GetKindFromPayloadPath(base::FilePath("/1.2.3/foo.dmp.gz")));
}

TEST_F(CrashSenderBaseTest, ParseMetadata) {
  brillo::KeyValueStore metadata;
  std::string value;
  EXPECT_TRUE(ParseMetadata("", &metadata));
  EXPECT_TRUE(ParseMetadata("log=test.log\n", &metadata));
  EXPECT_TRUE(ParseMetadata("#comment\nlog=test.log\n", &metadata));

  EXPECT_TRUE(metadata.GetString("log", &value));
  // This will clear the previously parsed data.
  EXPECT_TRUE(ParseMetadata("payload=test.dmp\n", &metadata));
  EXPECT_FALSE(metadata.GetString("log", &value));

  // Underscores, dashes, and periods should allowed, as Chrome uses them.
  // https://crbug.com/821530.
  EXPECT_TRUE(ParseMetadata("abcABC012_.-=test.log\n", &metadata));
  EXPECT_TRUE(metadata.GetString("abcABC012_.-", &value));
  EXPECT_EQ("test.log", value);

  // Invalid metadata should be detected.
  EXPECT_FALSE(ParseMetadata("=test.log\n", &metadata));
  EXPECT_FALSE(ParseMetadata("***\n", &metadata));
  EXPECT_FALSE(ParseMetadata("***=test.log\n", &metadata));
  EXPECT_FALSE(ParseMetadata("log\n", &metadata));
}

TEST_F(CrashSenderBaseTest, IsCompleteMetadata) {
  brillo::KeyValueStore metadata;
  metadata.LoadFromString("");
  EXPECT_FALSE(IsCompleteMetadata(metadata));

  metadata.LoadFromString("log=test.log\n");
  EXPECT_FALSE(IsCompleteMetadata(metadata));

  metadata.LoadFromString("log=test.log\ndone=1\n");
  EXPECT_TRUE(IsCompleteMetadata(metadata));

  metadata.LoadFromString("done=1\n");
  EXPECT_TRUE(IsCompleteMetadata(metadata));
}

TEST_F(CrashSenderBaseTest, CreateClientId) {
  std::string client_id = GetClientId();
  EXPECT_EQ(client_id.length(), 32);
  // Make sure it returns the same one multiple times.
  EXPECT_EQ(client_id, GetClientId());
}

TEST_F(CrashSenderBaseTest, RetrieveClientId) {
  CreateClientIdFile();
  EXPECT_EQ(kFakeClientId, GetClientId());
}

TEST_F(CrashSenderBaseTest, GetSleepTime) {
  const base::FilePath meta_file = test_dir_.Append("test.meta");
  base::TimeDelta max_spread_time = base::TimeDelta::FromSeconds(0);

  // This should fail since meta_file does not exist.
  base::TimeDelta sleep_time;
  EXPECT_FALSE(
      GetSleepTime(meta_file, max_spread_time, kMaxHoldOffTime, &sleep_time));

  ASSERT_TRUE(test_util::CreateFile(meta_file, ""));

  // sleep_time should be close enough to kMaxHoldOffTime since the meta file
  // was just created, but 10% error is allowed just in case.
  EXPECT_TRUE(
      GetSleepTime(meta_file, max_spread_time, kMaxHoldOffTime, &sleep_time));
  EXPECT_NEAR(kMaxHoldOffTime.InSecondsF(), sleep_time.InSecondsF(),
              kMaxHoldOffTime.InSecondsF() * 0.1);

  // Zero hold-off time and zero sleep time should always give zero sleep time.
  EXPECT_TRUE(GetSleepTime(meta_file, max_spread_time,
                           base::TimeDelta::FromSeconds(0) /*hold_off_time*/,
                           &sleep_time));
  EXPECT_EQ(base::TimeDelta::FromSeconds(0), sleep_time);

  // Even if file is new, a zero hold-off time means we choose a time between
  // 0 and max_spread_time.
  ASSERT_TRUE(test_util::TouchFileHelper(meta_file, base::Time::Now()));
  EXPECT_TRUE(GetSleepTime(
      meta_file, base::TimeDelta::FromSeconds(60) /*max_spread_time*/,
      base::TimeDelta::FromSeconds(0) /*hold_off_time*/, &sleep_time));
  EXPECT_LE(base::TimeDelta::FromSeconds(0), sleep_time);
  EXPECT_GE(base::TimeDelta::FromSeconds(60), sleep_time);

  // Make the meta file old enough so hold-off time is not necessary.
  const base::Time now = base::Time::Now();
  ASSERT_TRUE(test_util::TouchFileHelper(meta_file, now - kMaxHoldOffTime));

  // sleep_time should always be 0, since max_spread_time is set to 0.
  EXPECT_TRUE(
      GetSleepTime(meta_file, max_spread_time, kMaxHoldOffTime, &sleep_time));
  EXPECT_EQ(base::TimeDelta::FromSeconds(0), sleep_time);

  // sleep_time should be in range [0, 10].
  max_spread_time = base::TimeDelta::FromSeconds(10);
  EXPECT_TRUE(
      GetSleepTime(meta_file, max_spread_time, kMaxHoldOffTime, &sleep_time));
  EXPECT_LE(base::TimeDelta::FromSeconds(0), sleep_time);
  EXPECT_GE(base::TimeDelta::FromSeconds(10), sleep_time);

  // If the meta file is current, the minimum sleep time should be
  // kMaxHoldOffTime but the maximum is still max_spread_time.
  max_spread_time = base::TimeDelta::FromSeconds(60);
  ASSERT_TRUE(test_util::TouchFileHelper(meta_file, base::Time::Now()));
  EXPECT_TRUE(
      GetSleepTime(meta_file, max_spread_time, kMaxHoldOffTime, &sleep_time));
  // 0.9 in case we got preempted for 3 seconds between the file touch and the
  // GetSleepTime().
  EXPECT_LE(kMaxHoldOffTime * 0.9, sleep_time);
  EXPECT_GE(base::TimeDelta::FromSeconds(60), sleep_time);
}

}  // namespace
}  // namespace util
